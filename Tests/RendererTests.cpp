#include "PluginProcessor.h"
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {
int checks = 0;
void require(bool condition, const std::string& message) { ++checks; if (!condition) throw std::runtime_error(message); }

juce::AudioBuffer<float> phrase(double rate, double seconds = 1.0, int channels = 2) {
    juce::AudioBuffer<float> b(channels, static_cast<int>(rate * seconds));
    for (int c = 0; c < channels; ++c)
        for (int n = 0; n < b.getNumSamples(); ++n)
            b.setSample(c, n, static_cast<float>(0.5 * std::sin(n * 2 * juce::MathConstants<double>::pi * (c ? 330 : 220) / rate) * std::exp(-3.0 * n / rate)));
    return b;
}
void bounded(const juce::AudioBuffer<float>& b, const std::string& what) {
    for (int c = 0; c < b.getNumChannels(); ++c)
        for (int n = 0; n < b.getNumSamples(); ++n)
            if (!std::isfinite(b.getSample(c, n)) || std::abs(b.getSample(c, n)) > 1.0f) throw std::runtime_error("Unbounded output: " + what);
    ++checks;
}
double energy(const juce::AudioBuffer<float>& b, int from, int to) {
    double e = 0;
    for (int c = 0; c < b.getNumChannels(); ++c) for (int n = from; n < to; ++n) e += static_cast<double>(b.getSample(c, n)) * b.getSample(c, n);
    return e;
}
template <typename Predicate> bool waitFor(Predicate p) {
    for (int i = 0; i < 600; ++i) { if (p()) return true; juce::Thread::sleep(10); }
    return false;
}
}

int main() {
    juce::ScopedJuceInitialiser_GUI initialise;
    try {
        // FFT convolution against direct convolution.
        {
            const float x[] = { 1, 0, 0 }, h[] = { 1, 2, 3 };
            const auto y = verseb::convolve(x, 3, h, 3);
            require(y.size() == 5 && std::abs(y[0] - 1) < 1e-5f && std::abs(y[2] - 3) < 1e-5f && std::abs(y[4]) < 1e-5f, "Delta convolution");
            juce::Random r(3);
            std::vector<float> a(300), b(77);
            for (auto& v : a) v = r.nextFloat() - 0.5f;
            for (auto& v : b) v = r.nextFloat() - 0.5f;
            const auto fast = verseb::convolve(a.data(), 300, b.data(), 77);
            double worst = 0;
            for (size_t n = 0; n < fast.size(); ++n) {
                double direct = 0;
                for (size_t k = 0; k < b.size(); ++k) if (n >= k && n - k < a.size()) direct += a[n - k] * b[k];
                worst = juce::jmax(worst, std::abs(direct - fast[n]));
            }
            require(worst < 1e-4, "FFT convolution matches direct convolution");
        }
        // Swell: exact length, bounded, rising towards the phrase start, for all rates/types/effects.
        for (double rate : { 44100.0, 48000.0, 96000.0 }) {
            verseb::Settings s;
            s.sampleRate = rate; s.seconds = 1.0;
            for (int type = 0; type < verseb::reverbCount; ++type) {
                s.reverbType = type;
                const auto swell = verseb::renderSwell(phrase(rate), s);
                const int n = swell.getNumSamples();
                require(n == static_cast<int>(std::round(rate)), "Swell length equals the requested time");
                bounded(swell, verseb::reverbNames[type]);
                require(energy(swell, 3 * n / 4, n) > 4 * energy(swell, 0, n / 4), "Swell rises toward the phrase");
                require(std::abs(juce::jmax(swell.getMagnitude(0, 0, n), swell.getMagnitude(1, 0, n)) - 0.891f) < 0.01f, "Peak normalised to -1 dBFS");
            }
            for (int effect = 0; effect < verseb::effectCount; ++effect) {
                verseb::Settings e = s;
                e.reverbType = 0;
                for (int i = 0; i < verseb::effectCount; ++i) e.chain[static_cast<size_t>(i)] = { i, i == effect, 0.8f, 0.7f };
                e.highPass = 200; e.lowPass = 6000;
                const auto swell = verseb::renderSwell(phrase(rate, 0.3, 1), e);
                bounded(swell, verseb::effectNames[effect]);
                require(swell.getMagnitude(0, 0, swell.getNumSamples()) > 0.1f, "Effect keeps signal");
            }
        }
        {
            verseb::Settings s; s.width = 0;
            const auto mono = verseb::renderSwell(phrase(48000), s);
            double difference = 0;
            for (int n = 0; n < mono.getNumSamples(); ++n) difference += std::abs(mono.getSample(0, n) - mono.getSample(1, n));
            require(difference < 1e-3, "Width 0 is mono");
            juce::AudioBuffer<float> empty(2, 0);
            const auto silent = verseb::renderSwell(empty, s);
            require(silent.getMagnitude(0, 0, silent.getNumSamples()) < 1.0e-12f, "No phrase renders silence");
            s.levelDb = -12;
            const auto quiet = verseb::renderSwell(phrase(48000), s);
            require(std::abs(quiet.getMagnitude(0, 0, quiet.getNumSamples()) - 0.891f * 0.2512f) < 0.02f, "Level applies after normalisation");
        }

        // Processor: sync length, pass-through, playback, capture, state and presets.
        {
            VersebProcessor p;
            p.prepareToPlay(48000, 256);
            require(std::abs(p.currentSettings(48000).seconds - 1.0) < 1e-6, "1/2 bar at 120 BPM is one second");
            p.setControlValue("bpm", 90);
            p.setControlValue("division", 4);
            require(std::abs(p.currentSettings(48000).seconds - 4 * 60.0 / 90) < 1e-3, "1 bar at 90 BPM");
            p.setControlValue("sync", 0); p.setControlValue("time", 0.5f);
            require(std::abs(p.currentSettings(48000).seconds - 0.5) < 1e-3, "Free time in seconds");

            juce::MidiBuffer midi;
            juce::AudioBuffer<float> block(2, 256), copy(2, 256);
            for (int n = 0; n < 256; ++n) for (int c = 0; c < 2; ++c) block.setSample(c, n, 0.3f * std::sin(static_cast<float>(n) * 0.05f + static_cast<float>(c)));
            copy.makeCopyOf(block);
            p.processBlock(block, midi);
            double difference = 0;
            for (int c = 0; c < 2; ++c) for (int n = 0; n < 256; ++n) difference += std::abs(block.getSample(c, n) - copy.getSample(c, n));
            require(difference < 1.0e-12, "Audio passes through untouched when not previewing");

            // Capture through processBlock.
            p.startCapture();
            for (int i = 0; i < 100; ++i) { block.makeCopyOf(copy); p.processBlock(block, midi); }
            p.stopCapture();
            p.processBlock(block, midi);
            require(p.pollCapture(), "Capture finishes");
            require(p.sourceLength() == 100 * 256, "Captured length");
            require(waitFor([&p] { auto r = p.getResult(); return r != nullptr && r->swellLength == 24000; }), "Background render after capture");

            p.setPlaying(true);
            require(p.isPlaying(), "Preview starts");
            juce::AudioBuffer<float> silence(2, 256);
            silence.clear();
            p.processBlock(silence, midi);
            p.setPlaying(false);

            p.setSelection(1000, 9000);
            p.setControlValue("reverb", 2);
            p.setOrder({ 4, 3, 2, 1, 0 });
            juce::MemoryBlock state;
            p.getStateInformation(state);
            VersebProcessor restored;
            restored.prepareToPlay(48000, 256);
            restored.setStateInformation(state.getData(), static_cast<int>(state.getSize()));
            require(restored.sourceLength() == p.sourceLength(), "Captured audio restored from project");
            require(restored.getSelection() == juce::Range<int>(1000, 9000), "Selection restored");
            require(static_cast<int>(restored.value("reverb")) == 2, "Parameters restored");
            require(restored.getOrder()[0] == 4 && restored.getOrder()[4] == 0, "Effect order restored");

            restored.loadPreset(1);
            require(restored.currentPreset == "VOCAL SWELL" && restored.value("hp") > 150, "Factory preset loads");
            restored.undo.undo();
            require(restored.value("hp") < 21, "Undo reverts the preset");
        }
    } catch (const std::exception& e) {
        std::cerr << "FAILED after " << checks << " checks: " << e.what() << '\n';
        return 1;
    }
    std::cout << checks << " checks passed\n";
    return 0;
}
