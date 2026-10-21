#include "cameras.h"

#include <cmath>

CameraRig::CameraRig(Camera* cam) : camera(cam), orbit(cam) {
  orbit.enabled = false;
  orbit.enableDamping = true;
  orbit.maxPolarAngle = (float)M_PI * 0.495f;
  orbit.minDistance = 2.5f;
  orbit.maxDistance = 40;
}

void CameraRig::next() {
  mode = (mode + 1) % (int)modes.size();
  bool free = name() == "Livre";
  orbit.enabled = free;
  if (free) orbit.target = lastCar;
  camera->near = mode >= 2 && !free ? 0.03f : 0.1f;
  // esconde o capacete do piloto na câmera de cockpit
  for (Node* m : hideInCockpit) m->visible = name() != "Cockpit";
}

void CameraRig::update(float dt, Node& carObj, Node& visual, const phys::Vehicle& car) {
  Camera& cam = *camera;
  vec3 p = carObj.position;
  quat q = carObj.quaternion;
  vec3 fwd = q.rotate({0, 0, -1});
  const std::string& nm = name();
  vec3 vel((float)car.vel.x, (float)car.vel.y, (float)car.vel.z);
  bool chase = nm.rfind("Perseguição", 0) == 0;

  if (nm == "Livre" || chase) cam.up = vec3(0, 1, 0);
  if (nm == "Livre") {
    orbit.target += p - lastCar;
    cam.position += p - lastCar;
    orbit.update();
    cam.fov = 55;
  } else if (chase) {
    bool far = nm == "Perseguição longe";
    // a câmera acompanha a direção do movimento (com atraso), não a rotação crua do carro
    float heading = std::atan2(-fwd.x, -fwd.z);
    float moveHeading = length(vel) > 3 ? std::atan2(-vel.x, -vel.z) : heading;
    float target = moveHeading;
    if (car.forwardSpeed() < -1) target = heading;  // em ré, não gira a câmera
    float dy = target - yaw;
    dy = std::atan2(std::sin(dy), std::cos(dy));
    yaw += dy * (1 - std::exp(-dt * 4.5f));
    float dist = far ? 8.5f : 5.6f, height = far ? 2.6f : 1.65f;
    vec3 back(std::sin(yaw), 0, std::cos(yaw));
    // suaviza o deslocamento relativo ao carro (sem ficar para trás em alta velocidade)
    vec3 desired = back * dist + vec3(0, height, 0);
    pos = lerp(pos, desired, 1 - std::exp(-dt * 10));
    cam.position = p + pos;
    // vibração leve em alta velocidade
    float kmh = length(vel) * 3.6f;
    if (kmh > 120) {
      t += dt;
      float amp = std::min(1.f, (kmh - 120) / 160) * 0.012f;
      cam.position.x += std::sin(t * 37.1f) * std::sin(t * 11.3f) * amp;
      cam.position.y += std::sin(t * 43.7f + 1.3f) * std::sin(t * 7.9f) * amp;
    }
    if (cam.position.y < 0.3f) cam.position.y = 0.3f;
    cam.lookAt(p + vec3(0, 0.75f, 0) + back * -1.5f);
    cam.fov = 58 + std::min(14.f, kmh / 22);
  } else {
    // câmeras presas à carroceria (no referencial do modelo: frente = −z)
    vec3 local = nm == "Capô" ? hoodEye : cockpitEye;
    if (nm == "Cockpit") {
      // a cabeça do piloto é jogada pelas forças G (pescoço = mola amortecida)
      const auto& a = car.accel;
      head = lerp(head, vec3((float)-a.x * 0.0028f, (float)-a.y * 0.0012f, (float)-a.z * 0.0032f), 1 - std::exp(-dt * 7));
      local += head;
    }
    vec3 world = visual.localToWorld(local);
    cam.position = world;
    vec3 ahead = visual.localToWorld(local + vec3(0, -0.05f, -10));
    cam.up = q.rotate({0, 1, 0});
    cam.lookAt(ahead);
    cam.fov = nm == "Capô" ? 62.f : 68.f;
  }
  lastCar = p;
}
