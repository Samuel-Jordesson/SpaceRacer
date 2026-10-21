// Simulação do veículo: corpo rígido 6DOF + 4 rodas com suspensão por raycast,
// pneus Pacejka, motor turbo, embreagem, câmbio sequencial, diferencial autoblocante,
// aerodinâmica, ABS e controle de tração.
//
// Referencial do carro (corpo): x = direita, y = cima, z = trás (frente = −z). Origem no CG.
#pragma once
#include <array>
#include <optional>
#include <string>
#include <vector>
#include "carspec.h"
#include "tire.h"
#include "vmath.h"

namespace phys {

struct RayHit {
  double dist, nx, ny, nz, mu;
  double drag = 0;  // resistência extra de rolagem da superfície (brita, grama alta)
};
struct GroundQuery {
  double h, nx, ny, nz;
};

// Interface do chão usada pela física. Para uma pista, implemente raycast() e heightAt().
class Ground {
 public:
  virtual ~Ground() = default;
  virtual std::optional<RayHit> raycast(const V3& o, const V3& d, double maxDist) const = 0;
  virtual GroundQuery heightAt(double x, double z) const = 0;
  // altura do chão logo abaixo de (x, y, z) — importa em pistas com desníveis/pontes
  virtual GroundQuery heightNear(double x, double z, double y) const {
    (void)y;
    return heightAt(x, z);
  }
  // muros e barreiras: ponto p (com raio r) encostando numa parede → normal apontando para fora e penetração
  virtual bool wallContact(const V3& p, double r, V3& normal, double& depth) const {
    (void)p; (void)r; (void)normal; (void)depth;
    return false;
  }
};

// Chão plano infinito (y = h).
class FlatGround : public Ground {
 public:
  explicit FlatGround(double height = 0, double mu = 1) : h(height), mu(mu) {}
  std::optional<RayHit> raycast(const V3& o, const V3& d, double maxDist) const override {
    if (d.y > -1e-6) return std::nullopt;
    double t = (o.y - h) / -d.y;
    if (t < 0 || t > maxDist) return std::nullopt;
    return RayHit{t, 0, 1, 0, mu};
  }
  GroundQuery heightAt(double, double) const override { return {h, 0, 1, 0}; }
  double h, mu;
};

struct Controls {
  double throttle = 0, brake = 0, steer = 0, handbrake = 0;  // steer −1..1 (direita +)
};

struct Wheel {
  std::string id;
  bool front, left, driven, motor;
  V3 hardpoint;
  double radius;
  Suspension susp;
  double x0;  // compressão estática
  double inertia, maxBrake;
  // estado
  double omega = 0, spin = 0, steer = 0;
  double compression = 0;
  bool contact = false;
  double fz = 0, fsusp = 0;
  double slipRatio = 0, slipAngle = 0, slip = 0, fx = 0, fy = 0, dfx = 0;
  V3 contactPoint, contactNormal{0, 1, 0}, origin, center;
  double surfaceMu = 1, surfaceDrag = 0, absFactor = 1, denomX = 1;
  double Tbrake = 0, brakeTemp = 0, T0 = 0, kT = 0, Tfric = 0;
};

struct EngineState {
  double omega = 0, rpm = 0, torque = 0, throttle = 0, boost = 0;
  bool limiter = false;
};

struct Shift {
  int target;
  double t;
  bool up;
  double duration = 0;  // tempo da troca (engatar a partir do neutro é quase instantâneo)
};

class Vehicle {
 public:
  Vehicle(const CarSpec& spec, const Ground* ground, const Geometry* geometry = nullptr);

  void reset(const V3& position = V3(), double heading = 0);
  double ratio(int gear) const;
  double ratio() const { return ratio(gear); }
  double speed() const { return vel.length(); }
  double forwardSpeed() const { return vel.dot(quat.toMat3().mulV(V3(0, 0, -1))); }
  double drivenOmega() const;
  int drivenLeft() const;
  double shiftDuration(int from, int to) const;  // índice da roda motriz esquerda (0 = dianteira, 2 = traseira)
  bool shiftUp();
  bool shiftDown();
  void step(double dt, const Controls& input);

  CarSpec spec;
  const Ground* ground;
  TireModel tire;
  Assists assists;
  Geometry geometry;
  double mass;
  V3 inertia;
  double a, b;  // CG → eixo dianteiro / traseiro
  std::array<Wheel, 4> wheels;
  std::vector<V3> hull;

  V3 pos, vel, angVel, accel;
  Quat quat;
  EngineState engine;
  int gear = 0;  // −1 = R, 0 = N, 1..n
  std::optional<Shift> shift;
  double clutch = 0;
  bool clutchLocked = false;
  double clutchTorque = 0;
  double tcCut = 1, tcFront = 1, frontMotorTorque = 0;
  bool tcActive = false, absActive = false;
  double steerAngle = 0, time = 0, overrevBlocked = 0;

 private:
  void drivetrain(double dt, const Controls& input, double brake, double handbrake);
  TireOut tireOut;
};

}  // namespace phys
