#include "Renderer.h"
#include <juce_dsp/juce_dsp.h>
#include <cmath>
#include <complex>

namespace verseb {
namespace {
constexpr double pi = juce::MathConstants<double>::pi;

struct Character {
    double predelayMs, attackMs, brightStart, brightEnd, lowCut;
    int reflections; double reflectionMs, reflectionGain, combMs, combFeedback;
};
// PLATE, HALL, ROOM, CHAMBER, SPRING, CATHEDRAL
constexpr Character characters[] = {
    { 0, 3, 15000, 5200, 60, 0, 0, 0, 0, 0 },
    { 18, 38, 10000, 2600, 45, 8, 90, 0.55, 0, 0 },
    { 4, 6, 8000, 3000, 90, 12, 38, 0.85, 0, 0 },
    { 9, 18, 9000, 3600, 70, 12, 60, 0.6, 0, 0 },
    { 0, 2, 5600, 2300, 120, 0, 0, 0, 31, 0.55 },
    { 35, 95, 7200, 1500, 38, 6, 140, 0.4, 0, 0 },
};

float pitchExponent(float curve) { return static_cast<float>(0.35 * std::pow(3.0 / 0.35, static_cast<double>(curve))); }
double sweepFrom(float a) { return 60.0 * std::pow(8000.0 / 60.0, static_cast<double>(a)); }
double rateHz(int type, float a) { return type == Modulation ? 0.1 * std::pow(60.0, static_cast<double>(a)) : std::pow(16.0, static_cast<double>(a)); }

void pitchRiser(juce::AudioBuffer<float>& buffer, float amount, float curve) {
    const int n = buffer.getNumSamples();
    const double semitones = 24.0 * amount, exponent = pitchExponent(curve);
    if (semitones <= 0.01 || n < 2) return;
    std::vector<float> source(static_cast<size_t>(n));
    for (int c = 0; c < buffer.getNumChannels(); ++c) {
        auto* data = buffer.getWritePointer(c);
        std::copy(data, data + n, source.begin());
        // Read backwards from the end so the riser lands at original pitch on the phrase.
        double position = n - 1;
        for (int i = n - 1; i >= 0; --i) {
            const double progress = static_cast<double>(i) / n;
            const auto index = static_cast<int>(std::floor(position));
            const auto fraction = static_cast<float>(position - index);
            float value = 0;
            if (index >= 0 && index + 1 < n) value = source[static_cast<size_t>(index)] + fraction * (source[static_cast<size_t>(index + 1)] - source[static_cast<size_t>(index)]);
            else if (index == n - 1) value = source[static_cast<size_t>(index)];
            data[i] = value;
            position -= std::pow(2.0, -semitones * std::pow(1.0 - progress, exponent) / 12.0);
        }
    }
}

void saturation(juce::AudioBuffer<float>& buffer, float drive, float mix) {
    const float gain = juce::Decibels::decibelsToGain(30.0f * drive), norm = std::tanh(gain);
    for (int c = 0; c < buffer.getNumChannels(); ++c) {
        auto* data = buffer.getWritePointer(c);
        for (int i = 0; i < buffer.getNumSamples(); ++i)
            data[i] += mix * (std::tanh(gain * data[i]) / norm - data[i]);
    }
}

void sweep(juce::AudioBuffer<float>& buffer, double sampleRate, float from, float resonance) {
    const int n = buffer.getNumSamples();
    const double start = sweepFrom(from), top = juce::jmin(18000.0, sampleRate * 0.45), q = 0.6 + resonance * 7.0;
    for (int c = 0; c < buffer.getNumChannels(); ++c) {
        auto* data = buffer.getWritePointer(c);
        double ic1 = 0, ic2 = 0;  // TPT state-variable low-pass
        for (int i = 0; i < n; ++i) {
            const double progress = static_cast<double>(i) / n;
            const double cutoff = start * std::pow(top / start, std::pow(progress, 1.6));
            const double g = std::tan(pi * cutoff / sampleRate), k = 1.0 / q;
            const double a1 = 1.0 / (1.0 + g * (g + k)), a2 = g * a1, a3 = g * a2;
            const double v3 = data[i] - ic2, v1 = a1 * ic1 + a2 * v3, v2 = ic2 + a2 * ic1 + a3 * v3;
            ic1 = 2 * v1 - ic1; ic2 = 2 * v2 - ic2;
            data[i] = static_cast<float>(v2);
        }
    }
}

void modulation(juce::AudioBuffer<float>& buffer, double sampleRate, float rate, float depth) {
    const int n = buffer.getNumSamples(), size = static_cast<int>(sampleRate * 0.04) + 4;
    const double hz = rateHz(Modulation, rate);
    std::vector<float> line(static_cast<size_t>(size));
    for (int c = 0; c < buffer.getNumChannels(); ++c) {
        auto* data = buffer.getWritePointer(c);
        std::fill(line.begin(), line.end(), 0.0f);
        int write = 0;
        for (int i = 0; i < n; ++i) {
            line[static_cast<size_t>(write)] = data[i];
            const double delay = sampleRate * (0.009 + 0.0065 * depth * std::sin(2 * pi * hz * i / sampleRate + c * pi * 0.5));
            double read = write - delay;
            while (read < 0) read += size;
            const auto index = static_cast<int>(read);
            const auto fraction = static_cast<float>(read - index);
            const float a = line[static_cast<size_t>(index)], b = line[static_cast<size_t>((index + 1) % size)];
            data[i] = (data[i] + depth * (a + fraction * (b - a))) / (1.0f + 0.35f * depth);
            write = (write + 1) % size;
        }
    }
}

void tremolo(juce::AudioBuffer<float>& buffer, double sampleRate, float rate, float depth) {
    const double hz = rateHz(Tremolo, rate);
    for (int c = 0; c < buffer.getNumChannels(); ++c) {
        auto* data = buffer.getWritePointer(c);
        for (int i = 0; i < buffer.getNumSamples(); ++i)
            data[i] *= static_cast<float>(1.0 - depth * (0.5 - 0.5 * std::cos(2 * pi * hz * i / sampleRate)));
    }
}

void filter(juce::AudioBuffer<float>& buffer, double sampleRate, float highPass, float lowPass) {
    const double nyquistSafe = sampleRate * 0.49;
    for (int c = 0; c < buffer.getNumChannels(); ++c) {
        // Two biquads = 24 dB/octave Butterworth.
        for (const double q : { 0.5412, 1.3066 }) {
            if (highPass > 0) {
                juce::IIRFilter f; f.setCoefficients(juce::IIRCoefficients::makeHighPass(sampleRate, juce::jmin(static_cast<double>(highPass), nyquistSafe), q));
                f.processSamples(buffer.getWritePointer(c), buffer.getNumSamples());
            }
            if (lowPass > 0 && lowPass < nyquistSafe) {
                juce::IIRFilter f; f.setCoefficients(juce::IIRCoefficients::makeLowPass(sampleRate, lowPass, q));
                f.processSamples(buffer.getWritePointer(c), buffer.getNumSamples());
            }
        }
    }
}
}

int swellSamples(const Settings& s) {
    return juce::jmax(16, static_cast<int>(std::round(juce::jlimit(0.01, maxSwellSeconds, s.seconds) * s.sampleRate)));
}

std::vector<float> convolve(const float* x, int nx, const float* h, int nh) {
    if (nx <= 0 || nh <= 0) return {};
    const int length = nx + nh - 1;
    int order = 1;
    while ((1 << order) < length) ++order;
    const int size = 1 << order;
    juce::dsp::FFT fft(order);
    std::vector<float> a(static_cast<size_t>(size) * 2, 0.0f), b(static_cast<size_t>(size) * 2, 0.0f);
    std::copy(x, x + nx, a.begin());
    std::copy(h, h + nh, b.begin());
    fft.performRealOnlyForwardTransform(a.data());
    fft.performRealOnlyForwardTransform(b.data());
    auto* ca = reinterpret_cast<std::complex<float>*>(a.data());
    auto* cb = reinterpret_cast<std::complex<float>*>(b.data());
    for (int i = 0; i <= size / 2; ++i) ca[i] *= cb[i];
    fft.performRealOnlyInverseTransform(a.data());
    a.resize(static_cast<size_t>(length));
    return a;
}

juce::AudioBuffer<float> makeImpulse(int type, double sampleRate, int length, float shape, unsigned seed) {
    const auto& k = characters[juce::jlimit(0, reverbCount - 1, type)];
    juce::AudioBuffer<float> ir(2, juce::jmax(1, length));
    ir.clear();
    const double exponent = 0.45 * std::pow(2.2 / 0.45, static_cast<double>(shape));
    const int predelay = juce::jmin(length / 4, static_cast<int>(k.predelayMs * 0.001 * sampleRate));
    const int body = juce::jmax(1, length - predelay);
    for (int c = 0; c < 2; ++c) {
        juce::Random random(static_cast<juce::int64>(seed) * 7919 + c * 104729 + type * 13);
        auto* data = ir.getWritePointer(c);
        double lowState = 0, highState = 0, lowCoefficient = 0;
        const double highCoefficient = std::exp(-2 * pi * k.lowCut / sampleRate);
        for (int i = 0; i < body; ++i) {
            const double t = static_cast<double>(i) / body;
            if ((i & 31) == 0) {
                const double cutoff = juce::jmin(k.brightStart * std::pow(k.brightEnd / k.brightStart, t), sampleRate * 0.45);
                lowCoefficient = std::exp(-2 * pi * cutoff / sampleRate);
            }
            const double noise = random.nextDouble() * 2 - 1;
            lowState = noise + lowCoefficient * (lowState - noise);
            highState = highCoefficient * highState + (1 - highCoefficient) * lowState;
            const double envelope = std::pow(10.0, -3.0 * std::pow(t, exponent))
                * (1 - std::exp(-(i / sampleRate) * 1000.0 / juce::jmax(0.5, k.attackMs)))
                * (t > 0.96 ? 0.5 + 0.5 * std::cos(pi * (t - 0.96) / 0.04) : 1.0);
            data[predelay + i] = static_cast<float>((lowState - highState) * envelope);
        }
        for (int r = 0; r < k.reflections; ++r) {
            const auto position = predelay + static_cast<int>(random.nextDouble() * k.reflectionMs * 0.001 * sampleRate);
            if (position < length) data[position] += static_cast<float>(k.reflectionGain * (random.nextBool() ? 1 : -1) * (1.0 - 0.6 * r / juce::jmax(1, k.reflections)));
        }
        if (k.combMs > 0) {
            // Spring: dispersive allpass chain followed by a metallic comb.
            for (int stage = 0; stage < 12; ++stage) {
                float x1 = 0, y1 = 0;
                const float g = -0.68f;
                for (int i = 0; i < length; ++i) {
                    const float y = g * data[i] + x1 - g * y1;
                    x1 = data[i]; y1 = y; data[i] = y;
                }
            }
            const int delay = static_cast<int>(k.combMs * 0.001 * sampleRate);
            for (int i = delay; i < length; ++i) data[i] += static_cast<float>(k.combFeedback) * data[i - delay];
        }
        double energy = 0;
        for (int i = 0; i < length; ++i) energy += static_cast<double>(data[i]) * data[i];
        if (energy > 0) juce::FloatVectorOperations::multiply(data, static_cast<float>(1.0 / std::sqrt(energy)), length);
    }
    return ir;
}

juce::AudioBuffer<float> renderSwell(const juce::AudioBuffer<float>& phrase, const Settings& s) {
    const int length = swellSamples(s);
    juce::AudioBuffer<float> out(2, length);
    out.clear();
    const int onset = juce::jmin(length, phrase.getNumSamples());
    if (onset <= 0 || phrase.getNumChannels() == 0) return out;
    const auto ir = makeImpulse(s.reverbType, s.sampleRate, length, s.shape);
    std::vector<float> reversed(static_cast<size_t>(onset));
    for (int c = 0; c < 2; ++c) {
        const auto* source = phrase.getReadPointer(juce::jmin(c, phrase.getNumChannels() - 1));
        for (int i = 0; i < onset; ++i) reversed[static_cast<size_t>(i)] = source[onset - 1 - i];
        const auto wet = convolve(reversed.data(), onset, ir.getReadPointer(c), length);
        auto* data = out.getWritePointer(c);
        // Keep only the tail after the reversed onset; reversing it puts its end at the phrase start.
        for (int i = 0; i < length; ++i) {
            const auto index = static_cast<size_t>(onset + (length - 1 - i));
            data[i] = index < wet.size() ? wet[index] : 0.0f;
        }
    }
    filter(out, s.sampleRate, s.highPass, s.lowPass);
    for (const auto& slot : s.chain) {
        if (!slot.enabled) continue;
        switch (slot.type) {
            case PitchRiser: pitchRiser(out, slot.a, slot.b); break;
            case Saturation: saturation(out, slot.a, slot.b); break;
            case Sweep: sweep(out, s.sampleRate, slot.a, slot.b); break;
            case Modulation: modulation(out, s.sampleRate, slot.a, slot.b); break;
            case Tremolo: tremolo(out, s.sampleRate, slot.a, slot.b); break;
            default: break;
        }
    }
    auto* left = out.getWritePointer(0);
    auto* right = out.getWritePointer(1);
    for (int i = 0; i < length; ++i) {
        const float mid = 0.5f * (left[i] + right[i]), side = 0.5f * (left[i] - right[i]) * s.width;
        left[i] = mid + side; right[i] = mid - side;
    }
    const float peak = juce::jmax(out.getMagnitude(0, 0, length), out.getMagnitude(1, 0, length));
    if (peak > 1.0e-9f) out.applyGain(0.891f / peak * juce::Decibels::decibelsToGain(s.levelDb, -100.0f));
    for (int c = 0; c < 2; ++c) juce::FloatVectorOperations::clip(out.getWritePointer(c), out.getReadPointer(c), -1.0f, 1.0f, length);
    return out;
}

juce::String effectValueText(int type, int which, float v) {
    if (which == 1) {
        if (type == Sweep) return "Q " + juce::String(0.6 + v * 7.0, 1);
        return juce::String(juce::roundToInt(v * 100)) + " %";
    }
    switch (type) {
        case PitchRiser: return "-" + juce::String(24.0 * v, 1) + " st";
        case Saturation: return juce::String(30.0 * v, 1) + " dB";
        case Sweep: { const auto hz = sweepFrom(v); return hz >= 1000 ? juce::String(hz / 1000, 2) + " kHz" : juce::String(juce::roundToInt(hz)) + " Hz"; }
        default: return juce::String(rateHz(type, v), 2) + " Hz";
    }
}
}
