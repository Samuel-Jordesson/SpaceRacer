#include "settings.h"

#include <SDL3/SDL.h>

#include <cmath>
#include <fstream>
#include <sstream>

void Settings::applyPreset(int p) {
  p = ((p % 4) + 4) % 4;
  const Quality& q = QUALITY[p];
  preset = p;
  pixelRatio = q.pixelRatio;
  msaa = q.msaa;
  ao = !q.ao ? 0 : q.aoHalfRes ? 1 : 2;
  bloom = q.bloom;
  shadow = q.shadow;
}

void Settings::matchPreset() {
  preset = -1;
  for (int p = 0; p < 4; p++) {
    const Quality& q = QUALITY[p];
    int qao = !q.ao ? 0 : q.aoHalfRes ? 1 : 2;
    if (std::fabs(q.pixelRatio - pixelRatio) < 0.01f && q.msaa == msaa && qao == ao && q.bloom == bloom && q.shadow == shadow) preset = p;
  }
}

Quality Settings::quality() const {
  Quality q = preset >= 0 ? QUALITY[preset] : QUALITY[2];
  if (preset < 0) q.name = "Personalizado";
  q.pixelRatio = pixelRatio;
  q.msaa = msaa;
  q.ao = ao > 0;
  q.aoSamples = ao == 2 ? 16 : 12;
  q.aoHalfRes = ao == 1;
  q.bloom = bloom;
  q.shadow = shadow;
  return q;
}

std::string Settings::path() {
  char* dir = SDL_GetPrefPath("SpaceRacer", "SpaceRacer");
  std::string p = dir ? std::string(dir) + "settings.ini" : "settings.ini";
  SDL_free(dir);
  return p;
}

void Settings::load() {
  std::ifstream in(path());
  if (!in) return;
  std::string line;
  while (std::getline(in, line)) {
    auto eq = line.find('=');
    if (eq == std::string::npos) continue;
    std::string k = line.substr(0, eq);
    std::istringstream v(line.substr(eq + 1));
    if (k == "preset") v >> preset;
    else if (k == "pixelRatio") v >> pixelRatio;
    else if (k == "msaa") v >> msaa;
    else if (k == "ao") v >> ao;
    else if (k == "bloom") v >> bloom;
    else if (k == "shadow") v >> shadow;
    else if (k == "shadowDist") v >> shadowDist;
    else if (k == "shadowSoft") v >> shadowSoft;
    else if (k == "lensFx") v >> lensFx;
    else if (k == "speedBlur") v >> speedBlur;
    else if (k == "autoQuality") v >> autoQuality;
    else if (k == "fullscreen") v >> fullscreen;
    else if (k == "vsync") v >> vsync;
    else if (k == "windowW") v >> windowW;
    else if (k == "windowH") v >> windowH;
    else if (k == "windowX") v >> windowX;
    else if (k == "windowY") v >> windowY;
    else if (k == "windowMax") v >> windowMax;
    else if (k == "volume") v >> volume;
    else if (k == "steerSens") v >> steerSens;
    else if (k == "stabilityAssist") v >> stabilityAssist;
    else if (k == "lastCar") v >> lastCar;
    else if (k == "lastMap") v >> lastMap;
    else if (k == "timeOfDay") v >> timeOfDay;
    else if (k == "raceMode") v >> raceMode;
    else if (k == "raceLaps") v >> raceLaps;
    else if (k == "raceOpponents") v >> raceOpponents;
  }
  // valores fora do esperado (arquivo editado à mão)
  pixelRatio = std::fmax(0.5f, std::fmin(2.0f, pixelRatio));
  if (msaa != 0 && msaa != 2 && msaa != 4) msaa = 4;
  ao = std::max(0, std::min(2, ao));
  if (shadow != 1024 && shadow != 2048 && shadow != 4096) shadow = 4096;
  if (shadowDist != 20 && shadowDist != 40 && shadowDist != 60 && shadowDist != 100 && shadowDist != 150) shadowDist = 60;
  shadowSoft = std::fmax(0.5f, std::fmin(3.f, shadowSoft));
  volume = std::fmax(0.f, std::fmin(1.f, volume));
  steerSens = std::fmax(0.f, std::fmin(1.f, steerSens));
  timeOfDay = std::max(0, std::min(1, timeOfDay));
  raceMode = std::max(0, std::min(2, raceMode));
  raceLaps = std::max(1, std::min(30, raceLaps));
  raceOpponents = std::max(1, std::min(7, raceOpponents));
  windowW = std::max(MIN_WINDOW_W, std::min(7680, windowW));
  windowH = std::max(MIN_WINDOW_H, std::min(4320, windowH));
  matchPreset();
}

void Settings::save() const {
  std::ofstream out(path());
  out << "preset=" << preset << "\npixelRatio=" << pixelRatio << "\nmsaa=" << msaa << "\nao=" << ao << "\nbloom=" << bloom
      << "\nshadow=" << shadow << "\nshadowDist=" << shadowDist << "\nshadowSoft=" << shadowSoft << "\nlensFx=" << lensFx << "\nspeedBlur=" << speedBlur << "\nautoQuality=" << autoQuality
      << "\nfullscreen=" << fullscreen << "\nvsync=" << vsync << "\nwindowW=" << windowW << "\nwindowH=" << windowH
      << "\nwindowX=" << windowX << "\nwindowY=" << windowY << "\nwindowMax=" << windowMax << "\nvolume=" << volume << "\nsteerSens=" << steerSens << "\nstabilityAssist=" << stabilityAssist << "\nlastCar=" << lastCar << "\nlastMap=" << lastMap << "\ntimeOfDay=" << timeOfDay << "\nraceMode=" << raceMode << "\nraceLaps=" << raceLaps << "\nraceOpponents=" << raceOpponents << "\n";
}
