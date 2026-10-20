// Testes da física sem navegador: node test/sim-test.js
import { Vehicle, FlatGround } from '../js/physics/vehicle.js';
import { FERRARI_296_GT3, PORSCHE_992_GT3_R } from '../js/physics/carSpec.js';

const DT = 1 / 1000;
const kmh = (v) => v * 3.6;
let failures = 0;
const check = (name, ok, info) => {
  console.log(`${ok ? 'OK  ' : 'FAIL'} ${name}: ${info}`);
  if (!ok) failures++;
};
const finite = (car) => [car.pos.x, car.pos.y, car.pos.z, car.vel.x, car.vel.y, car.vel.z, car.engine.omega].every(Number.isFinite);

// faixas esperadas de cada carro (números públicos / típicos de GT3)
const SUITES = [
  [FERRARI_296_GT3, { t100: [2.6, 4.2], t200: [7, 12], vmax: [265, 310], brake: [75, 150] }],
  [PORSCHE_992_GT3_R, { t100: [2.8, 4.3], t200: [7.5, 12.5], vmax: [270, 300], brake: [75, 150] }],
];
const inRange = (v, [a, b]) => v !== null && v > a && v < b;
let SPEC, make;

for (const [spec, expect] of SUITES) {
SPEC = spec;
make = () => new Vehicle(SPEC, new FlatGround());
console.log(`\n=== ${spec.name} ===`);

// 1) Repouso: assentar e não derivar
{
  const car = make();
  const inp = { throttle: 0, brake: 0, steer: 0, handbrake: 0 };
  for (let i = 0; i < 4000; i++) car.step(DT, inp);
  const spd = car.speed;
  const drift = Math.hypot(car.pos.x, car.pos.z);
  check('repouso', spd < 0.01 && drift < 0.02 && finite(car), `vel=${spd.toExponential(2)} m/s, deriva=${drift.toFixed(4)} m, CG y=${car.pos.y.toFixed(3)}, rpm=${car.engine.rpm.toFixed(0)}`);
  const fz = car.wheels.map((w) => w.fz.toFixed(0)).join('/');
  check('cargas estáticas', Math.abs(car.wheels.reduce((s, w) => s + w.fz, 0) - car.mass * 9.81) < 50, `Fz FL/FR/RL/RR = ${fz} N`);
}

// 2) Aceleração (câmbio automático, TC ligado)
{
  const car = make();
  const inp = { throttle: 1, brake: 0, steer: 0, handbrake: 0 };
  let t100 = null, t200 = null, t = 0, maxV = 0, shifts = [];
  let lastGear = 0;
  for (; t < 70; t += DT) {
    car.step(DT, inp);
    const v = kmh(car.forwardSpeed);
    if (car.gear !== lastGear) { shifts.push(`${car.gear}@${v.toFixed(0)}`); lastGear = car.gear; }
    if (t100 === null && v >= 100) t100 = t;
    if (t200 === null && v >= 200) t200 = t;
    maxV = Math.max(maxV, v);
    if (!finite(car)) break;
  }
  check('0-100 km/h', inRange(t100, expect.t100), `${t100?.toFixed(2)} s`);
  check('0-200 km/h', inRange(t200, expect.t200), `${t200?.toFixed(2)} s`);
  check('velocidade máxima', inRange(maxV, expect.vmax), `${maxV.toFixed(1)} km/h, marchas: ${shifts.join(' ')}`);
  check('sem desvio lateral', Math.abs(car.pos.x) < 1, `x=${car.pos.x.toFixed(3)} m após ${car.pos.z.toFixed(0)} m`);
}

// 3) Frenagem 200 → 0 com ABS
{
  const car = make();
  let inp = { throttle: 1, brake: 0, steer: 0, handbrake: 0 };
  while (kmh(car.forwardSpeed) < 200) car.step(DT, inp);
  inp = { throttle: 0, brake: 1, steer: 0, handbrake: 0 };
  const z0 = car.pos.z;
  let t = 0, peakG = 0;
  while (car.forwardSpeed > 0.05 && t < 15) { car.step(DT, inp); t += DT; peakG = Math.max(peakG, car.accel.z / 9.81); }
  const dist = Math.abs(car.pos.z - z0);
  check('frenagem 200-0', inRange(dist, expect.brake), `${dist.toFixed(1)} m em ${t.toFixed(2)} s, pico ${peakG.toFixed(2)} g`);
  for (let i = 0; i < 3000; i++) car.step(DT, inp);
  check('parado após frear', car.speed < 0.02 && finite(car), `vel=${car.speed.toExponential(2)}, rpm=${car.engine.rpm.toFixed(0)}, marcha=${car.gear}`);
}

// 4) Curva em regime: aumenta esterço a velocidade constante e mede g lateral máximo
for (const target of [80, 160]) {
  const car = make();
  let inp = { throttle: 0, brake: 0, steer: 0, handbrake: 0 };
  let maxLat = 0, t = 0;
  while (t < 40) {
    const v = kmh(car.forwardSpeed);
    inp.throttle = Math.min(1, Math.max(0, (target - v) * 0.15 + 0.25));
    if (t > 12) inp.steer = Math.min(0.6, (t - 12) * (target > 100 ? 0.012 : 0.03));
    car.step(DT, inp);
    t += DT;
    if (t > 12) maxLat = Math.max(maxLat, Math.abs(car.accel.x) / 9.81);
    if (!finite(car)) break;
  }
  check(`g lateral a ${target} km/h`, maxLat > 1.4 && maxLat < 2.6 && finite(car), `${maxLat.toFixed(2)} g`);
}

// 5) Ré
{
  const car = make();
  const idle = { throttle: 0, brake: 0, steer: 0, handbrake: 0 };
  car.assists.autoGearbox = false;
  car.shiftDown(); for (let i = 0; i < 200; i++) car.step(DT, idle);
  const inp = { throttle: 0.5, brake: 0, steer: 0, handbrake: 0 };
  for (let i = 0; i < 3000; i++) car.step(DT, inp);
  check('marcha à ré', car.gear === -1 && car.forwardSpeed < -2, `marcha=${car.gear}, v=${kmh(car.forwardSpeed).toFixed(1)} km/h`);
}

// 6) Freio de mão + esterço em baixa: deve rodar (sobre-esterço) sem explodir
{
  const car = make();
  let inp = { throttle: 1, brake: 0, steer: 0, handbrake: 0 };
  while (kmh(car.forwardSpeed) < 90) car.step(DT, inp);
  inp = { throttle: 0, brake: 0, steer: 1, handbrake: 1 };
  let maxYaw = 0;
  for (let i = 0; i < 3000; i++) { car.step(DT, inp); maxYaw = Math.max(maxYaw, Math.abs(car.angVel.y)); }
  check('freio de mão', finite(car) && maxYaw > 0.8 && car.pos.y > 0.2 && car.pos.y < 0.6, `taxa de guinada máx ${maxYaw.toFixed(2)} rad/s, CG y=${car.pos.y.toFixed(3)}`);
}

// 7) Estresse: entradas aleatórias, sem auxílios, trocas de marcha aleatórias
{
  const car = make();
  car.assists = { abs: false, tc: false, autoGearbox: false };
  let s = 12345;
  const rnd = () => ((s = (s * 16807) % 2147483647) / 2147483647);
  const inp = { throttle: 0, brake: 0, steer: 0, handbrake: 0 };
  let ok = true, maxY = 0;
  for (let i = 0; i < 120000 && ok; i++) {
    if (i % 400 === 0) {
      inp.throttle = rnd() < 0.6 ? rnd() : 0;
      inp.brake = rnd() < 0.25 ? rnd() : 0;
      inp.steer = rnd() * 2 - 1;
      inp.handbrake = rnd() < 0.05 ? 1 : 0;
      const r = rnd();
      if (r < 0.15) car.shiftUp(); else if (r < 0.25) car.shiftDown();
    }
    car.step(DT, inp);
    ok = finite(car);
    maxY = Math.max(maxY, car.pos.y);
  }
  check('estresse 120 s', ok && maxY < 1.5, `finito=${ok}, CG y máx=${maxY.toFixed(3)} m, v final=${kmh(car.speed).toFixed(0)} km/h`);
}

}

// 8) Desempenho
{
  const car = new Vehicle(FERRARI_296_GT3, new FlatGround());
  const inp = { throttle: 1, brake: 0, steer: 0.2, handbrake: 0 };
  const t0 = performance.now();
  for (let i = 0; i < 20000; i++) car.step(DT, inp);
  const ms = (performance.now() - t0) / 20;
  check('custo por segundo simulado', ms < 30, `${ms.toFixed(2)} ms de CPU para 1 s (1000 passos)`);
}

console.log(failures ? `\n${failures} falha(s)` : '\nTodos os testes passaram');
process.exit(failures ? 1 : 0);
