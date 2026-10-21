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

// BMW M4 GT3 EVO (2025). Dados oficiais da BMW M Motorsport: P58 seis em linha 3.0 biturbo
// (2.993 cm³) com até 590 cv conforme o BoP, câmbio sequencial Xtrac de 6 marchas, embreagem
// eletro-hidráulica, entre-eixos 2.917 mm (o mais longo entre os GT3), 5.020 × 2.040 mm,
// freios de 390 mm/6 pistões na frente e 380 mm/4 pistões atrás (discos traseiros maiores no EVO),
// rodas 12,5 × 18" e 13 × 18". Torque e peso não são divulgados: valores típicos de GT3.
CarSpec BMW_M4_GT3_EVO = [] {
  CarSpec s;
  s.name = "BMW M4 GT3 EVO";
  s.mass = 1340;             // ~1.270 kg (BoP) + piloto + combustível
  s.inertia = {2350, 2550, 540};  // carro longo: mais inércia de guinada (mais estável, gira menos)
  s.cgHeight = 0.40;
  s.frontWeight = 0.48;      // motor dianteiro recuado (front-mid), tração traseira
  s.geometry = {2.917, 1.70, 1.68, 0.345, 0.355};
  // dianteira mais carregada que nos GT3 de motor central/traseiro
  s.suspension.front = {165000, 7000, 10000, 58000, 0.055};
  s.suspension.rear = {160000, 7000, 10500, 38000, 0.060};
  s.suspension.bumpStopK = 1.2e6;
  s.tire = {1.62, 3800, -0.11, 0.085, 6.5, 1.40, 0.10, 1.05, 0.012, 1.15, 1.35};  // slick de GT3
  s.brakes = {3350, 2100, 2600, 1900, 25, 0.01, 0.0012};
  s.engine.cylinders = 6;
  s.engine.idleRpm = 1200;
  s.engine.limiterRpm = 7500;
  s.engine.inertia = 0.22;
  // P58 de corrida: torque cheio do meio para cima, ~590 cv perto de 6.250 rpm
  s.engine.torqueCurve = {{800, 220}, {1500, 380}, {2000, 480}, {2500, 580}, {3000, 650}, {3500, 690},
                          {4500, 700}, {5500, 690}, {6250, 665}, {7000, 590}, {7500, 540}, {8000, 400}};
  s.engine.frictionA = 20;
  s.engine.frictionB = 0.0090;
  s.engine.turboLag = 0.30;
  s.engine.turboShare = 0.45;
  s.engine.boostRpmStart = 1800;
  s.engine.boostRpmFull = 3200;
  s.engine.clutchFullRpm = 7000;
  s.gearbox = {{2.90, 2.18, 1.76, 1.46, 1.25, 1.10}, -2.90, 3.15, 0.92, 0.07, 1000};
  s.differential = {90, 0.35, 0.25, 350};
  s.aero = {1.225, 1.12, 2.50, 0.42};  // asa traseira grande; EVO com ajustes de difusor e asa
  s.steering = {22, 0.45};
  return s;
}();

// Honda Civic Type R (FL5, 2023–2024), carro de rua. Dados da Honda: K20C1 2.0 turbo de 4 cilindros
// com 235 kW (329 cv) a 6.500 rpm e 420 N·m de 2.200 a 4.000 rpm, câmbio manual de 6 marchas
// (relações do FK8, diferencial final 3,842), tração dianteira com autoblocante helicoidal,
// ~1.430 kg, entre-eixos 2.735 mm, freios Brembo de 4 pistões com discos de 350 mm,
// pneus 265/30 R19, 275 km/h. Motor, câmbio e peso de série; pneus do modelo customizado (ver abaixo).
CarSpec HONDA_CIVIC_TYPE_R = [] {
  CarSpec s;
  s.name = "Honda Civic Type R (FL5)";
  s.mass = 1505;             // 1.430 kg + piloto + combustível
  s.inertia = {2150, 2350, 620};
  s.cgHeight = 0.52;         // hatch de rua: CG bem mais alto que um GT3
  s.frontWeight = 0.61;      // motor transversal na frente
  s.frontDrive = true;
  s.geometry = {2.735, 1.625, 1.615, 0.321, 0.321};
  // suspensão de rua esportiva (amortecedores adaptativos): macia perto de um carro de corrida
  s.suspension.front = {52000, 3600, 5200, 26000, 0.09};
  s.suspension.rear = {46000, 3300, 4800, 20000, 0.09};
  s.suspension.bumpStopK = 8.0e5;
  // o modelo do jogo é um FL5 customizado com Toyo Proxes R888 (semi-slick de pista homologado):
  // bem mais aderência que o Pilot Sport 4S de série; pico longitudinal ~8% acima do lateral
  s.tire = {1.26, 4000, -0.11, 0.10, 7.0, 1.45, 0.0, 1.08, 0.013, 1.05, 1.0};
  s.brakes = {2500, 1000, 1800, 1500, 25, 0.01, 0.0012};
  s.engine.cylinders = 4;
  s.engine.idleRpm = 800;
  s.engine.limiterRpm = 7000;
  s.engine.inertia = 0.15;   // volante leve
  // torque plano de 2.200 a 4.000 rpm, 329 cv a 6.500 rpm
  s.engine.torqueCurve = {{800, 170}, {1500, 280}, {2000, 380}, {2200, 420}, {4000, 420}, {5000, 400},
                          {6000, 372}, {6500, 345}, {7000, 305}, {7400, 220}};
  s.engine.frictionA = 12;
  s.engine.frictionB = 0.0060;
  s.engine.turboLag = 0.45;
  s.engine.turboShare = 0.50;
  s.engine.boostRpmStart = 1400;
  s.engine.boostRpmFull = 2200;
  s.engine.clutchFullRpm = 7000;  // largada a ~3.500 rpm, com o turbo já enchendo
  // manual de 6 marchas com rev-matching (o "blip" automático já existe no câmbio)
  s.gearbox = {{3.625, 2.115, 1.529, 1.125, 0.911, 0.734}, -3.27, 3.842, 0.93, 0.22, 600};
  s.differential = {30, 0.45, 0.10, 150};  // helicoidal (Torsen): trava sob tração, solto no alívio
  s.aero = {1.225, 0.72, 0.25, 0.45};      // asa de rua: pouca pressão aerodinâmica
  s.steering = {34, 0.8};
  return s;
}();

}  // namespace phys
