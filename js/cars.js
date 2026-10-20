// Catálogo de carros da garagem. Para adicionar um carro: um GLB, uma miniatura, um spec de física
// e o bloco `visual`, que diz ao carregador quais materiais do GLB são o quê
// (padrões: 'nome' = exato, 'prefixo*' = começa com).

import { FERRARI_296_GT3, PORSCHE_992_GT3_R, LAMBORGHINI_FENOMENO } from './physics/carSpec.js';

export const CARS = [
  {
    id: 'ferrari-296-gt3',
    maker: 'Ferrari',
    name: '296 GT3',
    year: 2024,
    category: 'GT3',
    model: 'ferrari-488-gt3/source/ferrari_488_gt3.glb',
    thumb: 'assets/cars/ferrari-296-gt3.jpg',
    spec: FERRARI_296_GT3,
    visual: {
      tires: ['mat_235'],
      headlights: ['AR3DMat_PBR_Light_Cool_White'],
      tailLights: ['mat_50'],
      brakeGlow: ['mat_57'],
      steeringWheel: ['mat_225'],
      glass: ['Window'],
      fixBodyMetal: true,          // o GLB deixa a carroceria 100% metálica
      cockpit: { eye: [-0.33, 0.93, -0.14], helmet: [-0.33, 0.9, 0.07] },
      hoodEye: [0, 1.0, -0.75],
    },
    // números medidos na própria simulação (npm test)
    figures: [
      ['Potência', '610 cv'],
      ['Peso', '1.310 kg'],
      ['0–100 km/h', '3,2 s'],
      ['Velocidade máx.', '289 km/h'],
      ['Motor', 'V6 2.9 biturbo'],
      ['Tração', 'Traseira'],
    ],
    // barras de 0 a 10, estilo jogo
    ratings: [
      ['Velocidade', 7.9],
      ['Aceleração', 8.4],
      ['Frenagem', 9.1],
      ['Curva', 8.8],
    ],
  },
  {
    id: 'porsche-992-gt3-r',
    maker: 'Porsche',
    name: '911 GT3 R',
    year: 2024,
    category: 'GT3',
    model: '2024_porsche_992_gt3_r/2024_porsche_992_gt3_r.glb',
    thumb: 'assets/cars/porsche-992-gt3-r.jpg',
    spec: PORSCHE_992_GT3_R,
    visual: {
      wheelbase: 2.507,            // o GLB vem em escala reduzida; ajusta pelo entre-eixos real
      tires: ['MI_Tyre_Flex_Dry_R*'],
      headlights: ['EXT_Emissive_Light_Front*'],
      headlightGlow: 1.2,
      tailLights: ['EXT_Emissive_Light_Rear*'],
      tailLightUseMap: true,
      discs: ['EXT_Disc*'],
      rimBlur: ['EXT_RIM_BLUR*'],
      steeringWheel: ['INT_Steer', 'INT_Steer_Carbon', 'STEER_PLASTIC'],
      glass: ['EXT_Windows*', 'INT_Windows', 'INT_Windshield'],
      paint: ['EXT_Carpaint_Inst'],
    },
    figures: [
      ['Potência', '565 cv'],
      ['Peso', '1.330 kg'],
      ['0–100 km/h', '3,9 s'],
      ['Velocidade máx.', '284 km/h'],
      ['Motor', 'Boxer 6 4.2 aspirado'],
      ['Tração', 'Traseira'],
    ],
    ratings: [
      ['Velocidade', 7.7],
      ['Aceleração', 7.9],
      ['Frenagem', 9.1],
      ['Curva', 8.7],
    ],
  },
  {
    id: 'lamborghini-fenomeno',
    maker: 'Lamborghini',
    name: 'Fenomeno',
    year: 2026,
    category: 'Hypercar',
    model: 'lamborghini_fenomeno_2026/lamborghini_fenomeno_2026.glb',
    thumb: 'assets/cars/lamborghini-fenomeno.jpg',
    spec: LAMBORGHINI_FENOMENO,
    visual: {
      wheelbase: 2.779,            // o GLB vem ~23% maior que o carro real
      // as rodas esquerda e direita vêm na mesma malha: o carregador divide ao meio
      splitWheels: ['front_tires.001', 'rear_tires*', 'rims_1*', 'carpaint_rims*', 'Caliper*', 'rotors*', 'metal*', 'ext_plastics*', 'Engexttex1Mtl*', 'redbodylight*'],
      tires: ['front_tires.001', 'rear_tires*'],
      frontHint: ['dash'],         // os faróis do modelo atravessam o carro; o painel indica a frente
      tailLights: ['przemo_l.001'],
      tailLightUseMap: true,
      discs: ['rotors*'],
      steeringWheelNodes: ['Object_133', 'Object_134', 'Object_135', 'Object_136', 'Object_137', 'Object_138', 'Object_139'],
      glass: ['glass_1', 'CAR_GLASS*'],
    },
    figures: [
      ['Potência', '1.080 cv'],
      ['Peso', '1.690 kg'],
      ['0–100 km/h', '2,5 s'],
      ['Velocidade máx.', '353 km/h'],
      ['Motor', 'V12 6.5 híbrido'],
      ['Tração', 'Integral'],
    ],
    ratings: [
      ['Velocidade', 9.8],
      ['Aceleração', 9.7],
      ['Frenagem', 8.3],
      ['Curva', 7.6],
    ],
  },
];
