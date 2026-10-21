#include "carspec.h"

namespace phys {

// Ferrari 296 GT3 (o modelo 3D em ferrari-488-gt3/ é o 296 GT3, segundo os metadados do GLB).
// Valores baseados em dados públicos do carro (V6 120° 2.992 cc biturbo, ~600 cv em BoP,
// câmbio sequencial Xtrac de 6 marchas, entre-eixos 2.660 mm) e em faixas típicas de GT3.
CarSpec FERRARI_296_GT3 = [] {
  CarSpec s;
  s.name = "Ferrari 296 GT3";
  s.mass = 1310;  // carro em peso mínimo + piloto + combustível
  s.inertia = {2050, 2250, 520};
  s.cgHeight = 0.40;
  s.frontWeight = 0.45;  // motor central-traseiro
  // Geometria padrão (substituída pelas medidas reais do modelo 3D ao carregar).
  s.geometry = {2.66, 1.70, 1.66, 0.345, 0.355};
  // k = taxa na roda (N/m), amortecedor bump/rebound (N·s/m), barra estabilizadora (N/m)
  s.suspension.front = {150000, 6500, 9500, 55000, 0.055};
  s.suspension.rear = {165000, 7000, 10500, 40000, 0.060};
  s.suspension.bumpStopK = 1.2e6;
  // Pirelli P Zero DHF (slick). Pacejka "Magic Formula" com escorregamento combinado.
  s.tire = {1.62, 3800, -0.11, 0.085, 6.5, 1.40, 0.10, 1.05, 0.012, 1.10, 1.35};
  s.brakes = {3300, 1900, 2600, 1800, 25, 0.01, 0.0012};
  s.engine.cylinders = 6;
  s.engine.idleRpm = 1200;
  s.engine.limiterRpm = 8000;
  s.engine.inertia = 0.20;
  // Curva de torque em plena carga (N·m): ~610 cv a 7250 rpm
  s.engine.torqueCurve = {{800, 200}, {1500, 330}, {2000, 420}, {2500, 520}, {3000, 600}, {3500, 655},
                          {4500, 680}, {5500, 680}, {6500, 650}, {7250, 600}, {8000, 520}, {8500, 380}};
  s.engine.frictionA = 18;
  s.engine.frictionB = 0.0085;
  s.engine.turboLag = 0.30;
  s.engine.turboShare = 0.45;
  s.engine.boostRpmStart = 1800;
  s.engine.boostRpmFull = 3400;
  s.engine.clutchFullRpm = 7000;
  s.gearbox = {{2.91, 2.14, 1.72, 1.43, 1.24, 1.10}, -2.90, 3.35, 0.92, 0.07, 950};
  s.differential = {90, 0.35, 0.25, 350};  // autoblocante mecânico (rampa) de GT3
  s.aero = {1.225, 1.10, 2.45, 0.42};       // ~470 kg de downforce a 200 km/h
  s.steering = {22, 0.45};
  return s;
}();

// Porsche 911 GT3 R (992), temporada 2023/2024: boxer 6 aspirado 4.194 cc, 565 cv a 9.400 rpm,
// sequencial de 6 marchas, tração traseira com motor traseiro, entre-eixos 2.507 mm.
CarSpec PORSCHE_992_GT3_R = [] {
  CarSpec s;
  s.name = "Porsche 911 GT3 R (992)";
  s.mass = 1330;
  s.inertia = {2000, 2150, 500};
  s.cgHeight = 0.40;
  s.frontWeight = 0.43;  // motor atrás do eixo traseiro
  s.geometry = {2.507, 1.68, 1.64, 0.34, 0.355};
  s.suspension.front = {140000, 6000, 9000, 52000, 0.055};
  s.suspension.rear = {178000, 7500, 11500, 36000, 0.060};
  s.suspension.bumpStopK = 1.2e6;
  s.tire = {1.62, 3800, -0.11, 0.085, 6.5, 1.40, 0.10, 1.05, 0.012, 1.10, 1.35};  // Michelin S9M
  s.brakes = {3200, 2250, 2600, 1800, 25, 0.01, 0.0012};
  s.engine.cylinders = 6;
  s.engine.idleRpm = 1500;
  s.engine.limiterRpm = 9500;
  s.engine.inertia = 0.16;  // aspirado de giro alto: volante leve
  s.engine.torqueCurve = {{800, 180}, {1500, 250}, {2500, 320}, {3500, 375}, {4500, 410}, {5500, 435},
                          {6500, 455}, {7500, 462}, {8500, 452}, {9000, 440}, {9400, 425}, {9800, 330}};
  s.engine.frictionA = 20;
  s.engine.frictionB = 0.0080;
  s.engine.turboLag = 0.05;
  s.engine.turboShare = 0;  // aspirado
  s.engine.boostRpmStart = 1000;
  s.engine.boostRpmFull = 1500;
  s.engine.clutchFullRpm = 10500;  // largada em ~6.000 rpm
  s.gearbox = {{3.12, 2.31, 1.87, 1.59, 1.42, 1.31}, -3.00, 3.40, 0.92, 0.06, 800};
  s.differential = {100, 0.40, 0.30, 350};
  s.aero = {1.225, 1.06, 2.55, 0.38};
  s.steering = {22, 0.45};
  return s;
}();

// Lamborghini Fenomeno (2026): V12 6.5 aspirado (835 cv) + três motores elétricos (1.080 cv),
// dupla embreagem de 8 marchas, tração integral, entre-eixos 2.779 mm. Bateria sempre disponível.
CarSpec LAMBORGHINI_FENOMENO = [] {
  CarSpec s;
  s.name = "Lamborghini Fenomeno";
  s.mass = 1830;
  s.inertia = {3000, 3300, 700};
  s.cgHeight = 0.42;
  s.frontWeight = 0.44;
  s.geometry = {2.779, 1.72, 1.70, 0.345, 0.356};
  // carro de rua com amortecedores magnetorreológicos: bem mais macio que um GT3
  s.suspension.front = {90000, 5200, 7800, 40000, 0.08};
  s.suspension.rear = {110000, 6200, 9300, 28000, 0.08};
  s.suspension.bumpStopK = 1.0e6;
  s.tire = {1.45, 4500, -0.10, 0.10, 7.5, 1.45, 0.05, 1.05, 0.012, 1.30, 1.70};  // Potenza Race
  s.brakes = {4200, 2800, 2800, 2600, 25, 0.01, 0.0012};
  s.engine.cylinders = 12;
  s.engine.idleRpm = 1000;
  s.engine.limiterRpm = 9500;
  s.engine.inertia = 0.25;
  s.engine.torqueCurve = {{800, 280}, {1500, 360}, {2500, 460}, {3500, 550}, {4500, 620}, {5500, 680},
                          {6750, 725}, {7500, 715}, {8500, 680}, {9250, 634}, {9700, 520}};
  s.engine.frictionA = 30;
  s.engine.frictionB = 0.011;
  s.engine.turboLag = 0.05;
  s.engine.turboShare = 0;
  s.engine.boostRpmStart = 1000;
  s.engine.boostRpmFull = 1500;
  s.engine.clutchFullRpm = 8000;
  // sistema híbrido: 835 cv do V12 + elétricos = 1.080 cv combinados
  s.electricRear = {true, 30000, 150};          // no câmbio, ajuda nas baixas
  s.electricFront = {true, 38000, 1800, 350};   // e-eixo dianteiro
  s.gearbox = {{3.75, 2.70, 2.06, 1.65, 1.38, 1.20, 1.10, 1.053}, -3.50, 3.40, 0.92, 0.05, 1400};
  s.differential = {80, 0.30, 0.20, 300};
  s.aero = {1.225, 0.78, 1.10, 0.42};  // aerodinâmica ativa, bem menos downforce que um GT3
  s.steering = {28, 0.6};
  return s;
}();

}  // namespace phys
