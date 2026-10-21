// Parâmetros físicos dos carros (unidades SI). Ver carspec.cpp.
#pragma once
#include <string>
#include <utility>
#include <vector>

namespace phys {

struct Geometry {
  double wheelbase = 0, trackFront = 0, trackRear = 0, radiusFront = 0, radiusRear = 0;
};

struct Suspension {
  double k, bump, rebound, arb, travel;
};

struct TireParams {
  double mu, fz0, loadSensitivity, slipRatioPeak, slipAnglePeakDeg, C, E, longScale;
  double rollingResistance, inertiaFront, inertiaRear;
};

struct Brakes {
  double maxTorqueFront, maxTorqueRear, handbrakeTorque, heatCapacity, ambientTemp, coolingBase, coolingPerMs;
};

struct Engine {
  int cylinders;
  double idleRpm, limiterRpm, inertia;
  std::vector<std::pair<double, double>> torqueCurve;
  double frictionA, frictionB, turboLag, turboShare, boostRpmStart, boostRpmFull, clutchFullRpm;
};

struct Gearbox {
  std::vector<double> ratios;
  double reverse, finalDrive, efficiency, shiftTime, clutchMaxTorque;
};

struct Differential {
  double preload, powerLock, coastLock, viscous;
};

struct Aero {
  double rho, CdA, ClA, frontBalance;
};

struct Steering {
  double maxAngleDeg, ackermann;
};

struct Assists {
  bool abs = true, tc = true, autoGearbox = true;
};

// sistema híbrido (opcional)
struct ElectricRear {
  bool present = false;
  double power = 0, maxTorque = 0;
};
struct ElectricFront {
  bool present = false;
  double powerPerWheel = 0, maxWheelTorque = 0, cutoffKmh = 0;
};

struct CarSpec {
  std::string name;
  double mass;
  struct { double pitch, yaw, roll; } inertia;
  double cgHeight, frontWeight;
  bool frontDrive = false;  // tração dianteira (motor → rodas da frente); padrão: traseira
  Geometry geometry;
  struct { Suspension front, rear; double bumpStopK; } suspension;
  TireParams tire;
  Brakes brakes;
  Engine engine;
  ElectricRear electricRear;
  ElectricFront electricFront;
  Gearbox gearbox;
  Differential differential;
  Aero aero;
  Steering steering;
  Assists assists;
};

extern CarSpec FERRARI_296_GT3;
extern CarSpec PORSCHE_992_GT3_R;
extern CarSpec LAMBORGHINI_FENOMENO;
extern CarSpec BMW_M4_GT3_EVO;
extern CarSpec HONDA_CIVIC_TYPE_R;

}  // namespace phys
