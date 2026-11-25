#pragma once
#include "PluginProcessor.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>

namespace ui {
struct Fonts {
    Fonts();
    juce::Font sans(float height, bool bold = true, float tracking = 0.1f) const;
    juce::Font mono(float height, bool medium = false, float tracking = 0.08f) const;
    juce::Typeface::Ptr bold, semi, mono400, mono500;
};

class Look final : public juce::LookAndFeel_V4 {
public:
    Look();
    void drawRotarySlider(juce::Graphics&, int, int, int, int, float, float, float, juce::Slider&) override;
    juce::Font getPopupMenuFont() override;
    juce::Font getAlertWindowMessageFont() override;
    juce::Font getAlertWindowTitleFont() override;
    juce::Font getTextButtonFont(juce::TextButton&, int) override;
    juce::SharedResourcePointer<Fonts> fonts;
};

enum class Icon { none, play, stop, loop, record, undo, redo, menu, led };
// Flat raised button matching the reference: optional icon and tracked caps label.
class PillButton final : public juce::Button {
public:
    PillButton(const juce::String& text, Icon icon);
    void paintButton(juce::Graphics&, bool over, bool down) override;
    Icon icon;
    bool lit = false, recording = false;
private:
    juce::SharedResourcePointer<Fonts> fonts;
};

// "‹ VALUE ›" box used for presets and reverb type.
class Selector final : public juce::Component {
public:
    void paint(juce::Graphics&) override;
    void mouseUp(const juce::MouseEvent&) override;
    void mouseMove(const juce::MouseEvent&) override { repaint(); }
    void mouseExit(const juce::MouseEvent&) override { repaint(); }
    void setText(const juce::String& t) { if (t != text) { text = t; repaint(); } }
    std::function<void(int)> onStep;
    std::function<void()> onClick;
    float textSize = 13;
private:
    juce::String text;
    juce::SharedResourcePointer<Fonts> fonts;
};

// Bar-style waveform for the captured audio, with a draggable selection.
class CaptureView final : public juce::Component {
public:
    explicit CaptureView(VersebProcessor&);
    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;
    void mouseMove(const juce::MouseEvent&) override;
    void mouseDoubleClick(const juce::MouseEvent&) override;
    void refresh();
    bool dropHighlight = false;
private:
    juce::Rectangle<float> waveArea() const;
    int sampleAt(float x) const;
    float xFor(int sample) const;
    VersebProcessor& processor;
    std::vector<std::pair<float, float>> peaks;
    enum class Drag { none, create, start, end, move } drag = Drag::none;
    int anchor = 0, grabOffset = 0;
    juce::SharedResourcePointer<Fonts> fonts;
};

// Rendered reverse reverb; drag it into the DAW timeline.
class SwellView final : public juce::Component {
public:
    explicit SwellView(VersebProcessor&);
    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;
    void mouseMove(const juce::MouseEvent&) override;
    void refresh();
    juce::Rectangle<int> bpmArea() const;
    juce::Rectangle<int> gripArea() const;
private:
    VersebProcessor& processor;
    std::vector<float> peaks;
    std::shared_ptr<const VersebProcessor::Result> result;
    bool draggingBpm = false, exported = false;
    float bpmAtDown = 120;
    juce::SharedResourcePointer<Fonts> fonts;
};

class FilterGraph final : public juce::Component, private juce::AudioProcessorValueTreeState::Listener {
public:
    explicit FilterGraph(VersebProcessor&);
    ~FilterGraph() override;
    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;
    void mouseDoubleClick(const juce::MouseEvent&) override;
    void mouseMove(const juce::MouseEvent&) override;
    void mouseExit(const juce::MouseEvent&) override { hover = -1; repaint(); }
    int handleAt(juce::Point<float>) const;
private:
    void parameterChanged(const juce::String&, float) override { juce::MessageManager::callAsync([safe = juce::Component::SafePointer<FilterGraph>(this)] { if (safe) safe->repaint(); }); }
    juce::Rectangle<float> plot() const;
    float xFor(float hz) const;
    float hzFor(float x) const;
    float responseDb(float hz) const;
    VersebProcessor& processor;
    int dragging = -1, hover = -1;
    juce::SharedResourcePointer<Fonts> fonts;
};

class EffectsList final : public juce::Component {
public:
    explicit EffectsList(VersebProcessor&);
    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;
    int rowAt(float y) const;
    std::function<void(int)> onSelect;
    int selected = verseb::PitchRiser;
private:
    juce::Rectangle<float> rowBounds(int row) const;
    VersebProcessor& processor;
    int dragRow = -1, dropRow = -1;
    bool reordering = false;
    juce::SharedResourcePointer<Fonts> fonts;
};
}

class VersebEditor final : public juce::AudioProcessorEditor, public juce::FileDragAndDropTarget, private juce::Timer {
public:
    explicit VersebEditor(VersebProcessor&);
    ~VersebEditor() override;
    void resized() override;
    void mouseDown(const juce::MouseEvent&) override;
    bool isInterestedInFileDrag(const juce::StringArray&) override;
    void fileDragEnter(const juce::StringArray&, int, int) override;
    void fileDragExit(const juce::StringArray&) override;
    void filesDropped(const juce::StringArray&, int, int) override;
private:
    struct Content final : juce::Component {
        explicit Content(VersebEditor& e) : editor(e) {}
        void paint(juce::Graphics&) override;
        VersebEditor& editor;
    };
    void timerCallback() override;
    void configureKnob(juce::Slider&, const juce::String& help);
    void attachLengthKnob();
    void selectEffect(int);
    void showMenu();
    void showPresetMenu();
    void savePreset();
    void updateHelp();
    juce::String lengthText() const;
    void help(juce::Component&, const juce::String&);

    VersebProcessor& owner;
    ui::Look look;
    juce::SharedResourcePointer<ui::Fonts> fonts;
    juce::Image wallpaper;
    Content content { *this };
    ui::PillButton undoButton { "", ui::Icon::undo }, redoButton { "", ui::Icon::redo }, saveButton { "SAVE", ui::Icon::none }, menuButton { "", ui::Icon::menu };
    ui::PillButton playButton { "PLAY", ui::Icon::play }, loopButton { "LOOP", ui::Icon::loop }, captureButton { "CAPTURE", ui::Icon::record };
    ui::PillButton syncButton { "SYNC", ui::Icon::led }, timeButton { "TIME", ui::Icon::led };
    ui::Selector presetSelector, reverbSelector;
    ui::CaptureView captureView;
    ui::SwellView swellView;
    ui::FilterGraph filterGraph;
    ui::EffectsList effectsList;
    juce::Slider lengthKnob, shapeKnob, widthKnob, levelKnob, effectKnobA, effectKnobB;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> lengthAttachment, shapeAttachment, widthAttachment, levelAttachment, effectAttachmentA, effectAttachmentB;
    std::unique_ptr<juce::FileChooser> chooser;
    juce::String helpText;
    int shownSource = -1, shownResult = -1, selectedEffect = verseb::PitchRiser;
    bool lengthSynced = true, wasCapturing = false;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(VersebEditor)
};
