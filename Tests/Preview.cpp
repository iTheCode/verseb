#include "PluginProcessor.h"
#include <juce_audio_utils/juce_audio_utils.h>
#include <cmath>
#include <iostream>

namespace {
// Synthetic sung phrase (syllables with vibrato and formant-ish harmonics) for the screenshot.
juce::AudioBuffer<float> demoPhrase(double rate) {
    const double seconds = 1.68;
    juce::AudioBuffer<float> audio(2, static_cast<int>(rate * seconds));
    audio.clear();
    struct Syllable { double start, length, pitch, level; };
    const Syllable syllables[] = { { 0.0, 0.17, 220, 0.95 }, { 0.19, 0.24, 247, 0.85 }, { 0.46, 0.26, 262, 0.8 },
        { 0.74, 0.27, 247, 0.75 }, { 1.04, 0.28, 220, 0.85 }, { 1.34, 0.32, 196, 0.45 } };
    juce::Random random(7);
    for (auto& s : syllables)
        for (int i = 0; i < static_cast<int>(s.length * rate); ++i) {
            const double t = i / rate, p = t / s.length;
            const double envelope = s.level * std::pow(std::sin(juce::MathConstants<double>::pi * std::pow(p, 0.6)), 0.7) * (0.75 + 0.25 * std::sin(t * 37));
            const double f = s.pitch * (1 + 0.012 * std::sin(2 * juce::MathConstants<double>::pi * 5.5 * t));
            double v = 0;
            for (int h = 1; h <= 8; ++h) v += std::sin(2 * juce::MathConstants<double>::pi * f * h * t) / (h * (h == 3 || h == 4 ? 0.6 : 1.0));
            v = v * 0.3 * envelope + (random.nextFloat() - 0.5f) * 0.02 * envelope;
            const int n = static_cast<int>(s.start * rate) + i;
            if (n < audio.getNumSamples()) for (int c = 0; c < 2; ++c) audio.addSample(c, n, static_cast<float>(v));
        }
    return audio;
}
}

class Preview final : public juce::JUCEApplication {
public:
    const juce::String getApplicationName() override { return "Verseb Preview"; }
    const juce::String getApplicationVersion() override { return "0.1.0"; }
    void initialise(const juce::String& args) override {
        processor = std::make_unique<VersebProcessor>();
        processor->prepareToPlay(48000, 256);
        processor->setSource(demoPhrase(48000), 48000, false);
        // Wait for the background renderer so the screenshot shows the swell.
        for (int i = 0; i < 400; ++i) {
            if (auto r = processor->getResult(); r != nullptr && r->swellLength > 0) break;
            juce::Thread::sleep(10); }
        std::unique_ptr<juce::AudioProcessorEditor> editor(processor->createEditor());
        if (args.contains("--show")) {
            window = std::make_unique<Window>(); window->setContentOwned(editor.release(), true);
            window->centreWithSize(window->getContentComponent()->getWidth(), window->getContentComponent()->getHeight());
            window->setResizable(true, false);
            window->setVisible(true);
            return;
        }
        const auto image = editor->createComponentSnapshot(editor->getLocalBounds(), true, 2.0f);
        auto destination = juce::File::getCurrentWorkingDirectory().getChildFile("docs/images/verseb.png");
        destination.getParentDirectory().createDirectory();
        { juce::FileOutputStream output(destination);
          if (!output.openedOk()) { setApplicationReturnValue(1); quit(); return; }
          output.setPosition(0); output.truncate();
          if (!juce::PNGImageFormat().writeImageToStream(image, output)) { setApplicationReturnValue(1); quit(); return; } }
        std::cout << "Preview: " << destination.getFullPathName() << '\n';
        quit();
    }
    void shutdown() override { window.reset(); processor.reset(); }
private:
    struct Window final : juce::DocumentWindow {
        Window() : DocumentWindow("Verseb / Preview sin audio", juce::Colour(0xff19222f), allButtons) { setUsingNativeTitleBar(true); }
        void closeButtonPressed() override { juce::JUCEApplication::getInstance()->systemRequestedQuit(); }
    };
    std::unique_ptr<VersebProcessor> processor;
    std::unique_ptr<Window> window;
};
START_JUCE_APPLICATION(Preview)
