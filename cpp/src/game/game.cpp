#include "game.h"

#include <chrono>
#include <cmath>
#include <cstdlib>

static const double PHYSICS_DT = 1.0 / 1000;  // 1000 Hz, passo fixo

Game::Game(Renderer& r, Input& in) : renderer(r), input(in), rig(&camera) {
  camera.fov = 60;
  camera.near = 0.1f;
  camera.far = 12000;
}

Game::~Game() {
  if (prepThread.joinable()) prepThread.join();
}

void Game::beginPrepare(const MapEntry& m, int timeOfDay) {
  if (prepThread.joinable()) return;
  map = &m;
  // outro horário: o céu, a luz e a neblina mudam, então o mundo é recarregado (as pistas ficam)
  if (world && worldReady && world->timeOfDay != timeOfDay) {
    world->setTrack(nullptr);  // solta a pista da cena antiga (senão o nó fica com pai inválido)
    world.reset();
    worldReady = false;
  }
  bool needTrack = !m.flat && !tracks.count(m.id);
  ready = worldReady && !needTrack;
  if (ready) {
    world->setTrack(m.flat ? nullptr : tracks[m.id].get());
    return;
  }
  if (!world) world = std::make_unique<World>();
  world->timeOfDay = timeOfDay;
  if (needTrack) loadingTrack = std::make_unique<TrackMap>();
  prepDone = false;
  prepProgress = 0;
  prepError.clear();
  bool loadWorld = !worldReady;
  prepThread = std::thread([this, loadWorld, needTrack, &m] {
    try {
      // céu/asfalto ~30% e pista ~70% da barra
      float wShare = needTrack ? (loadWorld ? 0.3f : 0.f) : 1.f;
      if (loadWorld) world->loadCPU([this, wShare](float p) { prepProgress = p * wShare; });
      if (needTrack) loadingTrack->loadCPU(m, [this, wShare](float p) { prepProgress = wShare + p * (1 - wShare) * 0.97f; });
    } catch (std::exception& e) {
      prepError = e.what();
    }
    prepDone = true;
  });
}

bool Game::pollPrepare(std::string& error) {
  if (ready) return true;
  if (!prepDone) return false;
  prepThread.join();
  if (!prepError.empty()) {
    error = prepError;
    loadingTrack.reset();
    if (!worldReady) world.reset();
    return true;
  }
  if (!worldReady) {
    world->finishGPU();
    skids = std::make_unique<SkidMarks>();
    smoke = std::make_unique<TireSmoke>();
    world->scene.transparentFirst.push_back(skids.get());
    world->scene.transparentLast.push_back(smoke.get());
    world->setShadowQuality(fx.quality().shadow);
    if (settings) {
      world->setShadowDistance((float)settings->shadowDist);
      world->setShadowSoftness(settings->shadowSoft);
    }
    carObj = makeNode("car");
    visual = makeNode("visual");
    carObj->add(visual);
    world->scene.root->add(carObj);
    worldReady = true;
  }
  if (loadingTrack) {
    auto t0 = SDL_GetTicks();
    loadingTrack->upload();
    std::printf("[pista] envio à GPU: %.1f s\n", (SDL_GetTicks() - t0) / 1000.0);
    tracks[map->id] = std::move(loadingTrack);
  }
  world->setTrack(map->flat ? nullptr : tracks[map->id].get());
  prepProgress = 1;
  ready = true;
  return true;
}

void Game::start(const CarEntry& e, const std::shared_ptr<LoadedCar>& l) {
  const phys::CarSpec& spec = *e.spec;
  entry = &e;
  loaded = l;
  hud = std::make_unique<Hud>(spec);
  ground = world->physicsGround();
  car = std::make_unique<phys::Vehicle>(spec, ground, &loaded->geometry);
  rig.ground = ground;
  {
    // largada da pista (na pista de teste: origem)
    vec3 sp = world->spawnPos();
    car->reset(phys::V3(sp.x, sp.y, sp.z), world->spawnHeading());
  }
  visual->position = vec3(0, (float)-spec.cgHeight, (float)(loaded->geometry.wheelbase / 2 - car->a));
  visual->add(loaded->root);
  loaded->root->position = vec3();
  loaded->root->quaternion = quat();
  if (loaded->cockpit) rig.cockpitEye = loaded->cockpit->eye;
  if (loaded->hoodEye) rig.hoodEye = *loaded->hoodEye;
  findHelmet();
  skids->clear();
  smoke->clear();
  syncVisuals(0);
  rig.mode = (int)rig.modes.size() - 1;
  rig.next();  // volta para a câmera de perseguição
  rig.pos = vec3(0, 1.65f, 5.6f);
  rig.yaw = 0;
  fx.toneMapping = PostFX::ToneMapping::ACES;
  // pôr do sol: um pouco mais de exposição e gradação mais quente
  bool sunset = world->timeOfDay == 1;
  fx.exposure = sunset ? 1.2f : 0.95f;
  fx.saturation = sunset ? 1.14f : 1.08f;
  audio.start();
  audio.setPaused(false);
  if (settings) {
    autoQ.enabled = settings->autoQuality && autoQ.drops < 2;
    audio.setVolume(settings->volume);
  }
  running = true;
  paused = false;
  fx.resize(winW, winH, dpr);
  input.clearPressed();
  const auto& a = loaded->attribution;
  hud->creditTitle = a.title;
  hud->creditAuthor = a.author;
  hud->creditLicense = a.license;
  hud->creditSource = a.source;
}

void Game::teleport(float x, float z, float heading) {
  phys::GroundQuery h = ground->heightAt(x, z);
  car->reset(phys::V3(x, h.h > -1e8 ? h.h : 0, z), heading);
  rig.yaw = heading;
  skids->clear();
  smoke->clear();
}

void Game::debugPick(float x, float z) {
  TrackMap* t = world->currentTrack();
  if (!t) return;
  t->root->updateMatrixWorld();
  for (auto& h : raycast(*t->root, vec3(x, 3000, z), vec3(0, -1, 0), 0, 6000)) {
    const Material& m = *h.object->material;
    std::printf("[pick] (%.0f, %.0f) y=%.2f %-24s transp=%d decal=%d alphaTest=%.1f cor=(%.2f %.2f %.2f) mapa=%dx%d\n", x, z, h.point.y,
                m.name.c_str(), m.transparent, m.decal, m.alphaTest, m.color.x, m.color.y, m.color.z,
                m.map ? m.map.tex->width : 0, m.map ? m.map.tex->height : 0);
  }
}

void Game::stop() {
  running = false;
  audio.setPaused(true);
  if (hud) hud->helpOpen = false;
  rig.orbit.enabled = false;
  for (Node* m : rig.hideInCockpit) m->visible = true;
  visual->remove(loaded->root.get());
}

// capacete/cabeça do piloto (malhas perto do olho do cockpit) somem na câmera de cockpit
void Game::findHelmet() {
  rig.hideInCockpit.clear();
  if (!loaded->cockpit) return;
  vec3 eye = loaded->cockpit->helmet;
  carObj->position = vec3();
  carObj->quaternion = quat();
  carObj->updateMatrixWorld();
  mat4 inv = visual->matrixWorld.inverse();
  loaded->root->traverse([&](Node& o) {
    if (!o.isMesh()) return;
    Box3 b = worldBox(o).transformed(inv);
    vec3 c = b.center(), s = b.size();
    if (distance(c, eye) < 0.15f && std::max({s.x, s.y, s.z}) < 0.45f) rig.hideInCockpit.push_back(&o);
  });
}

void Game::resize(int w, int h, float d) {
  winW = w;
  winH = h;
  dpr = d;
  camera.aspect = (float)w / std::max(1, h);
  fx.resize(w, h, d);
}

void Game::applySettings() {
  if (!settings) return;
  fx.setQuality(settings->quality(), settings->preset);
  fx.lensFx = settings->lensFx;
  fx.speedBlur = settings->speedBlur;
  fx.resize(winW, winH, dpr);
  if (world) {
    world->setShadowQuality(fx.quality().shadow);
    world->setShadowDistance((float)settings->shadowDist);
    world->setShadowSoftness(settings->shadowSoft);
  }
  audio.setVolume(settings->volume);
}

void Game::setQuality(int level, bool autoAdj) {
  level = ((level % 4) + 4) % 4;
  if (!autoAdj && settings) {
    // tecla G: muda o nível salvo nas configurações
    settings->applyPreset(level);
    settings->save();
    fx.setQuality(settings->quality(), level);
  } else {
    fx.setLevel(level);
  }
  world->setShadowQuality(fx.quality().shadow);
  fx.resize(winW, winH, dpr);
  hud->flash(std::string("Gráficos: ") + fx.quality().name + (autoAdj ? " (ajuste automático)" : ""), 2);
}

void Game::resetCar() {
  vec3 f = carObj->quaternion.rotate({0, 0, -1});
  phys::GroundQuery h = ground->heightNear(car->pos.x, car->pos.z, car->pos.y + 3);
  car->reset(phys::V3(car->pos.x, h.h > -1e8 ? h.h : 0, car->pos.z), std::atan2(-f.x, -f.z));
  skids->clear();
  smoke->clear();
}

void Game::handleKeys(const Input::Events& ev) {
  if (ev.shiftUp) car->shiftUp();
  if (ev.shiftDown) car->shiftDown();
  if (input.take({SDL_SCANCODE_ESCAPE})) {
    if (onExit) onExit();
    return;
  }
  if (input.take({SDL_SCANCODE_C})) rig.next();
  if (input.take({SDL_SCANCODE_R})) { resetCar(); hud->flash("Carro reposicionado"); }
  if (input.take({SDL_SCANCODE_M})) {
    car->assists.autoGearbox = !car->assists.autoGearbox;
    hud->flash(std::string("Câmbio ") + (car->assists.autoGearbox ? "automático" : "manual"));
  }
  if (input.take({SDL_SCANCODE_B})) {
    car->assists.abs = !car->assists.abs;
    hud->flash(std::string("ABS ") + (car->assists.abs ? "ligado" : "desligado"));
  }
  if (input.take({SDL_SCANCODE_T})) {
    car->assists.tc = !car->assists.tc;
    hud->flash(std::string("Controle de tração ") + (car->assists.tc ? "ligado" : "desligado"));
  }
  if (input.take({SDL_SCANCODE_G})) { autoQ.enabled = false; setQuality(fx.level < 0 ? 0 : fx.level + 1); }
  if (input.take({SDL_SCANCODE_H})) hud->helpOpen = !hud->helpOpen;
  if (input.take({SDL_SCANCODE_N})) audio.toggleMute();
  if (input.take({SDL_SCANCODE_P})) { paused = !paused; hud->flash(paused ? "Pausado" : "Continuando"); }
}

// qualidade automática: se o fps ficar baixo, reduz um nível (até 2 vezes)
void Game::autoQuality(float dt) {
  if (!autoQ.enabled) return;
  autoQ.t += dt;
  autoQ.frames++;
  if (autoQ.t < 3) return;
  float fps = autoQ.frames / autoQ.t;
  autoQ.t = 0;
  autoQ.frames = 0;
  if (fps < 42 && fx.level != 0 && autoQ.drops < 2) {
    autoQ.drops++;
    setQuality(fx.level < 0 ? 1 : fx.level - 1, true);
  } else {
    autoQ.enabled = fps < 42 && autoQ.drops < 2;
  }
}

void Game::syncVisuals(float dt) {
  // a física anda em passos de 1 ms e sobra um pedaço (acc) até o próximo passo: o carro é
  // desenhado adiantado por essa sobra. Sem isso a rotação pula de quadro em quadro e a câmera
  // presa ao carro sacode o céu e o chão nas curvas
  float ahead = paused ? 0.f : (float)acc;
  carObj->position = vec3((float)(car->pos.x + car->vel.x * ahead), (float)(car->pos.y + car->vel.y * ahead),
                          (float)(car->pos.z + car->vel.z * ahead));
  carObj->quaternion = quat((float)car->quat.x, (float)car->quat.y, (float)car->quat.z, (float)car->quat.w);
  {
    vec3 w((float)car->angVel.x, (float)car->angVel.y, (float)car->angVel.z);
    float wl = length(w);
    if (wl > 1e-5f && ahead > 0) carObj->quaternion = (quat::fromAxisAngle(w * (1 / wl), wl * ahead) * carObj->quaternion).normalized();
  }
  for (const auto& w : car->wheels) {
    auto it = loaded->pivots.find(w.id);
    if (it == loaded->pivots.end()) continue;
    WheelPivot& pv = it->second;
    pv.steer->position.y = pv.base.y - (float)(w.x0 - w.compression);
    pv.steer->setRotationY((float)w.steer);
    pv.spin->setRotationX((float)-w.spin);
    // disco incandescente a partir de ~450 °C
    float glow = std::min(1.f, std::max(0.f, (float)(w.brakeTemp - 450) / 450));
    for (auto& m : pv.glow) {
      if (m->glowQuad) m->opacity = glow;
      m->emissiveIntensity = glow * 4;
    }
    // roda borrada aparece acima de ~25 rad/s (≈ 30 km/h)
    float blur = std::min(0.85f, std::max(0.f, (float)(std::fabs(w.omega) - 25) / 45));
    for (auto& m : pv.blur) m->opacity = blur;
  }
  if (loaded->steeringWheel) {
    auto& sw = *loaded->steeringWheel;
    sw.pivot->quaternion = quat::fromAxisAngle(sw.axis, -input.steer * 3.8f);
  }
  brakeLight += ((input.brake > 0.05f ? 1.f : 0.f) - brakeLight) * std::min(1.f, dt * 25);
  for (auto& m : loaded->tailLights) m->emissiveIntensity = 0.8f + brakeLight * 14;

  carObj->updateMatrixWorld();
  vec3 right = carObj->quaternion.rotate({1, 0, 0});
  double spd = car->speed();
  vec3 vel((float)car->vel.x, (float)car->vel.y, (float)car->vel.z);
  for (const auto& w : car->wheels) {
    vec3 cp((float)w.contactPoint.x, (float)w.contactPoint.y, (float)w.contactPoint.z);
    float k = w.contact && spd > 1.5 ? std::min(1.f, std::max(0.f, (float)(w.slip - 0.95) / 0.9f)) * std::min(1.f, (float)w.fz / 3500) : 0;
    skids->add(w.id, cp, right, k);
    double slipSpeed = std::fabs(w.omega * w.radius - car->forwardSpeed()) + std::fabs(std::sin(w.slipAngle)) * spd;
    float s = w.contact ? std::min(1.f, std::max(0.f, (float)(w.slip - 1.2) / 1.2f)) * std::min(1.f, std::max(0.f, (float)(slipSpeed - 3) / 10)) : 0;
    smoke->emit(w.id, cp, vel, s, dt);
  }
}

void Game::frame(float dt) {
  Input::Events ev = input.update(dt, (float)car->speed() * 3.6f);
  handleKeys(ev);
  if (!running) {
    input.endFrame();
    return;
  }
  if (!paused) {
    acc += dt;
    phys::Controls controls{input.throttle, input.brake, input.steer, input.handbrake};
    while (acc >= PHYSICS_DT) {
      car->step(PHYSICS_DT, controls);
      acc -= PHYSICS_DT;
    }
  }
  syncVisuals(dt);
  rig.update(dt, *carObj, *visual, *car);
  camera.aspect = (float)winW / std::max(1, winH);
  camera.update();
  {
    // o quadrado da sombra fica um pouco à frente do carro (é para lá que a câmera olha)
    vec3 f = carObj->quaternion.rotate({0, 0, -1});
    f.y = 0;
    float fl = length(f);
    vec3 center = carObj->position;
    if (fl > 0.01f && settings) center += f * (1 / fl) * (settings->shadowDist * 0.25f);
    world->follow(center);
  }
  smoke->update(paused ? 0 : dt, camera.fov, (float)fx.height);
  hud->update(*car, input, rig.name(), dt);
  audio.update(*car);

  world->scene.root->updateMatrixWorld();
  static bool prof = std::getenv("SR_PROFILE") != nullptr;
  auto T = [] { return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now().time_since_epoch()).count(); };
  double t0 = T();
  if (prof) glFinish();
  double t1 = T();
  renderer.renderShadows(world->scene);
  if (prof) glFinish();
  double t2 = T();
  fx.beginScene();
  if (TrackMap* tm = world->currentTrack()) tm->updateLod(camera.position);
  renderer.render(world->scene, camera, fx.width, fx.height);
  double t3 = T();
  if (prof) glFinish();
  double t4 = T();
  fx.endScene(dt, std::min(1.f, std::max(0.f, ((float)car->speed() * 3.6f - 140) / 160)), camera);
  if (prof) {
    glFinish();
    double t5 = T();
    static double acc[4] = {}, n = 0;
    acc[0] += t2 - t1; acc[1] += t3 - t2; acc[2] += t4 - t3; acc[3] += t5 - t4; n++;
    if (n >= 60) {
      std::printf("[perf] sombras %.1f ms · cena CPU %.1f ms + GPU %.1f ms · pós %.1f ms\n", acc[0] / n, acc[1] / n, acc[2] / n, acc[3] / n);
      acc[0] = acc[1] = acc[2] = acc[3] = n = 0;
    }
  }
  (void)t0;
  autoQuality(dt);
  input.endFrame();
}
