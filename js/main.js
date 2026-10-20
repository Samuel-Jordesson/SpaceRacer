// Aplicação: garagem (tela inicial) → escolher carro → pilotar → Esc volta para a garagem.

import * as THREE from 'three';
import { Garage } from './garage.js';
import { Game } from './game.js';
import { loadCar } from './carModel.js';
import { CARS } from './cars.js';
import { Cinematic } from './cinematic.js';

const renderer = new THREE.WebGLRenderer({ antialias: false, powerPreference: 'high-performance', stencil: false });
renderer.setSize(innerWidth, innerHeight);
renderer.toneMapping = THREE.ACESFilmicToneMapping;
renderer.toneMappingExposure = 0.95;
renderer.shadowMap.enabled = true;
renderer.shadowMap.type = THREE.PCFSoftShadowMap;
document.getElementById('app').appendChild(renderer.domElement);

const garage = new Garage(renderer);
const game = new Game(renderer);
const cine = garage.cine = new Cinematic(garage);
let mode = 'garage';
garage.setActive(true);

// ---------------------------------------------------------------- UI da garagem
const ui = {
  root: document.getElementById('garage-ui'),
  strip: document.getElementById('car-strip'),
  maker: document.getElementById('car-maker'),
  name: document.getElementById('car-name'),
  meta: document.getElementById('car-meta'),
  figures: document.getElementById('car-figures'),
  ratings: document.getElementById('car-ratings'),
  info: document.getElementById('car-info'),
  play: document.getElementById('play'),
  cineBtn: document.getElementById('cine-btn'),
  empty: document.getElementById('garage-empty'),
  fade: document.getElementById('fade'),
  fadeText: document.getElementById('fade-text'),
  fadeBar: document.getElementById('fade-fill'),
};
const cache = new Map(); // id → modelo carregado
let selected = null, loading = null;

for (const car of CARS) {
  const card = document.createElement('button');
  card.className = 'car-card';
  card.dataset.id = car.id;
  card.innerHTML = `
    <div class="thumb"><img src="${car.thumb}" alt="" draggable="false"></div>
    <div class="label"><span class="maker">${car.maker}</span><span class="model">${car.name}</span></div>
    <span class="cls">${car.category}</span>
    <div class="progress"><div></div></div>`;
  card.addEventListener('click', () => selectCar(car, card));
  ui.strip.appendChild(card);
}

async function selectCar(car, card) {
  if (loading || selected === car) return;
  document.querySelectorAll('.car-card').forEach((c) => c.classList.toggle('selected', c === card));
  ui.play.disabled = true;
  ui.cineBtn.disabled = true;
  showInfo(car);
  let loaded = cache.get(car.id);
  if (!loaded) {
    loading = car;
    card.classList.add('loading');
    const bar = card.querySelector('.progress div');
    try {
      loaded = await loadCar(car.model, car.visual, (p) => { bar.style.transform = `scaleX(${p})`; });
    } catch (err) {
      console.error(err);
      card.classList.remove('loading');
      loading = null;
      ui.meta.textContent = 'Erro ao carregar o modelo: ' + err.message;
      return;
    }
    cache.set(car.id, loaded);
    card.classList.remove('loading');
    loading = null;
  }
  selected = car;
  ui.empty.classList.add('hide');
  garage.showCar(loaded);
  ui.play.disabled = false;
  ui.cineBtn.disabled = false;
}

function showInfo(car) {
  ui.info.classList.remove('show');
  void ui.info.offsetWidth; // reinicia a animação
  ui.maker.textContent = car.maker;
  ui.name.textContent = car.name;
  ui.meta.textContent = `${car.year} · ${car.category}`;
  ui.figures.innerHTML = car.figures.map(([k, v]) => `<div><span>${k}</span><b>${v}</b></div>`).join('');
  ui.ratings.innerHTML = car.ratings.map(([k, v]) =>
    `<div class="rating"><span>${k}</span><div class="bar"><i style="transform:scaleX(${v / 10})"></i></div><b>${v.toFixed(1)}</b></div>`).join('');
  ui.info.classList.add('show');
}

// ---------------------------------------------------------------- transições
const wait = (ms) => new Promise((r) => setTimeout(r, ms));

async function play() {
  if (!selected || mode !== 'garage' || ui.play.disabled) return;
  mode = 'loading';
  ui.fade.classList.add('show');
  ui.fadeText.textContent = 'Preparando a pista…';
  ui.fadeBar.style.transform = 'scaleX(0)';
  await wait(450);
  try {
    await game.prepare((p) => { ui.fadeBar.style.transform = `scaleX(${p})`; });
  } catch (err) {
    console.error(err);
    ui.fadeText.textContent = 'Erro ao carregar a pista: ' + err.message;
    mode = 'garage';
    return;
  }
  ui.fadeBar.style.transform = 'scaleX(1)';
  garage.setActive(false);
  ui.root.classList.add('hide');
  game.start(selected, cache.get(selected.id));
  mode = 'game';
  await wait(150);
  ui.fade.classList.remove('show');
}

async function backToGarage() {
  if (mode !== 'game') return;
  mode = 'loading';
  ui.fadeText.textContent = '';
  ui.fadeBar.style.transform = 'scaleX(0)';
  ui.fade.classList.add('show');
  await wait(400);
  game.stop();
  garage.reclaimCar();
  garage.setActive(true);
  ui.root.classList.remove('hide');
  mode = 'garage';
  await wait(100);
  ui.fade.classList.remove('show');
}
game.onExit = backToGarage;

ui.play.addEventListener('click', play);

// ---------------------------------------------------------------- cinemática
ui.cineBtn.addEventListener('click', () => {
  if (!selected || mode !== 'garage' || ui.cineBtn.disabled) return;
  mode = 'cinematic';
  ui.root.classList.add('hide');
  cine.start(selected, cache.get(selected.id));
});
cine.onExit = () => {
  setTimeout(() => { ui.root.classList.remove('hide'); mode = 'garage'; }, 250);
};
addEventListener('keydown', (e) => {
  if (mode !== 'garage') return;
  if (e.code === 'Enter') play();
  // setas trocam de carro na lista
  if (e.code === 'ArrowRight' || e.code === 'ArrowLeft') {
    const cards = [...document.querySelectorAll('.car-card')];
    const i = cards.findIndex((c) => c.classList.contains('selected'));
    cards[(i + (e.code === 'ArrowRight' ? 1 : -1) + cards.length) % cards.length]?.click();
  }
});

addEventListener('resize', () => {
  if (mode === 'game') game.resize(); else garage.resize();
});

// ---------------------------------------------------------------- loop
let last = performance.now();
function frame(now) {
  const dt = Math.min(0.05, (now - last) / 1000);
  last = now;
  if (mode === 'game') game.frame(dt);
  else if (garage.active) garage.frame(dt); // inclui a cinemática
  requestAnimationFrame(frame);
}
requestAnimationFrame(frame);
document.getElementById('boot').classList.add('done');

// acesso pelo console do navegador (ex.: game.car.spec.tire.mu = 1.4)
window.app = { renderer, garage, game, cine, CARS, cache, play, backToGarage };
window.game = game;
