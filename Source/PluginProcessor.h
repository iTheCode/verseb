#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include "Renderer.h"
#include <memory>

class VersebProcessor final : public juce::AudioProcessor, private juce::ValueTree::Listener {
public:
    static constexpr double maxCaptureSeconds = 30.0;
    static constexpr int liveBucket = 256;
    struct Result {
        juce::AudioBuffer<float> swell, preview;  // preview = swell followed by the selected phrase
        double sampleRate = 48000, bpm = 120, seconds = 1;
        int swellLength = 0, division = 3;
        bool sync = true, hostTempo = false;
    };

    VersebProcessor();
    ~VersebProcessor() override;
    void prepareToPlay(double, int) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported(const BusesLayout&) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using juce::AudioProcessor::processBlock;
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "Verseb"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}
    void getStateInformation(juce::MemoryBlock&) override;
    void setStateInformation(const void*, int) override;

    // Parameters
    void setControlValue(const juce::String& id, float value);
    float value(const char* id) const { return parameters.getRawParameterValue(id)->load(); }
    static juce::String effectId(int effect, const char* suffix);
    std::array<int, verseb::effectCount> getOrder() const;
    void setOrder(const std::array<int, verseb::effectCount>&);
    verseb::Settings currentSettings(double sampleRate) const;
    double currentBpm() const;
    double beatsPerBar() const { return barBeats.load(); }

    // Capture / source audio (message thread)
    void startCapture();
    void stopCapture();
    bool isCapturing() const { return captureState.load() != 0; }
    bool pollCapture();  // true when a finished capture was moved to the source
    int liveCaptureLength() const { return captureLength.load(); }
    float livePeak(int bucket) const { return livePeaks[static_cast<size_t>(bucket)].load(); }
    int liveBucketCount() const { return static_cast<int>(livePeaks.size()); }
    double captureRate() const { return hostRate.load(); }
    bool loadAudioFile(const juce::File&);
    void setSource(juce::AudioBuffer<float>, double rate, bool trimToOnset);
    void clearSource();
    int sourceLength() const { return sourceSamples.load(); }
    double sourceRate() const { return sourceSampleRate.load(); }
    int sourceVersion() const { return sourceCounter.load(); }
    std::vector<std::pair<float, float>> sourcePeaks(int columns) const;
    void setSelection(int start, int end);
    juce::Range<int> getSelection() const { return { selectionStart.load(), selectionEnd.load() }; }

    // Rendering / playback
    std::shared_ptr<const Result> getResult() const;
    int resultVersion() const { return resultCounter.load(); }
    void setPlaying(bool);
    bool isPlaying() const { return playing.load(); }
    std::atomic<bool> looping { false };
    double playSeconds() const { return playTime.load(); }
    bool writeWav(const juce::File&, bool includePhrase) const;
    juce::File exportForDrag() const;

    // Presets
    juce::StringArray presetNames() const;
    int numFactoryPresets() const;
    void loadPreset(int index);
    bool saveUserPreset(const juce::String& name);
    static juce::File presetFolder();
    juce::String currentPreset { "DEFAULT" };

    juce::UndoManager undo;
    juce::AudioProcessorValueTreeState parameters;

private:
    class RenderThread;
    static juce::AudioProcessorValueTreeState::ParameterLayout layout();
    void valueTreePropertyChanged(juce::ValueTree&, const juce::Identifier&) override;
    void valueTreeRedirected(juce::ValueTree&) override;
    void syncOrderFromTree();
    void renderIfNeeded();
    void applyPresetXml(const juce::XmlElement&);

    std::unique_ptr<RenderThread> renderer;
    std::array<std::atomic<int>, verseb::effectCount> order {};
    std::atomic<double> hostRate { 48000 }, hostBpm { 0 }, barBeats { 4 }, playTime { 0 };

    // Capture written only by the audio thread while captureState == 1.
    juce::AudioBuffer<float> captureBuffer;
    std::vector<std::atomic<float>> livePeaks;
    std::atomic<int> captureState { 0 }, captureLength { 0 };  // 0 idle, 1 recording, 2 stop requested
    std::atomic<bool> captureReady { false };

    mutable juce::CriticalSection sourceLock;
    juce::AudioBuffer<float> source;
    std::atomic<double> sourceSampleRate { 48000 };
    std::atomic<int> sourceSamples { 0 }, sourceCounter { 0 }, selectionStart { 0 }, selectionEnd { 0 };

    mutable juce::SpinLock resultLock;
    std::shared_ptr<const Result> result;
    std::atomic<int> resultCounter { 0 };
    std::atomic<bool> playing { false };
    double playPosition = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(VersebProcessor)
};
