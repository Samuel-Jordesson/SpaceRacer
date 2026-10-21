#include "audio.h"

#include <cmath>
#include <random>

namespace {

const float MULT[5] = {1, 2, 0.5f, 3, 1.005f};
const float GAIN[5] = {0.5f, 0.22f, 0.18f, 0.12f, 0.25f};
enum Wave { SAW, SQUARE, TRIANGLE };
const Wave TYPE[5] = {SAW, SAW, SQUARE, TRIANGLE, SAW};

// PolyBLEP: suaviza as descontinuidades (osciladores limitados em banda, como no Web Audio)
float polyblep(double t, double dt) {
  if (t < dt) { t /= dt; return (float)(t + t - t * t - 1); }
  if (t > 1 - dt) { t = (t - 1) / dt; return (float)(t * t + t + t + 1); }
  return 0;
}

}  // namespace

void CarAudio::Biquad::lowpass(float f, float qDb, float s) {
  // Web Audio: Q do passa-baixa em dB
  f = std::fmin(f, s * 0.49f);
  float w0 = 2 * (float)M_PI * f / s, cw = std::cos(w0), alpha = std::sin(w0) / (2 * std::pow(10.f, qDb / 20));
  float a0 = 1 + alpha;
  b0 = (1 - cw) / 2 / a0; b1 = (1 - cw) / a0; b2 = b0;
  a1 = -2 * cw / a0; a2 = (1 - alpha) / a0;
}

void CarAudio::Biquad::bandpass(float f, float q, float s) {
  f = std::fmin(f, s * 0.49f);
  float w0 = 2 * (float)M_PI * f / s, cw = std::cos(w0), alpha = std::sin(w0) / (2 * q);
  float a0 = 1 + alpha;
  b0 = alpha / a0; b1 = 0; b2 = -alpha / a0;
  a1 = -2 * cw / a0; a2 = (1 - alpha) / a0;
}

float CarAudio::Biquad::process(float x) {
  float y = b0 * x + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2;
  x2 = x1; x1 = x; y2 = y1; y1 = y;
  return y;
}

CarAudio::~CarAudio() {
  if (stream) SDL_DestroyAudioStream(stream);
}

void CarAudio::start() {
  if (stream) return;
  // ruído branco de 2 s em loop
  std::mt19937 rng(7);
  std::uniform_real_distribution<float> u(-1, 1);
  noise.resize((size_t)(sr * 2));
  for (auto& n : noise) n = u(rng);
  // curva de distorção do WaveShaper
  const int n = 1024;
  const float amount = 6;
  curve.resize(n);
  for (int i = 0; i < n; i++) {
    float x = (i * 2.f) / n - 1;
    curve[i] = ((1 + amount) * x) / (1 + amount * std::fabs(x));
  }
  for (auto* ps : {params, pending}) {
    for (int i = 0; i < NPARAMS; i++) ps[i] = Param{};
    ps[MASTER] = {0.55f, 0.55f, 0.05f};
    ps[ENG_FREQ] = {350, 350, 0.03f};
    ps[TIRE_FREQ] = {1100, 1100, 0.05f};
    ps[WIND_FREQ] = {500, 500, 0.1f};
    ps[RASP_FREQ] = {350, 350, 0.03f};
    ps[TURBO_FREQ] = {2500, 2500, 0.05f};
  }
  SDL_AudioSpec spec{SDL_AUDIO_F32, 2, (int)sr};
  stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, callback, this);
  if (!stream) {
    SDL_Log("[audio] sem dispositivo de áudio: %s", SDL_GetError());
    return;
  }
  SDL_ResumeAudioStreamDevice(stream);
}

void CarAudio::setPaused(bool p) {
  if (!stream) return;
  if (p) SDL_PauseAudioStreamDevice(stream);
  else SDL_ResumeAudioStreamDevice(stream);
}

void CarAudio::set(P p, float target, float tau) {
  pending[p].target = target;
  pending[p].tau = tau;
}

void CarAudio::toggleMute() {
  std::lock_guard<std::mutex> lk(mtx);
  muted = !muted;
  set(MASTER, muted ? 0 : 0.55f * volume, 0.05f);
}

void CarAudio::setVolume(float v) {
  std::lock_guard<std::mutex> lk(mtx);
  volume = v;
  set(MASTER, muted ? 0 : 0.55f * volume, 0.05f);
}

void CarAudio::update(const phys::Vehicle& car) {
  if (!stream) return;
  std::lock_guard<std::mutex> lk(mtx);
  float t = (float)clock.load(), k = 0.03f;
  float rpm = std::fmax(300.f, (float)car.engine.rpm);
  float f = rpm / 60 * ((car.spec.engine.cylinders ? car.spec.engine.cylinders : 6) / 2.f);  // quatro tempos
  for (int i = 0; i < 5; i++) set(P(OSC0 + i), f * MULT[i], 0.01f);
  float load = (float)car.engine.throttle;
  float limiter = car.engine.limiter ? 0.55f + 0.45f * std::sin(t * 90) : 1;
  set(ENG_GAIN, (0.12f + 0.28f * load + rpm / (float)car.spec.engine.limiterRpm * 0.12f) * limiter, k);
  set(ENG_FREQ, 500 + load * 2600 + rpm * 0.35f, k);
  set(RASP_FREQ, f * 3.5f, k);
  set(RASP_GAIN, 0.15f + 0.5f * load, k);
  set(TURBO_FREQ, 2500 + (float)car.engine.boost * 4500, 0.05f);
  set(TURBO_GAIN, car.spec.engine.turboShare > 0 ? (float)car.engine.boost * 0.018f : 0, 0.05f);
  float slip = 0;
  for (auto& w : car.wheels)
    if (w.contact) slip = std::fmax(slip, std::fmin(1.f, std::fmax(0.f, (float)w.slip - 0.85f)) * std::fmin(1.f, (float)w.fz / 3000));
  float spd = (float)car.speed();
  set(TIRE_GAIN, spd > 1.5f ? slip * 0.35f : 0, 0.04f);
  set(TIRE_FREQ, 800 + slip * 600, 0.05f);
  set(WIND_GAIN, std::fmin(0.45f, spd * spd * 0.00006f), 0.1f);
  set(WIND_FREQ, 300 + spd * 12, 0.1f);
}

void SDLCALL CarAudio::callback(void* user, SDL_AudioStream* stream, int additional, int) {
  auto* self = static_cast<CarAudio*>(user);
  int frames = additional / (int)(sizeof(float) * 2);
  if (frames <= 0) return;
  std::vector<float> buf(frames * 2);
  self->render(buf.data(), frames);
  SDL_PutAudioStreamData(stream, buf.data(), (int)(buf.size() * sizeof(float)));
}

void CarAudio::render(float* out, int frames) {
  {
    std::lock_guard<std::mutex> lk(mtx);
    for (int i = 0; i < NPARAMS; i++) {
      params[i].target = pending[i].target;
      params[i].tau = pending[i].tau;
    }
  }
  const float dt = 1 / sr;
  float coef[NPARAMS];
  for (int i = 0; i < NPARAMS; i++) coef[i] = 1 - std::exp(-dt / std::fmax(params[i].tau, 1e-4f));
  // compressor do Web Audio (padrões: −24 dB, joelho 30, razão 12, ataque 3 ms, soltura 250 ms)
  const float T = -24, Wk = 30, R = 12;
  auto curveDb = [&](float L) {
    if (L < T - Wk / 2) return L;
    if (L > T + Wk / 2) return T + (L - T) / R;
    float x = L - T + Wk / 2;
    return L + (1 / R - 1) * x * x / (2 * Wk);
  };
  const float makeup = std::pow(1 / std::pow(10.f, curveDb(0) / 20), 0.6f);
  const float att = 1 - std::exp(-dt / 0.003f), rel = 1 - std::exp(-dt / 0.25f);

  for (int s = 0; s < frames; s++) {
    for (int i = 0; i < NPARAMS; i++) params[i].value += (params[i].target - params[i].value) * coef[i];
    if ((s & 31) == 0) {
      engineLp.lowpass(params[ENG_FREQ].value, 2.5f, sr);
      rasp.bandpass(params[RASP_FREQ].value, 1.5f, sr);
      tireBp.bandpass(params[TIRE_FREQ].value, 6, sr);
      windLp.lowpass(params[WIND_FREQ].value, 1, sr);
    }
    float nz = noise[noisePos];
    noisePos = (noisePos + 1) % noise.size();

    // motor: harmônicos + ronco do escape → distorção → passa-baixa dependente da carga
    float mix = 0;
    for (int i = 0; i < 5; i++) {
      double inc = params[OSC0 + i].value / sr;
      double ph = phase[i];
      float v;
      if (TYPE[i] == SAW) v = (float)(2 * ph - 1) - polyblep(ph, inc);
      else if (TYPE[i] == SQUARE) {
        v = ph < 0.5 ? 1.f : -1.f;
        v += polyblep(ph, inc);
        v -= polyblep(std::fmod(ph + 0.5, 1.0), inc);
      } else v = (float)(ph < 0.5 ? 4 * ph - 1 : 3 - 4 * ph);
      phase[i] = std::fmod(ph + inc, 1.0);
      mix += v * GAIN[i];
    }
    mix += rasp.process(nz) * params[RASP_GAIN].value;
    float x = std::fmax(-1.f, std::fmin(1.f, mix));
    float fi = (x + 1) * 0.5f * (curve.size() - 1);
    int i0 = (int)fi;
    int i1 = std::min<int>(i0 + 1, (int)curve.size() - 1);
    float shaped = curve[i0] + (curve[i1] - curve[i0]) * (fi - i0);
    float engine = engineLp.process(shaped) * params[ENG_GAIN].value;

    turboPhase = std::fmod(turboPhase + params[TURBO_FREQ].value / sr, 1.0);
    float turbo = (float)std::sin(turboPhase * 2 * M_PI) * params[TURBO_GAIN].value;
    float tire = tireBp.process(nz) * params[TIRE_GAIN].value;
    float wind = windLp.process(nz) * params[WIND_GAIN].value;
    float y = (engine + turbo + tire + wind) * params[MASTER].value;

    float L = 20 * std::log10(std::fmax(std::fabs(y), 1e-6f));
    compEnv += (L - compEnv) * (L > compEnv ? att : rel);
    float gDb = curveDb(compEnv) - compEnv;
    float g = std::pow(10.f, gDb / 20) * makeup;
    y = std::fmax(-1.f, std::fmin(1.f, y * g));
    out[s * 2] = out[s * 2 + 1] = y;
  }
  clock.store(clock.load() + frames / (double)sr);
}
