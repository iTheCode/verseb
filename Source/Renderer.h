#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include <array>
#include <vector>

// Offline engine: renders the reverse-reverb swell that leads into the start of a phrase.
// Method: reverse the phrase onset, convolve it with a synthetic impulse response whose
// length equals the requested swell, keep the tail and reverse it again. The swell ends
// exactly where the phrase begins, so it can be placed immediately before it in the DAW.
namespace verseb {
inline constexpr const char* lengthNames[] = { "1/16 BAR", "1/8 BAR", "1/4 BAR", "1/2 BAR", "1 BAR", "2 BARS", "4 BARS" };
inline constexpr double lengthBars[] = { 0.0625, 0.125, 0.25, 0.5, 1.0, 2.0, 4.0 };
inline constexpr const char* reverbNames[] = { "PLATE", "HALL", "ROOM", "CHAMBER", "SPRING", "CATHEDRAL" };
inline constexpr int reverbCount = 6;
enum Effect { PitchRiser, Saturation, Sweep, Modulation, Tremolo, effectCount };
inline constexpr const char* effectNames[] = { "PITCH RISER", "SATURATION", "SWEEP", "MODULATION", "TREMOLO" };
inline constexpr const char* effectParamNames[][2] = { { "PITCH", "CURVE" }, { "DRIVE", "MIX" },
    { "FROM", "RES" }, { "RATE", "DEPTH" }, { "RATE", "DEPTH" } };
inline constexpr double maxSwellSeconds = 24.0;

struct EffectSlot { int type = 0; bool enabled = false; float a = 0.5f, b = 0.5f; };
struct Settings {
    double sampleRate = 48000, seconds = 1.0;
    int reverbType = 0;
    float shape = 0.5f, width = 1.0f, levelDb = 0.0f;
    float highPass = 0, lowPass = 0;   // Hz; 0 = off
    std::array<EffectSlot, effectCount> chain {};  // processing order
};

// Swell length in samples for the requested duration (capped at maxSwellSeconds).
int swellSamples(const Settings&);
// Impulse response for a reverb type. Deterministic for a given seed.
juce::AudioBuffer<float> makeImpulse(int type, double sampleRate, int length, float shape, unsigned seed = 0x5eb);
// FFT linear convolution, result length nx + nh - 1.
std::vector<float> convolve(const float* x, int nx, const float* h, int nh);
// The full render chain: reverb -> filter -> effects (in order) -> width -> level.
juce::AudioBuffer<float> renderSwell(const juce::AudioBuffer<float>& phrase, const Settings&);
// Display text for the effect parameters.
juce::String effectValueText(int type, int which, float value);
}
