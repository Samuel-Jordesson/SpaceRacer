// Som sintetizado (sem arquivos de áudio): motor (frequência de combustão = rpm/60 · cilindros/2),
// turbo, chiado de pneu e vento. Reproduz o grafo do Web Audio da versão de navegador.
#pragma once
#include <SDL3/SDL.h>

#include <atomic>
#include <mutex>
#include <vector>

#include "../physics/vehicle.h"

class CarAudio {
 public:
  ~CarAudio();
  void start();
  void setPaused(bool p);
  void toggleMute();
  void setVolume(float v);
  void update(const phys::Vehicle& car);

 private:
  enum P { OSC0, OSC1, OSC2, OSC3, OSC4, ENG_GAIN, ENG_FREQ, RASP_FREQ, RASP_GAIN, TURBO_FREQ, TURBO_GAIN,
           TIRE_GAIN, TIRE_FREQ, WIND_GAIN, WIND_FREQ, MASTER, NPARAMS };
  struct Param {
    float value = 0, target = 0, tau = 0.03f;
  };
  struct Biquad {
    float b0 = 0, b1 = 0, b2 = 0, a1 = 0, a2 = 0, x1 = 0, x2 = 0, y1 = 0, y2 = 0;
    void lowpass(float f, float qDb, float sr);
    void bandpass(float f, float q, float sr);
    float process(float x);
  };
  static void SDLCALL callback(void* user, SDL_AudioStream* stream, int additional, int total);
  void render(float* out, int frames);
  void set(P p, float target, float tau);

  SDL_AudioStream* stream = nullptr;
  float sr = 48000;
  std::mutex mtx;
  Param params[NPARAMS];      // lado do áudio
  Param pending[NPARAMS];     // escrito pela thread principal
  bool muted = false;
  float volume = 1;
  std::atomic<double> clock{0};
  // estado da síntese
  double phase[5] = {}, turboPhase = 0;
  std::vector<float> noise;
  size_t noisePos = 0;
  Biquad engineLp, rasp, tireBp, windLp;
  std::vector<float> curve;
  float compEnv = 0, compGain = 1;
};
