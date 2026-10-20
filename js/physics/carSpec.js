// Ferrari 296 GT3 (o modelo 3D em ferrari-488-gt3/ é o 296 GT3, segundo os metadados do GLB).
// Valores baseados em dados públicos do carro (V6 120° 2.992 cc biturbo, ~600 cv em BoP,
// câmbio sequencial Xtrac de 6 marchas, entre-eixos 2.660 mm) e em faixas típicas de GT3.
// Unidades SI: kg, m, N, N·m, s, rad.

export const FERRARI_296_GT3 = {
  name: 'Ferrari 296 GT3',

  mass: 1310,              // carro em peso mínimo + piloto + combustível
  inertia: { pitch: 2050, yaw: 2250, roll: 520 },
  cgHeight: 0.40,          // altura do CG em relação ao chão
  frontWeight: 0.45,       // motor central-traseiro

  // Geometria padrão (substituída pelas medidas reais do modelo 3D ao carregar).
  geometry: {
    wheelbase: 2.66,
    trackFront: 1.70,
    trackRear: 1.66,
    radiusFront: 0.345,
    radiusRear: 0.355,
  },

  suspension: {
    // k = taxa na roda (N/m), amortecedor bump/rebound (N·s/m), barra estabilizadora (N/m)
    front: { k: 150000, bump: 6500, rebound: 9500, arb: 55000, travel: 0.055 },
    rear: { k: 165000, bump: 7000, rebound: 10500, arb: 40000, travel: 0.060 },
    bumpStopK: 1.2e6,
  },

  tire: {
    // Pirelli P Zero DHF (slick). Modelo Pacejka "Magic Formula" com escorregamento combinado.
    mu: 1.62,                 // atrito de pico na carga nominal
    fz0: 3800,                // carga nominal (N)
    loadSensitivity: -0.11,   // perda de aderência com carga (sensibilidade à carga)
    slipRatioPeak: 0.085,
    slipAnglePeakDeg: 6.5,
    C: 1.40,                  // fator de forma: aderência em deslizamento ≈ 81% do pico
    E: 0.10,                  // curvatura
    longScale: 1.05,          // pico longitudinal ligeiramente maior que o lateral
    rollingResistance: 0.012,
    inertiaFront: 1.10,       // roda + pneu + disco (kg·m²)
    inertiaRear: 1.35,
  },

  brakes: {
    maxTorqueFront: 3300,     // por roda (N·m)
    maxTorqueRear: 1900,
    handbrakeTorque: 2600,    // traseiras
    heatCapacity: 1800,       // J/K efetivo da superfície do disco
    ambientTemp: 25,          // °C
    coolingBase: 0.01,        // 1/s
    coolingPerMs: 0.0012,     // 1/s por m/s de velocidade (dutos de freio)
  },

  engine: {
    idleRpm: 1200,
    limiterRpm: 8000,
    inertia: 0.20,
    // Curva de torque em plena carga (N·m) — ~610 cv a 7250 rpm
    torqueCurve: [
      [800, 200], [1500, 330], [2000, 420], [2500, 520], [3000, 600], [3500, 655],
      [4500, 680], [5500, 680], [6500, 650], [7250, 600], [8000, 520], [8500, 380],
    ],
    // Atrito/freio-motor (N·m) = a + b·rpm
    frictionA: 18,
    frictionB: 0.0085,
    turboLag: 0.30,           // constante de tempo do turbo (s)
    turboShare: 0.45,         // fração do torque que depende de pressão de turbo
    boostRpmStart: 1800,
    boostRpmFull: 3400,
    clutchFullRpm: 7000,      // embreagem automática: rotação de acoplamento total com pé no fundo
  },

  gearbox: {
    ratios: [2.91, 2.14, 1.72, 1.43, 1.24, 1.10],
    reverse: -2.90,
    finalDrive: 3.35,
    efficiency: 0.92,
    shiftTime: 0.07,          // sequencial com corte de ignição
    clutchMaxTorque: 950,
  },

  differential: {
    // Autoblocante mecânico (rampa) de GT3
    preload: 90,
    powerLock: 0.35,
    coastLock: 0.25,
    viscous: 350,
  },

  aero: {
    rho: 1.225,
    CdA: 1.10,
    ClA: 2.45,                // ~470 kg de downforce a 200 km/h
    frontBalance: 0.42,
  },

  steering: {
    maxAngleDeg: 22,
    ackermann: 0.45,
  },

  assists: {
    abs: true,
    tc: true,
    autoGearbox: true,
  },
};

// Porsche 911 GT3 R (992), temporada 2023/2024.
// Dados públicos da Porsche Motorsport: boxer de 6 cilindros aspirado 4.194 cc (M97/80) com até
// 416 kW (565 cv) a 9.400 rpm, câmbio sequencial de 6 marchas, tração traseira com motor traseiro,
// entre-eixos 2.507 mm, freios de aço de 390 mm (dianteiros, 6 pistões) e 370 mm (traseiros, 4 pistões),
// ABS e controle de tração Bosch, pneus Michelin 30/68-18 e 31/71-18.
export const PORSCHE_992_GT3_R = {
  name: 'Porsche 911 GT3 R (992)',

  mass: 1330,              // ~1.250 kg + piloto + combustível
  inertia: { pitch: 2000, yaw: 2150, roll: 500 },
  cgHeight: 0.40,
  frontWeight: 0.43,       // motor atrás do eixo traseiro: carro "pesado de traseira"

  geometry: {
    wheelbase: 2.507,
    trackFront: 1.68,
    trackRear: 1.64,
    radiusFront: 0.34,
    radiusRear: 0.355,
  },

  suspension: {
    // duplo A na frente, multi-link atrás; traseira mais dura por carregar mais peso
    front: { k: 140000, bump: 6000, rebound: 9000, arb: 52000, travel: 0.055 },
    rear: { k: 178000, bump: 7500, rebound: 11500, arb: 36000, travel: 0.060 },
    bumpStopK: 1.2e6,
  },

  tire: {
    // Michelin Pilot Sport GT S9M (slick)
    mu: 1.62,
    fz0: 3800,
    loadSensitivity: -0.11,
    slipRatioPeak: 0.085,
    slipAnglePeakDeg: 6.5,
    C: 1.40,
    E: 0.10,
    longScale: 1.05,
    rollingResistance: 0.012,
    inertiaFront: 1.10,
    inertiaRear: 1.35,
  },

  brakes: {
    maxTorqueFront: 3200,     // disco de 390 mm
    maxTorqueRear: 2250,      // 370 mm; mais freio atrás porque o peso está atrás
    handbrakeTorque: 2600,
    heatCapacity: 1800,
    ambientTemp: 25,
    coolingBase: 0.01,
    coolingPerMs: 0.0012,
  },

  engine: {
    idleRpm: 1500,
    limiterRpm: 9500,
    inertia: 0.16,            // aspirado de giro alto: volante leve
    // Curva em plena carga (N·m): ~565 cv a 9.400 rpm, sem turbo
    torqueCurve: [
      [800, 180], [1500, 250], [2500, 320], [3500, 375], [4500, 410], [5500, 435],
      [6500, 455], [7500, 462], [8500, 452], [9000, 440], [9400, 425], [9800, 330],
    ],
    frictionA: 20,
    frictionB: 0.0080,
    turboLag: 0.05,
    turboShare: 0,            // aspirado
    boostRpmStart: 1000,
    boostRpmFull: 1500,
    clutchFullRpm: 10500,     // largada em ~6.000 rpm (pouco torque embaixo)
  },

  gearbox: {
    ratios: [3.12, 2.31, 1.87, 1.59, 1.42, 1.31],
    reverse: -3.00,
    finalDrive: 3.40,
    efficiency: 0.92,
    shiftTime: 0.06,
    clutchMaxTorque: 800,
  },

  differential: {
    preload: 100,
    powerLock: 0.40,          // mais travamento: ajuda a tracionar a traseira pesada
    coastLock: 0.30,
    viscous: 350,
  },

  aero: {
    rho: 1.225,
    CdA: 1.06,
    ClA: 2.55,                // asa "pescoço de cisne" grande
    frontBalance: 0.38,
  },

  steering: {
    maxAngleDeg: 22,
    ackermann: 0.45,
  },

  assists: {
    abs: true,
    tc: true,
    autoGearbox: true,
  },
};
