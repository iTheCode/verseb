#include "PluginEditor.h"
#include <BinaryData.h>
#include <cmath>

namespace {
const juce::Colour navy(0xff19222f), panel(0xff1d2735), panelEdge(0xff2a3646), display(0xff050b12), displayEdge(0xff1a2533),
    accent(0xff4db2ec), accentBright(0xff86cdf4), fgText(0xffdde5ee), label(0xffa9b5c3), muted(0xff7c899a), divider(0xff2c3847),
    buttonFill(0xff263142), buttonTop(0xff313e50), caption(0xff74b3d9), recordRed(0xffff5a5a);
constexpr int baseWidth = 890, baseHeight = 616;
const juce::Rectangle<int> lengthReadout { 38, 472, 136, 27 }, helpBar { 18, 557, 854, 43 }, effectTitle { 700, 372, 200, 132 };

juce::String hzText(float hz) {
    return hz >= 1000 ? juce::String(hz / 1000.0f, hz >= 10000 ? 1 : 2) + " kHz" : juce::String(juce::roundToInt(hz)) + " Hz";
}
void tracked(juce::Graphics& g, const juce::Font& font, const juce::String& s, juce::Rectangle<int> r, juce::Colour c,
             juce::Justification j = juce::Justification::centredLeft) {
    g.setFont(font); g.setColour(c); g.drawText(s, r, j, false);
}
void displayBox(juce::Graphics& g, juce::Rectangle<float> r, float alpha = 0.9f) {
    g.setColour(display.withAlpha(alpha)); g.fillRoundedRectangle(r, 4);
    g.setColour(displayEdge); g.drawRoundedRectangle(r.reduced(0.5f), 4, 1);
}
void chevron(juce::Graphics& g, juce::Point<float> c, bool right, juce::Colour colour) {
    juce::Path p;
    const float s = right ? 1.0f : -1.0f;
    p.startNewSubPath(c.x - 2 * s, c.y - 4); p.lineTo(c.x + 2 * s, c.y); p.lineTo(c.x - 2 * s, c.y + 4);
    g.setColour(colour); g.strokePath(p, juce::PathStrokeType(1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}
void grip(juce::Graphics& g, float x, float y, juce::Colour c) {
    g.setColour(c);
    for (int col = 0; col < 2; ++col) for (int row = 0; row < 3; ++row) g.fillEllipse(x + static_cast<float>(col) * 3.5f, y + static_cast<float>(row) * 3.5f, 1.6f, 1.6f);
}
}

namespace ui {
// ---------------------------------------------------------------- fonts and look
Fonts::Fonts()
    : bold(juce::Typeface::createSystemTypefaceFor(BinaryData::BarlowBold_ttf, BinaryData::BarlowBold_ttfSize)),
      semi(juce::Typeface::createSystemTypefaceFor(BinaryData::BarlowSemiBold_ttf, BinaryData::BarlowSemiBold_ttfSize)),
      mono400(juce::Typeface::createSystemTypefaceFor(BinaryData::IBMPlexMonoRegular_ttf, BinaryData::IBMPlexMonoRegular_ttfSize)),
      mono500(juce::Typeface::createSystemTypefaceFor(BinaryData::IBMPlexMonoMedium_ttf, BinaryData::IBMPlexMonoMedium_ttfSize)) {}
juce::Font Fonts::sans(float height, bool isBold, float tracking) const {
    return juce::Font(juce::FontOptions(isBold ? bold : semi).withHeight(height)).withExtraKerningFactor(tracking);
}
juce::Font Fonts::mono(float height, bool medium, float tracking) const {
    return juce::Font(juce::FontOptions(medium ? mono500 : mono400).withHeight(height)).withExtraKerningFactor(tracking);
}

Look::Look() {
    setColour(juce::PopupMenu::backgroundColourId, juce::Colour(0xff111924));
    setColour(juce::PopupMenu::textColourId, fgText);
    setColour(juce::PopupMenu::headerTextColourId, accent);
    setColour(juce::PopupMenu::highlightedBackgroundColourId, juce::Colour(0xff1f3a52));
    setColour(juce::PopupMenu::highlightedTextColourId, accentBright);
    setColour(juce::AlertWindow::backgroundColourId, juce::Colour(0xff1a2331));
    setColour(juce::AlertWindow::textColourId, fgText);
    setColour(juce::AlertWindow::outlineColourId, panelEdge);
    setColour(juce::TextEditor::backgroundColourId, display);
    setColour(juce::TextEditor::textColourId, accentBright);
    setColour(juce::TextEditor::outlineColourId, panelEdge);
    setColour(juce::TextEditor::focusedOutlineColourId, accent);
    setColour(juce::TextButton::buttonColourId, buttonFill);
    setColour(juce::TextButton::textColourOffId, fgText);
    setColour(juce::BubbleComponent::backgroundColourId, juce::Colour(0xf0080e16));
    setColour(juce::BubbleComponent::outlineColourId, panelEdge);
    setColour(juce::TooltipWindow::textColourId, accentBright);
}
juce::Font Look::getPopupMenuFont() { return fonts->sans(14, false, 0.06f); }
juce::Font Look::getAlertWindowMessageFont() { return fonts->sans(14, false, 0.02f); }
juce::Font Look::getAlertWindowTitleFont() { return fonts->sans(17, true, 0.1f); }
juce::Font Look::getTextButtonFont(juce::TextButton&, int) { return fonts->sans(13, true, 0.1f); }

void Look::drawRotarySlider(juce::Graphics& g, int x, int y, int w, int h, float position, float start, float end, juce::Slider&) {
    const auto bounds = juce::Rectangle<float>(static_cast<float>(x), static_cast<float>(y), static_cast<float>(w), static_cast<float>(h));
    const float radius = juce::jmin(bounds.getWidth(), bounds.getHeight()) * 0.5f - 2.0f;
    const auto c = bounds.getCentre();
    const float ring = juce::jmax(3.0f, radius * 0.11f), ringRadius = radius - ring * 0.5f;
    const float angle = start + position * (end - start);
    // Recessed dark well and track
    g.setColour(juce::Colour(0xff0b1119)); g.fillEllipse(c.x - radius, c.y - radius, radius * 2, radius * 2);
    juce::Path track; track.addCentredArc(c.x, c.y, ringRadius, ringRadius, 0, start, end, true);
    g.setColour(juce::Colour(0xff141d29)); g.strokePath(track, juce::PathStrokeType(ring, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    if (position > 0.001f) {
        juce::Path arc; arc.addCentredArc(c.x, c.y, ringRadius, ringRadius, 0, start, angle, true);
        g.setColour(accent.withAlpha(0.18f)); g.strokePath(arc, juce::PathStrokeType(ring + 3, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setColour(accent); g.strokePath(arc, juce::PathStrokeType(ring, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }
    // Knob body
    const float body = radius - ring - juce::jmax(2.0f, radius * 0.07f);
    g.setColour(juce::Colours::black.withAlpha(0.45f)); g.fillEllipse(c.x - body, c.y - body + 2.5f, body * 2, body * 2);
    g.setGradientFill(juce::ColourGradient(juce::Colour(0xff334054), c.x, c.y - body, juce::Colour(0xff161e2a), c.x, c.y + body, false));
    g.fillEllipse(c.x - body, c.y - body, body * 2, body * 2);
    g.setColour(juce::Colour(0xff415066)); g.drawEllipse(c.x - body + 0.5f, c.y - body + 0.5f, body * 2 - 1, body * 2 - 1, 1);
    // Indicator dot
    const float dot = juce::jmax(2.0f, body * 0.11f), distance = body * 0.66f;
    g.setColour(juce::Colour(0xffeef3f8));
    g.fillEllipse(c.x + std::sin(angle) * distance - dot, c.y - std::cos(angle) * distance - dot, dot * 2, dot * 2);
}

// ---------------------------------------------------------------- buttons
PillButton::PillButton(const juce::String& t, Icon i) : juce::Button(t), icon(i) { setMouseCursor(juce::MouseCursor::PointingHandCursor); }

void PillButton::paintButton(juce::Graphics& g, bool over, bool down) {
    auto r = getLocalBounds().toFloat().reduced(0.5f);
    const bool pressed = down || (lit && icon != Icon::led);
    g.setColour(juce::Colours::black.withAlpha(0.35f)); g.fillRoundedRectangle(r.translated(0, 1.5f), 4);
    g.setGradientFill(juce::ColourGradient(pressed ? juce::Colour(0xff1b2533) : over ? buttonTop.brighter(0.08f) : buttonTop, 0, r.getY(),
                                           pressed ? juce::Colour(0xff1f2a39) : buttonFill, 0, r.getBottom(), false));
    g.fillRoundedRectangle(r, 4);
    g.setColour(pressed ? accent.withAlpha(0.55f) : juce::Colour(0xff36445a)); g.drawRoundedRectangle(r.reduced(0.5f), 4, 1);
    const float alpha = isEnabled() ? 1.0f : 0.35f;
    const auto fg = (pressed ? accentBright : fgText).withAlpha(alpha);
    const auto name = getButtonText();
    const auto font = fonts->sans(13.0f, true, 0.14f);
    const float textWidth = name.isEmpty() ? 0.0f : juce::GlyphArrangement::getStringWidth(font, name);
    const float iconWidth = icon == Icon::none ? 0.0f : 9.0f, gap = name.isEmpty() || icon == Icon::none ? 0.0f : 7.0f;
    float left = r.getCentreX() - (textWidth + iconWidth + gap) * 0.5f;
    const float cy = r.getCentreY();
    juce::Path p;
    switch (icon) {
        case Icon::play: p.addTriangle(left, cy - 4.5f, left, cy + 4.5f, left + 8, cy); g.setColour(fg); g.fillPath(p); break;
        case Icon::stop: g.setColour(fg); g.fillRect(left, cy - 4, 8.0f, 8.0f); break;
        case Icon::record:
            g.setColour(recording ? recordRed.withAlpha(alpha) : juce::Colour(0xff8995a6).withAlpha(alpha));
            if (recording) { g.setColour(recordRed.withAlpha(0.3f)); g.fillEllipse(left - 2.5f, cy - 6.5f, 13, 13); g.setColour(recordRed); }
            g.fillEllipse(left, cy - 4, 8, 8); break;
        case Icon::led:
            g.setColour(lit ? accent : juce::Colour(0xff0d141d)); g.fillEllipse(left + 1, cy - 3, 6, 6);
            if (lit) { g.setColour(accent.withAlpha(0.3f)); g.fillEllipse(left - 1, cy - 5, 10, 10); }
            break;
        case Icon::loop: {
            const float w = 11, h = 8; left -= 1;
            p.startNewSubPath(left + 2, cy + 1); p.lineTo(left + 2, cy - h / 2 + 1); p.lineTo(left + w - 1, cy - h / 2 + 1);
            p.startNewSubPath(left + w - 2, cy - 1); p.lineTo(left + w - 2, cy + h / 2 - 1); p.lineTo(left + 1, cy + h / 2 - 1);
            g.setColour(fg.withMultipliedAlpha(0.85f)); g.strokePath(p, juce::PathStrokeType(1.4f));
            juce::Path heads;
            heads.addTriangle(left + w - 3, cy - h / 2 - 1.5f, left + w - 3, cy - h / 2 + 3.5f, left + w + 0.5f, cy - h / 2 + 1);
            heads.addTriangle(left + 3, cy + h / 2 - 3.5f, left + 3, cy + h / 2 + 1.5f, left - 0.5f, cy + h / 2 - 1);
            g.fillPath(heads); break;
        }
        case Icon::undo: case Icon::redo: {
            const bool mirrored = icon == Icon::redo;
            const float cx = r.getCentreX();
            const auto mx = [&](float v) { return mirrored ? cx - (v - cx) : v; };
            p.startNewSubPath(mx(cx - 4), cy - 3);
            p.lineTo(mx(cx + 1.5f), cy - 3);
            p.quadraticTo(mx(cx + 5), cy - 3, mx(cx + 5), cy + 0.5f);
            p.quadraticTo(mx(cx + 5), cy + 4, mx(cx + 1.5f), cy + 4);
            p.lineTo(mx(cx - 1), cy + 4);
            g.setColour(fg); g.strokePath(p, juce::PathStrokeType(1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            juce::Path head; head.startNewSubPath(mx(cx - 1.5f), cy - 6); head.lineTo(mx(cx - 4.5f), cy - 3); head.lineTo(mx(cx - 1.5f), cy);
            g.strokePath(head, juce::PathStrokeType(1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            return;
        }
        case Icon::menu:
            g.setColour(fg);
            for (int i = -1; i <= 1; ++i) g.fillRoundedRectangle(r.getCentreX() - 6.5f, cy + static_cast<float>(i) * 4.5f - 0.8f, 13, 1.6f, 0.8f);
            return;
        case Icon::none: break;
    }
    if (name.isNotEmpty()) {
        g.setColour(icon == Icon::led ? fgText.withAlpha(alpha) : fg); g.setFont(font);
        g.drawText(name, juce::Rectangle<float>(left + iconWidth + gap, r.getY(), textWidth + 4, r.getHeight()), juce::Justification::centredLeft, false);
    }
}

// ---------------------------------------------------------------- selector
void Selector::paint(juce::Graphics& g) {
    auto r = getLocalBounds().toFloat();
    displayBox(g, r, 0.92f);
    const auto mouse = getMouseXYRelative();
    const bool hot = isMouseOver();
    chevron(g, { 15, r.getCentreY() }, false, accent.withAlpha(hot && mouse.x < 30 ? 1.0f : 0.75f));
    chevron(g, { r.getRight() - 15, r.getCentreY() }, true, accent.withAlpha(hot && mouse.x > getWidth() - 30 ? 1.0f : 0.75f));
    tracked(g, fonts->mono(textSize, true, 0.16f), text, getLocalBounds().reduced(30, 0), accentBright,
            textSize > 13.5f ? juce::Justification::centred : juce::Justification::centredLeft);
}
void Selector::mouseUp(const juce::MouseEvent& e) {
    if (!getLocalBounds().contains(e.getPosition())) return;
    if (e.x < 30) { if (onStep) onStep(-1); }
    else if (e.x > getWidth() - 30) { if (onStep) onStep(1); }
    else if (onClick) onClick();
}

// ---------------------------------------------------------------- captured audio
CaptureView::CaptureView(VersebProcessor& p) : processor(p) {}

juce::Rectangle<float> CaptureView::waveArea() const { return { 14.0f, 25.0f, static_cast<float>(getWidth()) - 28.0f, static_cast<float>(getHeight()) - 33.0f }; }
int CaptureView::sampleAt(float x) const {
    const auto a = waveArea();
    return static_cast<int>(juce::jlimit(0.0f, 1.0f, (x - a.getX()) / a.getWidth()) * static_cast<float>(processor.sourceLength()));
}
float CaptureView::xFor(int sample) const {
    const auto a = waveArea();
    const int length = juce::jmax(1, processor.sourceLength());
    return a.getX() + a.getWidth() * static_cast<float>(sample) / static_cast<float>(length);
}
void CaptureView::refresh() { peaks = processor.sourcePeaks(static_cast<int>(waveArea().getWidth() / 5.0f)); repaint(); }

void CaptureView::paint(juce::Graphics& g) {
    displayBox(g, getLocalBounds().toFloat());
    if (dropHighlight) { g.setColour(accent); g.drawRoundedRectangle(getLocalBounds().toFloat().reduced(1), 4, 2); }
    const bool capturing = processor.isCapturing();
    tracked(g, fonts->mono(9.5f, true, 0.2f), "CAPTURED AUDIO", { 14, 7, 300, 14 }, caption);
    const auto a = waveArea();
    const float cy = a.getCentreY(), half = a.getHeight() * 0.5f;
    const int bars = static_cast<int>(a.getWidth() / 5.0f);
    const float period = a.getWidth() / static_cast<float>(juce::jmax(1, bars));
    const auto bar = [&](int i, float amplitude, juce::Colour c) {
        const float h = juce::jmax(0.8f, amplitude * half);
        g.setColour(c); g.fillRoundedRectangle(a.getX() + static_cast<float>(i) * period + (period - 2.2f) * 0.5f, cy - h, 2.2f, h * 2, 1.0f);
    };
    if (capturing) {
        const double seconds = processor.liveCaptureLength() / processor.captureRate();
        tracked(g, fonts->mono(10, true, 0.08f), "REC  " + juce::String(seconds, 2) + " s", { getWidth() - 160, 7, 146, 14 }, recordRed, juce::Justification::centredRight);
        const int buckets = juce::jmin(processor.liveBucketCount(), processor.liveCaptureLength() / VersebProcessor::liveBucket);
        float top = 0.05f;
        for (int b = 0; b < buckets; ++b) top = juce::jmax(top, processor.livePeak(b));
        // Live view grows across the panel as the capture lengthens (minimum 4 s window).
        const int window = juce::jmax(buckets, static_cast<int>(4.0 * processor.captureRate() / VersebProcessor::liveBucket));
        for (int i = 0; i < bars; ++i) {
            const int from = i * window / bars, to = juce::jmax(from + 1, (i + 1) * window / bars);
            if (from >= buckets) { bar(i, 0, accent.withAlpha(0.25f)); continue; }
            float level = 0;
            for (int b = from; b < juce::jmin(to, buckets); ++b) level = juce::jmax(level, processor.livePeak(b));
            bar(i, juce::jmin(1.0f, level / top * 0.95f), accent);
        }
        return;
    }
    const int length = processor.sourceLength();
    if (length == 0 || peaks.empty()) {
        for (int i = 0; i < bars; ++i) bar(i, 0, accent.withAlpha(0.3f));
        tracked(g, fonts->mono(10, false, 0.12f), "PULSA CAPTURE O ARRASTRA UN AUDIO AQUI", a.toNearestInt().withTrimmedTop(8), muted, juce::Justification::centredTop);
        return;
    }
    const auto selection = processor.getSelection();
    const double rate = processor.sourceRate();
    tracked(g, fonts->mono(10, true, 0.08f), juce::String(selection.getLength() / rate, 2) + " s", { getWidth() - 160, 7, 146, 14 }, caption, juce::Justification::centredRight);
    float top = 0.02f;
    for (auto& [lo, hi] : peaks) top = juce::jmax(top, juce::jmax(-lo, hi));
    const float x0 = xFor(selection.getStart()), x1 = xFor(selection.getEnd());
    for (int i = 0; i < static_cast<int>(peaks.size()) && i < bars; ++i) {
        const auto [lo, hi] = peaks[static_cast<size_t>(i)];
        const float x = a.getX() + (static_cast<float>(i) + 0.5f) * period;
        const bool inside = x >= x0 && x <= x1;
        bar(i, juce::jmin(1.0f, juce::jmax(-lo, hi) / top * 0.97f), inside ? accent : accent.withAlpha(0.25f));
    }
    if (x1 - x0 < a.getWidth() - 1) { g.setColour(accent.withAlpha(0.06f)); g.fillRect(x0, a.getY() - 2, x1 - x0, a.getHeight() + 4); }
    g.setColour(accent.withAlpha(0.7f)); g.fillRect(x1 - 0.75f, a.getY() - 3, 1.5f, a.getHeight() + 6);
    g.fillRect(x1 - 3.0f, a.getY() - 5, 6.0f, 6.0f);
    g.setColour(juce::Colour(0xfff2f6fa)); g.fillRect(x0 - 0.75f, a.getY() - 3, 1.5f, a.getHeight() + 6);
    g.fillRect(x0 - 3.0f, a.getY() - 5, 6.0f, 6.0f);
    if (processor.isPlaying())
        if (auto r = processor.getResult(); r != nullptr) {
            const double phraseTime = processor.playSeconds() - r->swellLength / r->sampleRate;
            if (phraseTime >= 0) {
                const float x = xFor(selection.getStart() + static_cast<int>(phraseTime * rate));
                g.setColour(accentBright); g.fillRect(x - 0.5f, a.getY() - 2, 1.0f, a.getHeight() + 4);
            }
        }
}

void CaptureView::mouseMove(const juce::MouseEvent& e) {
    const auto s = processor.getSelection();
    const bool edge = processor.sourceLength() > 0 && (std::abs(e.position.x - xFor(s.getStart())) < 6 || std::abs(e.position.x - xFor(s.getEnd())) < 6);
    setMouseCursor(edge ? juce::MouseCursor::LeftRightResizeCursor : juce::MouseCursor::IBeamCursor);
}
void CaptureView::mouseDown(const juce::MouseEvent& e) {
    if (processor.sourceLength() == 0 || processor.isCapturing()) { drag = Drag::none; return; }
    const auto s = processor.getSelection();
    const int sample = sampleAt(e.position.x);
    if (std::abs(e.position.x - xFor(s.getStart())) < 6) drag = Drag::start;
    else if (std::abs(e.position.x - xFor(s.getEnd())) < 6) drag = Drag::end;
    else if (e.mods.isShiftDown() && s.contains(sample)) { drag = Drag::move; grabOffset = sample - s.getStart(); }
    else { drag = Drag::create; anchor = sample; }
}
void CaptureView::mouseDrag(const juce::MouseEvent& e) {
    const auto s = processor.getSelection();
    const int sample = sampleAt(e.position.x);
    switch (drag) {
        case Drag::create: if (e.getDistanceFromDragStart() > 2) processor.setSelection(anchor, sample); break;
        case Drag::start: processor.setSelection(juce::jmin(sample, s.getEnd() - 32), s.getEnd()); break;
        case Drag::end: processor.setSelection(s.getStart(), juce::jmax(sample, s.getStart() + 32)); break;
        case Drag::move: {
            const int start = juce::jlimit(0, processor.sourceLength() - s.getLength(), sample - grabOffset);
            processor.setSelection(start, start + s.getLength()); break;
        }
        case Drag::none: break;
    }
    repaint();
}
void CaptureView::mouseUp(const juce::MouseEvent& e) {
    // A plain click moves the start of the selection: the swell is built from the phrase onset.
    if (drag == Drag::create && e.getDistanceFromDragStart() <= 2) {
        const auto s = processor.getSelection();
        processor.setSelection(anchor, juce::jmax(anchor + 32, s.getEnd() > anchor ? s.getEnd() : processor.sourceLength()));
        repaint();
    }
    drag = Drag::none;
}
void CaptureView::mouseDoubleClick(const juce::MouseEvent&) { processor.setSelection(0, processor.sourceLength()); repaint(); }

// ---------------------------------------------------------------- reverse reverb
SwellView::SwellView(VersebProcessor& p) : processor(p) {}

void SwellView::refresh() {
    result = processor.getResult();
    const int bars = (getWidth() - 28) / 5;
    peaks.assign(static_cast<size_t>(juce::jmax(0, bars)), 0.0f);
    if (result != nullptr && result->swellLength > 0 && bars > 0) {
        const int length = result->swellLength;
        for (int i = 0; i < bars; ++i) {
            const int a = static_cast<int>(static_cast<juce::int64>(length) * i / bars);
            const int b = juce::jmax(a + 1, static_cast<int>(static_cast<juce::int64>(length) * (i + 1) / bars));
            for (int c = 0; c < 2; ++c) peaks[static_cast<size_t>(i)] = juce::jmax(peaks[static_cast<size_t>(i)], result->swell.getMagnitude(c, a, juce::jmin(b, length) - a));
        }
    }
    repaint();
}

juce::Rectangle<int> SwellView::bpmArea() const {
    const auto width = juce::GlyphArrangement::getStringWidthInt(fonts->mono(10, true, 0.08f), juce::String(juce::roundToInt(processor.currentBpm())) + " BPM");
    return { getWidth() - 14 - width - 2, 5, width + 4, 18 };
}
juce::Rectangle<int> SwellView::gripArea() const { return { 10, 26, 110, 16 }; }

void SwellView::paint(juce::Graphics& g) {
    displayBox(g, getLocalBounds().toFloat());
    tracked(g, fonts->mono(9.5f, true, 0.2f), "REVERSE REVERB", { 14, 7, 300, 14 }, caption);
    const bool host = processor.currentBpm() > 0 && result != nullptr && result->hostTempo;
    const auto font = fonts->mono(10, true, 0.08f);
    juce::String length;
    if (processor.value("sync") > 0.5f) length = verseb::lengthNames[juce::jlimit(0, 6, static_cast<int>(processor.value("division")))];
    else length = juce::String(processor.value("time"), 2) + " s";
    const juce::String bpm = juce::String(juce::roundToInt(processor.currentBpm())) + " BPM";
    const auto bpmBox = bpmArea();
    tracked(g, font, bpm, bpmBox.withTrimmedRight(2), draggingBpm ? accentBright : caption, juce::Justification::centredRight);
    tracked(g, font, length + juce::String::fromUTF8(" \xc2\xb7 "), { getWidth() - 300, 5, bpmBox.getX() + 2 - (getWidth() - 300), 18 }, caption, juce::Justification::centredRight);
    if (!host && (isMouseOver() && bpmArea().contains(getMouseXYRelative()))) {
        g.setColour(caption.withAlpha(0.6f)); g.fillRect(bpmArea().getX() + 6, bpmArea().getBottom() - 2, bpmArea().getWidth() - 8, 1);
    }
    grip(g, 15, 30, caption);
    tracked(g, fonts->mono(8.5f, true, 0.16f), "DRAG INTO DAW", { 25, 27, 140, 14 }, caption);
    const juce::Rectangle<float> a { 14.0f, 44.0f, static_cast<float>(getWidth()) - 28.0f, static_cast<float>(getHeight()) - 52.0f };
    const float cy = a.getCentreY(), half = a.getHeight() * 0.5f;
    const int bars = static_cast<int>(peaks.size());
    const float period = a.getWidth() / static_cast<float>(juce::jmax(1, bars));
    float top = 0.02f;
    for (auto p : peaks) top = juce::jmax(top, p);
    for (int i = 0; i < bars; ++i) {
        const float h = juce::jmax(0.8f, peaks[static_cast<size_t>(i)] / top * half);
        g.setColour(result != nullptr && result->swellLength > 0 ? juce::Colour(0xff6ec0ee) : accent.withAlpha(0.3f));
        g.fillRoundedRectangle(a.getX() + static_cast<float>(i) * period + (period - 2.2f) * 0.5f, cy - h, 2.2f, h * 2, 1.0f);
    }
    if (result == nullptr || result->swellLength == 0)
        tracked(g, fonts->mono(10, false, 0.12f), "SELECCIONA UN TRAMO PARA GENERAR LA REVERB INVERSA", a.toNearestInt().withTrimmedTop(4), muted, juce::Justification::centredTop);
    if (processor.isPlaying() && result != nullptr && result->swellLength > 0) {
        const double t = processor.playSeconds() * result->sampleRate / result->swellLength;
        if (t <= 1.0) {
            const float x = a.getX() + a.getWidth() * static_cast<float>(t);
            g.setColour(accentBright); g.fillRect(x - 0.5f, a.getY() - 2, 1.0f, a.getHeight() + 4);
        }
    }
}

void SwellView::mouseMove(const juce::MouseEvent& e) {
    const bool bpm = bpmArea().contains(e.getPosition()) && processor.value("sync") > 0.5f;
    setMouseCursor(bpm ? juce::MouseCursor::UpDownResizeCursor : juce::MouseCursor::DraggingHandCursor);
    repaint(bpmArea());
}
void SwellView::mouseDown(const juce::MouseEvent& e) {
    exported = false;
    draggingBpm = bpmArea().contains(e.getPosition());
    bpmAtDown = processor.value("bpm");
}
void SwellView::mouseDrag(const juce::MouseEvent& e) {
    if (draggingBpm) {
        processor.setControlValue("bpm", juce::jlimit(40.0f, 240.0f, std::round(bpmAtDown - static_cast<float>(e.getDistanceFromDragStartY()) * 0.25f)));
        repaint();
        return;
    }
    if (exported || e.getDistanceFromDragStart() < 5) return;
    exported = true;
    const auto file = processor.exportForDrag();
    if (file.existsAsFile()) juce::DragAndDropContainer::performExternalDragDropOfFiles({ file.getFullPathName() }, false, this);
}
void SwellView::mouseUp(const juce::MouseEvent&) { draggingBpm = false; repaint(); }

// ---------------------------------------------------------------- filter
FilterGraph::FilterGraph(VersebProcessor& p) : processor(p) {
    processor.parameters.addParameterListener("hp", this);
    processor.parameters.addParameterListener("lp", this);
}
FilterGraph::~FilterGraph() {
    processor.parameters.removeParameterListener("hp", this);
    processor.parameters.removeParameterListener("lp", this);
}
juce::Rectangle<float> FilterGraph::plot() const { return getLocalBounds().toFloat().withTrimmedBottom(22); }
float FilterGraph::xFor(float hz) const {
    const auto p = plot();
    return p.getX() + 10 + std::log(hz / 20.0f) / std::log(1000.0f) * (p.getWidth() - 20);
}
float FilterGraph::hzFor(float x) const {
    const auto p = plot();
    return 20.0f * std::pow(1000.0f, juce::jlimit(0.0f, 1.0f, (x - p.getX() - 10) / (p.getWidth() - 20)));
}
float FilterGraph::responseDb(float hz) const {
    const float hp = processor.value("hp"), lp = processor.value("lp");
    double power = 1;
    if (hp > 20.5f) power /= 1 + std::pow(hp / hz, 8.0f);
    if (lp < 19900.0f) power /= 1 + std::pow(hz / lp, 8.0f);
    return static_cast<float>(10.0 * std::log10(juce::jmax(1.0e-9, power)));
}
int FilterGraph::handleAt(juce::Point<float> pos) const {
    const float y = plot().getY() + plot().getHeight() * 0.45f;
    if (pos.getDistanceFrom({ xFor(processor.value("hp")), y }) < 10) return 0;
    if (pos.getDistanceFrom({ xFor(processor.value("lp")), y }) < 10) return 1;
    return -1;
}
void FilterGraph::paint(juce::Graphics& g) {
    displayBox(g, getLocalBounds().toFloat());
    const auto p = plot();
    const float base = p.getY() + p.getHeight() * 0.45f;
    for (float hz : { 100.0f, 1000.0f, 10000.0f }) {
        const float x = xFor(hz);
        g.setColour(juce::Colour(0xff17212d)); g.fillRect(x, p.getY() + 4, 1.0f, p.getHeight() - 4);
        tracked(g, fonts->mono(8.5f, false, 0.02f), hz >= 10000 ? "10k" : hz >= 1000 ? "1k" : "100",
                { static_cast<int>(x) + 3, static_cast<int>(p.getBottom()) - 15, 30, 12 }, juce::Colour(0xff5f7084));
    }
    g.setColour(divider); g.fillRect(4.0f, p.getBottom(), static_cast<float>(getWidth()) - 8, 1.0f);
    juce::Path curve, fill;
    for (int step = 0; p.getX() + 10 + static_cast<float>(step) <= p.getRight() - 10; ++step) {
        const float x = p.getX() + 10 + static_cast<float>(step);
        const float y = juce::jmin(p.getBottom() - 1, base - responseDb(hzFor(x)) * 1.6f);
        if (step == 0) curve.startNewSubPath(x, y); else curve.lineTo(x, y);
    }
    fill = curve; fill.lineTo(p.getRight() - 10, p.getBottom()); fill.lineTo(p.getX() + 10, p.getBottom()); fill.closeSubPath();
    g.setGradientFill(juce::ColourGradient(accent.withAlpha(0.12f), 0, base, accent.withAlpha(0.0f), 0, p.getBottom(), false));
    g.fillPath(fill);
    g.setColour(accent); g.strokePath(curve, juce::PathStrokeType(1.8f));
    for (int h = 0; h < 2; ++h) {
        const float x = xFor(processor.value(h == 0 ? "hp" : "lp"));
        g.setColour(display); g.fillEllipse(x - 5, base - 5, 10, 10);
        if (hover == h || dragging == h) { g.setColour(accent.withAlpha(0.35f)); g.fillEllipse(x - 5, base - 5, 10, 10); }
        g.setColour(juce::Colour(0xff8ed2f7)); g.drawEllipse(x - 5, base - 5, 10, 10, 1.6f);
    }
    const float hp = processor.value("hp"), lp = processor.value("lp");
    const auto labelFont = fonts->mono(9.5f, true, 0.04f);
    const int row = static_cast<int>(p.getBottom()) + 3;
    tracked(g, labelFont, "HP", { 10, row, 20, 16 }, juce::Colour(0xff6f8aa3));
    tracked(g, labelFont, hp > 20.5f ? hzText(hp) : "Off", { 30, row, 62, 16 }, accentBright);
    tracked(g, labelFont, "LP", { getWidth() / 2 + 1, row, 20, 16 }, juce::Colour(0xff6f8aa3));
    tracked(g, labelFont, lp < 19900.0f ? hzText(lp) : "Off", { getWidth() / 2 + 21, row, 70, 16 }, accentBright);
}
void FilterGraph::mouseMove(const juce::MouseEvent& e) {
    const int h = handleAt(e.position);
    if (h != hover) { hover = h; repaint(); }
    setMouseCursor(h >= 0 ? juce::MouseCursor::LeftRightResizeCursor : juce::MouseCursor::NormalCursor);
}
void FilterGraph::mouseDown(const juce::MouseEvent& e) {
    dragging = handleAt(e.position);
    if (dragging < 0 && plot().contains(e.position)) dragging = e.position.x < (xFor(processor.value("hp")) + xFor(processor.value("lp"))) * 0.5f ? 0 : 1;
    if (dragging >= 0) { processor.parameters.getParameter(dragging == 0 ? "hp" : "lp")->beginChangeGesture(); mouseDrag(e); }
}
void FilterGraph::mouseDrag(const juce::MouseEvent& e) {
    if (dragging < 0) return;
    float hz = hzFor(e.position.x);
    if (dragging == 0) hz = juce::jmin(hz, processor.value("lp") * 0.8f);
    else hz = juce::jmax(hz, processor.value("hp") * 1.25f);
    auto* parameter = processor.parameters.getParameter(dragging == 0 ? "hp" : "lp");
    parameter->setValueNotifyingHost(parameter->convertTo0to1(hz));
    repaint();
}
void FilterGraph::mouseUp(const juce::MouseEvent&) {
    if (dragging >= 0) processor.parameters.getParameter(dragging == 0 ? "hp" : "lp")->endChangeGesture();
    dragging = -1; repaint();
}
void FilterGraph::mouseDoubleClick(const juce::MouseEvent& e) {
    const int h = handleAt(e.position);
    if (h == 0) processor.setControlValue("hp", 20.0f);
    if (h == 1) processor.setControlValue("lp", 20000.0f);
}

// ---------------------------------------------------------------- effects list
EffectsList::EffectsList(VersebProcessor& p) : processor(p) {}
juce::Rectangle<float> EffectsList::rowBounds(int row) const { return { 2.0f, 1.5f + static_cast<float>(row) * 22.0f, static_cast<float>(getWidth()) - 4.0f, 22.0f }; }
int EffectsList::rowAt(float y) const { return juce::jlimit(0, verseb::effectCount - 1, static_cast<int>((y - 1.5f) / 22.0f)); }

void EffectsList::paint(juce::Graphics& g) {
    displayBox(g, getLocalBounds().toFloat(), 0.85f);
    const auto order = processor.getOrder();
    for (int row = 0; row < verseb::effectCount; ++row) {
        const int e = order[static_cast<size_t>(row)];
        const auto r = rowBounds(row);
        if (e == selected) { g.setColour(juce::Colour(0xff17222f)); g.fillRoundedRectangle(r.reduced(1, 1), 3); }
        if (reordering && row == dragRow) { g.setColour(accent.withAlpha(0.12f)); g.fillRoundedRectangle(r.reduced(1, 1), 3); }
        const bool on = processor.value(VersebProcessor::effectId(e, "On").toRawUTF8()) > 0.5f;
        const juce::Rectangle<float> box(r.getX() + 8, r.getCentreY() - 5, 10, 10);
        g.setColour(on ? accent : juce::Colour(0xff1a2431)); g.fillRoundedRectangle(box, 1.5f);
        if (!on) { g.setColour(juce::Colour(0xff2b3747)); g.drawRoundedRectangle(box, 1.5f, 1); }
        tracked(g, fonts->sans(11, true, 0.08f), verseb::effectNames[e], r.withTrimmedLeft(25).toNearestInt(),
                e == selected ? juce::Colour(0xfff3f7fb) : juce::Colour(0xffd5dde7));
        grip(g, r.getRight() - 13, r.getCentreY() - 4, juce::Colour(0xff485669));
    }
    if (reordering && dropRow >= 0 && dropRow != dragRow) {
        const auto r = rowBounds(dropRow);
        const float y = dropRow > dragRow ? r.getBottom() : r.getY();
        g.setColour(accent); g.fillRect(r.getX() + 4, y - 1, r.getWidth() - 8, 2.0f);
    }
}
void EffectsList::mouseDown(const juce::MouseEvent& e) {
    const int row = rowAt(e.position.y);
    const int effect = processor.getOrder()[static_cast<size_t>(row)];
    reordering = false; dropRow = -1; dragRow = row;
    if (e.position.x < 24) {
        const auto id = VersebProcessor::effectId(effect, "On");
        processor.setControlValue(id, processor.value(id.toRawUTF8()) > 0.5f ? 0.0f : 1.0f);
    }
    selected = effect;
    if (onSelect) onSelect(effect);
    repaint();
}
void EffectsList::mouseDrag(const juce::MouseEvent& e) {
    if (dragRow < 0 || std::abs(e.getDistanceFromDragStartY()) < 4) return;
    reordering = true; dropRow = rowAt(e.position.y);
    setMouseCursor(juce::MouseCursor::DraggingHandCursor);
    repaint();
}
void EffectsList::mouseUp(const juce::MouseEvent&) {
    if (reordering && dropRow >= 0 && dropRow != dragRow) {
        auto order = processor.getOrder();
        const int moved = order[static_cast<size_t>(dragRow)];
        std::vector<int> list(order.begin(), order.end());
        list.erase(list.begin() + dragRow);
        list.insert(list.begin() + dropRow, moved);
        std::copy(list.begin(), list.end(), order.begin());
        processor.setOrder(order);
    }
    reordering = false; dragRow = dropRow = -1;
    setMouseCursor(juce::MouseCursor::NormalCursor);
    repaint();
}
}

// ================================================================ editor
VersebEditor::VersebEditor(VersebProcessor& p)
    : AudioProcessorEditor(&p), owner(p),
      captureView(p), swellView(p), filterGraph(p), effectsList(p) {
    setLookAndFeel(&look);
    // Pre-crop the wallpaper to the editor's aspect ratio (fill, centred) at 2x.
    const auto source = juce::ImageCache::getFromMemory(BinaryData::wallpaper_jpg, BinaryData::wallpaper_jpgSize);
    if (source.isValid()) {
        const float aspect = static_cast<float>(baseWidth) / baseHeight;
        auto crop = source.getBounds();
        const auto w = static_cast<float>(crop.getWidth()), h = static_cast<float>(crop.getHeight());
        if (w / h > aspect) crop = crop.withSizeKeepingCentre(juce::roundToInt(h * aspect), crop.getHeight());
        else crop = crop.withSizeKeepingCentre(crop.getWidth(), juce::roundToInt(w / aspect));
        wallpaper = source.getClippedImage(crop).rescaled(baseWidth * 2, baseHeight * 2, juce::Graphics::highResamplingQuality);
    }
    addAndMakeVisible(content);
    content.addMouseListener(this, true);

    for (auto* c : std::initializer_list<juce::Component*> { &undoButton, &redoButton, &presetSelector, &saveButton, &menuButton, &captureView, &swellView,
             &playButton, &loopButton, &captureButton, &reverbSelector, &lengthKnob, &syncButton, &timeButton, &shapeKnob, &widthKnob,
             &levelKnob, &filterGraph, &effectsList, &effectKnobA, &effectKnobB })
        content.addAndMakeVisible(*c);

    help(undoButton, "Deshacer el ultimo cambio de parametros.");
    help(redoButton, "Rehacer el cambio deshecho.");
    help(presetSelector, "Presets: flechas para recorrerlos, clic en el nombre para ver la lista.");
    help(saveButton, "Guarda los ajustes actuales como preset de usuario.");
    help(menuButton, "Cargar audio, exportar WAV, limpiar la captura, carpeta de presets y tamano de la ventana.");
    help(captureView, "Audio capturado. Arrastra para elegir el tramo (frase); clic mueve el inicio; doble clic selecciona todo; Shift+arrastrar mueve el tramo. Tambien puedes soltar un WAV/AIFF/FLAC.");
    help(swellView, "Reverb inversa: termina justo donde empieza la frase. Arrastrala al DAW y colocala antes del tramo. Arrastra el BPM verticalmente si no hay tempo del host.");
    help(playButton, "Escucha la reverb inversa seguida del tramo seleccionado.");
    help(loopButton, "Repite la escucha en bucle.");
    help(captureButton, "Graba la entrada del plugin: reproduce la frase en el DAW, pulsa CAPTURE y vuelve a pulsar para detener (max. 30 s).");
    help(reverbSelector, "Tipo de reverb: Plate, Hall, Room, Chamber, Spring o Cathedral. Clic para ver la lista.");
    help(lengthKnob, "Duracion de la reverb inversa. SYNC: en compases segun el tempo. TIME: en segundos.");
    help(syncButton, "Longitud sincronizada al tempo, en compases.");
    help(timeButton, "Longitud libre, en segundos.");
    help(shapeKnob, "Forma del crescendo: a la izquierda crece tarde y de golpe; a la derecha empieza antes y sube gradual.");
    help(widthKnob, "Anchura estereo: 0 % mono, 100 % natural, 200 % extra ancha.");
    help(levelKnob, "Nivel de la reverb inversa. 0 dB = pico normalizado a -1 dBFS.");
    help(filterGraph, "Filtro de la reverb, 24 dB/oct. Arrastra los puntos: izquierda high-pass, derecha low-pass. Doble clic: Off.");
    help(effectsList, "Efectos en orden de arriba abajo. Casilla: activar. Nombre: editar. Arrastra una fila para reordenar.");

    configureKnob(lengthKnob, {}); configureKnob(shapeKnob, {}); configureKnob(widthKnob, {}); configureKnob(levelKnob, {});
    configureKnob(effectKnobA, {}); configureKnob(effectKnobB, {});
    shapeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(owner.parameters, "shape", shapeKnob);
    widthAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(owner.parameters, "width", widthKnob);
    levelAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(owner.parameters, "level", levelKnob);
    shapeKnob.textFromValueFunction = [](double v) { return juce::String(juce::roundToInt(v * 100)) + " %"; };
    widthKnob.textFromValueFunction = [](double v) { return juce::String(juce::roundToInt(v * 100)) + " %"; };
    levelKnob.textFromValueFunction = [](double v) { return (v > 0 ? "+" : "") + juce::String(v, 1) + " dB"; };
    for (auto* k : { &shapeKnob, &widthKnob, &levelKnob }) {
        auto* parameter = owner.parameters.getParameter(k == &shapeKnob ? "shape" : k == &widthKnob ? "width" : "level");
        k->setDoubleClickReturnValue(true, parameter->convertFrom0to1(parameter->getDefaultValue()));
    }
    attachLengthKnob();
    selectEffect(verseb::PitchRiser);
    effectsList.onSelect = [this](int e) { selectEffect(e); };

    undoButton.onClick = [this] { owner.undo.undo(); };
    redoButton.onClick = [this] { owner.undo.redo(); };
    saveButton.onClick = [this] { savePreset(); };
    menuButton.onClick = [this] { showMenu(); };
    presetSelector.onClick = [this] { showPresetMenu(); };
    presetSelector.onStep = [this](int step) {
        const auto names = owner.presetNames();
        const int index = names.indexOf(owner.currentPreset);
        owner.loadPreset((juce::jmax(0, index) + step + names.size()) % names.size());
    };
    reverbSelector.textSize = 15;
    reverbSelector.onStep = [this](int step) {
        owner.undo.beginNewTransaction();
        owner.setControlValue("reverb", static_cast<float>((static_cast<int>(owner.value("reverb")) + step + verseb::reverbCount) % verseb::reverbCount));
    };
    reverbSelector.onClick = [this] {
        juce::PopupMenu menu;
        const int current = static_cast<int>(owner.value("reverb"));
        for (int i = 0; i < verseb::reverbCount; ++i) menu.addItem(i + 1, verseb::reverbNames[i], true, i == current);
        menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(reverbSelector).withMinimumWidth(reverbSelector.getWidth()),
            [this](int r) { if (r > 0) { owner.undo.beginNewTransaction(); owner.setControlValue("reverb", static_cast<float>(r - 1)); } });
    };
    playButton.onClick = [this] { owner.setPlaying(!owner.isPlaying()); };
    loopButton.onClick = [this] { owner.looping.store(!owner.looping.load()); };
    captureButton.onClick = [this] { if (owner.isCapturing()) owner.stopCapture(); else owner.startCapture(); };
    syncButton.onClick = [this] { owner.setControlValue("sync", 1.0f); };
    timeButton.onClick = [this] { owner.setControlValue("sync", 0.0f); };

    setResizable(true, true);
    setResizeLimits(baseWidth * 3 / 4, baseHeight * 3 / 4, baseWidth * 2, baseHeight * 2);
    if (auto* constrainer = getConstrainer()) constrainer->setFixedAspectRatio(static_cast<double>(baseWidth) / baseHeight);
    setSize(baseWidth, baseHeight);
    timerCallback();
    startTimerHz(30);
}

VersebEditor::~VersebEditor() {
    stopTimer();
    content.removeMouseListener(this);
    setLookAndFeel(nullptr);
}

void VersebEditor::help(juce::Component& c, const juce::String& t) { c.getProperties().set("help", t); }

void VersebEditor::configureKnob(juce::Slider& knob, const juce::String& tip) {
    knob.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    knob.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    knob.setRotaryParameters(juce::MathConstants<float>::pi * 1.25f, juce::MathConstants<float>::pi * 2.75f, true);
    knob.setPopupDisplayEnabled(true, true, &content, 900);
    knob.setMouseCursor(juce::MouseCursor::UpDownResizeCursor);
    if (tip.isNotEmpty()) help(knob, tip);
}

void VersebEditor::attachLengthKnob() {
    lengthSynced = owner.value("sync") > 0.5f;
    lengthAttachment.reset();
    lengthAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(owner.parameters, lengthSynced ? "division" : "time", lengthKnob);
    auto* parameter = owner.parameters.getParameter(lengthSynced ? "division" : "time");
    lengthKnob.setDoubleClickReturnValue(true, parameter->convertFrom0to1(parameter->getDefaultValue()));
    if (lengthSynced) lengthKnob.textFromValueFunction = [](double v) { return juce::String(verseb::lengthNames[juce::jlimit(0, 6, juce::roundToInt(v))]); };
    else lengthKnob.textFromValueFunction = [](double v) { return juce::String(v, 2) + " s"; };
}

void VersebEditor::selectEffect(int effect) {
    selectedEffect = effect;
    effectsList.selected = effect;
    effectAttachmentA.reset(); effectAttachmentB.reset();
    effectAttachmentA = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(owner.parameters, VersebProcessor::effectId(effect, "A"), effectKnobA);
    effectAttachmentB = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(owner.parameters, VersebProcessor::effectId(effect, "B"), effectKnobB);
    effectKnobA.textFromValueFunction = [effect](double v) { return verseb::effectValueText(effect, 0, static_cast<float>(v)); };
    effectKnobB.textFromValueFunction = [effect](double v) { return verseb::effectValueText(effect, 1, static_cast<float>(v)); };
    for (int which = 0; which < 2; ++which) {
        auto* parameter = owner.parameters.getParameter(VersebProcessor::effectId(effect, which == 0 ? "A" : "B"));
        (which == 0 ? effectKnobA : effectKnobB).setDoubleClickReturnValue(true, parameter->convertFrom0to1(parameter->getDefaultValue()));
    }
    static const char* helpA[] = { "Pitch Riser: semitonos por debajo desde los que sube hasta el tono original al llegar a la frase.",
        "Saturation: ganancia de entrada de la saturacion tanh.", "Sweep: frecuencia inicial del low-pass que se abre hasta la frase.",
        "Modulation: velocidad del chorus estereo.", "Tremolo: velocidad del tremolo." };
    static const char* helpB[] = { "Pitch Riser: curva de la subida; a la izquierda sube antes, a la derecha al final.",
        "Saturation: mezcla entre la senal limpia y la saturada.", "Sweep: resonancia del filtro.",
        "Modulation: profundidad del chorus.", "Tremolo: profundidad del tremolo." };
    help(effectKnobA, helpA[effect]); help(effectKnobB, helpB[effect]);
    effectsList.repaint();
    content.repaint(effectTitle);
}

juce::String VersebEditor::lengthText() const {
    if (owner.value("sync") > 0.5f) return verseb::lengthNames[juce::jlimit(0, 6, static_cast<int>(owner.value("division")))];
    const float s = owner.value("time");
    return s < 1.0f ? juce::String(juce::roundToInt(s * 1000)) + " ms" : juce::String(s, 2) + " s";
}

void VersebEditor::resized() {
    const float scale = static_cast<float>(getWidth()) / baseWidth;
    content.setBounds(0, 0, baseWidth, baseHeight);
    content.setTransform(juce::AffineTransform::scale(scale));
    undoButton.setBounds(419, 18, 30, 28); redoButton.setBounds(459, 18, 30, 28);
    presetSelector.setBounds(498, 18, 277, 28); saveButton.setBounds(785, 18, 45, 28); menuButton.setBounds(840, 18, 32, 28);
    captureView.setBounds(18, 60, 854, 108); swellView.setBounds(18, 179, 854, 108);
    playButton.setBounds(20, 301, 78, 26); loopButton.setBounds(109, 301, 86, 26); captureButton.setBounds(206, 301, 103, 26);
    reverbSelector.setBounds(723, 300, 149, 28);
    lengthKnob.setBounds(59, 377, 92, 92);
    syncButton.setBounds(38, 506, 64, 25); timeButton.setBounds(111, 506, 63, 25);
    shapeKnob.setBounds(224, 399, 54, 54); widthKnob.setBounds(291, 399, 54, 54); levelKnob.setBounds(257, 478, 54, 54);
    filterGraph.setBounds(387, 401, 193, 116);
    effectsList.setBounds(617, 398, 113, 113);
    effectKnobA.setBounds(777, 414, 46, 46); effectKnobB.setBounds(777, 485, 46, 46);
    captureView.refresh(); swellView.refresh();
}

void VersebEditor::Content::paint(juce::Graphics& g) {
    auto& e = editor;
    g.fillAll(navy);
    if (e.wallpaper.isValid()) g.drawImage(e.wallpaper, getLocalBounds().toFloat());
    g.setColour(navy.withAlpha(0.6f)); g.fillAll();
    const auto& f = *e.fonts;
    // Title (brand removed; product mark only)
    g.setFont(f.sans(27, true, 0.07f));
    const juce::String a = "VERS", b = "EB";
    const float wa = juce::GlyphArrangement::getStringWidth(g.getCurrentFont(), a);
    g.setColour(juce::Colours::black.withAlpha(0.4f)); g.drawText(a + b, juce::Rectangle<float>(23, 17, 260, 30), juce::Justification::centredLeft, false);
    g.setColour(juce::Colour(0xffeef2f7)); g.drawText(a, juce::Rectangle<float>(22, 15, 200, 30), juce::Justification::centredLeft, false);
    g.setColour(accent); g.drawText(b, juce::Rectangle<float>(22 + wa, 15, 200, 30), juce::Justification::centredLeft, false);

    struct Section { juce::Rectangle<float> r; const char* title; };
    for (auto& s : { Section { { 19, 342, 173, 202 }, "LENGTH" }, Section { { 206, 342, 158, 202 }, "TONE" },
                     Section { { 377, 342, 214, 202 }, "FILTER" }, Section { { 605, 342, 267, 202 }, "EFFECTS" } }) {
        g.setColour(juce::Colours::black.withAlpha(0.3f)); g.fillRoundedRectangle(s.r.translated(0, 2), 6);
        g.setColour(panel.withAlpha(0.88f)); g.fillRoundedRectangle(s.r, 6);
        g.setColour(panelEdge); g.drawRoundedRectangle(s.r.reduced(0.5f), 6, 1);
        tracked(g, f.sans(11.5f, false, 0.32f), s.title, { static_cast<int>(s.r.getX()) + 13, static_cast<int>(s.r.getY()) + 8, 200, 16 }, muted);
        g.setColour(divider); g.fillRect(s.r.getX() + 11, s.r.getY() + 29, s.r.getWidth() - 22, 1.0f);
    }
    tracked(g, f.sans(11.5f, true, 0.32f), "REVERB TYPE", { 561, 302, 152, 26 }, juce::Colours::black.withAlpha(0.75f), juce::Justification::centredRight);
    tracked(g, f.sans(11.5f, true, 0.32f), "REVERB TYPE", { 560, 301, 152, 26 }, label, juce::Justification::centredRight);
    const auto knobLabel = [&](const juce::String& t, int cx, int y) { tracked(g, f.sans(11, true, 0.12f), t, { cx - 50, y, 100, 14 }, label, juce::Justification::centred); };
    knobLabel("SHAPE", 251, 383); knobLabel("WIDTH", 318, 383); knobLabel("LEVEL", 284, 463);
    knobLabel(verseb::effectNames[e.selectedEffect], 800, 377);
    tracked(g, f.sans(10.5f, true, 0.12f), verseb::effectParamNames[e.selectedEffect][0], { 750, 398, 100, 14 }, muted, juce::Justification::centred);
    tracked(g, f.sans(10.5f, true, 0.12f), verseb::effectParamNames[e.selectedEffect][1], { 750, 469, 100, 14 }, muted, juce::Justification::centred);

    displayBox(g, lengthReadout.toFloat(), 0.95f);
    tracked(g, f.mono(17, true, 0.1f), e.lengthText(), lengthReadout, accentBright, juce::Justification::centred);

    displayBox(g, helpBar.toFloat(), 0.9f);
    tracked(g, f.mono(12, true, 0.04f), "HELP:", helpBar.withTrimmedLeft(13), accentBright);
    tracked(g, f.mono(12, false, 0.02f), e.helpText, helpBar.withTrimmedLeft(58).withTrimmedRight(12), juce::Colour(0xffa9c3d6));
}

void VersebEditor::updateHelp() {
    juce::String next = "Pasa el cursor sobre cualquier control para ver que hace.";
    for (auto* c = juce::Desktop::getInstance().getMainMouseSource().getComponentUnderMouse(); c != nullptr && c != &content; c = c->getParentComponent())
        if (c->getProperties().contains("help")) { next = c->getProperties()["help"].toString(); break; }
    if (next != helpText) { helpText = next; content.repaint(helpBar); }
}

void VersebEditor::timerCallback() {
    if (owner.pollCapture()) owner.setPlaying(false);
    if (owner.sourceVersion() != shownSource) { shownSource = owner.sourceVersion(); captureView.refresh(); }
    if (owner.resultVersion() != shownResult) { shownResult = owner.resultVersion(); swellView.refresh(); }
    const bool capturing = owner.isCapturing(), playing = owner.isPlaying();
    if (capturing || wasCapturing) captureView.repaint();
    wasCapturing = capturing;
    if (playing) { captureView.repaint(); swellView.repaint(); }

    playButton.lit = playing; playButton.icon = playing ? ui::Icon::stop : ui::Icon::play; playButton.setButtonText(playing ? "STOP" : "PLAY");
    loopButton.lit = owner.looping.load();
    captureButton.recording = capturing; captureButton.setButtonText(capturing ? "STOP" : "CAPTURE");
    const bool synced = owner.value("sync") > 0.5f;
    syncButton.lit = synced; timeButton.lit = !synced;
    if (synced != lengthSynced) attachLengthKnob();
    undoButton.setEnabled(owner.undo.canUndo()); redoButton.setEnabled(owner.undo.canRedo());
    for (auto* b : { &playButton, &loopButton, &captureButton, &syncButton, &timeButton }) b->repaint();
    presetSelector.setText(owner.currentPreset);
    reverbSelector.setText(verseb::reverbNames[juce::jlimit(0, verseb::reverbCount - 1, static_cast<int>(owner.value("reverb")))]);
    static juce::String lastLength;
    if (const auto t = lengthText(); t != lastLength) { lastLength = t; content.repaint(lengthReadout); swellView.repaint(); }
    swellView.repaint(swellView.bpmArea().withLeft(swellView.getWidth() - 300));
    effectsList.repaint(); filterGraph.repaint();
    updateHelp();
}

void VersebEditor::mouseDown(const juce::MouseEvent& e) {
    if (e.eventComponent != &undoButton && e.eventComponent != &redoButton) owner.undo.beginNewTransaction();
}

void VersebEditor::showPresetMenu() {
    juce::PopupMenu menu;
    const auto names = owner.presetNames();
    menu.addSectionHeader("FACTORY");
    for (int i = 0; i < names.size(); ++i) {
        if (i == owner.numFactoryPresets()) menu.addSectionHeader("USER");
        menu.addItem(i + 1, names[i], true, names[i] == owner.currentPreset);
    }
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(presetSelector).withMinimumWidth(presetSelector.getWidth()),
        [this](int r) { if (r > 0) owner.loadPreset(r - 1); });
}

void VersebEditor::savePreset() {
    auto* window = new juce::AlertWindow("SAVE PRESET", "Nombre del preset de usuario:", juce::MessageBoxIconType::NoIcon, this);
    window->setLookAndFeel(&look);
    window->addTextEditor("name", owner.currentPreset == "DEFAULT" ? "MY PRESET" : owner.currentPreset);
    window->addButton("SAVE", 1, juce::KeyPress(juce::KeyPress::returnKey));
    window->addButton("CANCEL", 0, juce::KeyPress(juce::KeyPress::escapeKey));
    window->enterModalState(true, juce::ModalCallbackFunction::create([this, window](int r) {
        if (r == 1) owner.saveUserPreset(window->getTextEditorContents("name"));
    }), true);
}

void VersebEditor::showMenu() {
    juce::PopupMenu menu, size;
    const bool hasSwell = owner.getResult() != nullptr && owner.getResult()->swellLength > 0;
    menu.addItem(1, "Cargar audio...");
    menu.addItem(2, "Exportar reverb inversa (WAV)...", hasSwell);
    menu.addItem(3, "Exportar reverb + frase (WAV)...", hasSwell);
    menu.addSeparator();
    menu.addItem(4, "Seleccionar todo el audio", owner.sourceLength() > 0);
    menu.addItem(5, "Borrar audio capturado", owner.sourceLength() > 0);
    menu.addSeparator();
    menu.addItem(6, "Abrir carpeta de presets");
    for (int s : { 75, 100, 125, 150, 200 }) size.addItem(100 + s, juce::String(s) + " %", true, std::abs(getWidth() - baseWidth * s / 100) < 3);
    menu.addSubMenu("Tamano de ventana", size);
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(menuButton), [this](int r) {
        if (r == 1) {
            chooser = std::make_unique<juce::FileChooser>("Cargar audio", juce::File(), "*.wav;*.aif;*.aiff;*.flac;*.mp3;*.ogg");
            chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                [this](const juce::FileChooser& c) { if (c.getResult().existsAsFile()) owner.loadAudioFile(c.getResult()); });
        } else if (r == 2 || r == 3) {
            const bool phrase = r == 3;
            chooser = std::make_unique<juce::FileChooser>("Exportar WAV", juce::File::getSpecialLocation(juce::File::userMusicDirectory)
                .getChildFile(phrase ? "Verseb reverse + phrase.wav" : "Verseb reverse reverb.wav"), "*.wav");
            chooser->launchAsync(juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting,
                [this, phrase](const juce::FileChooser& c) { if (c.getResult() != juce::File()) owner.writeWav(c.getResult().withFileExtension("wav"), phrase); });
        } else if (r == 4) owner.setSelection(0, owner.sourceLength());
        else if (r == 5) owner.clearSource();
        else if (r == 6) { VersebProcessor::presetFolder().createDirectory(); VersebProcessor::presetFolder().startAsProcess(); }
        else if (r > 100) setSize(baseWidth * (r - 100) / 100, baseHeight * (r - 100) / 100);
        captureView.refresh();
    });
}

bool VersebEditor::isInterestedInFileDrag(const juce::StringArray& files) {
    for (auto& f : files) if (juce::File(f).hasFileExtension("wav;aif;aiff;flac;mp3;ogg")) return true;
    return false;
}
void VersebEditor::fileDragEnter(const juce::StringArray&, int, int) { captureView.dropHighlight = true; captureView.repaint(); }
void VersebEditor::fileDragExit(const juce::StringArray&) { captureView.dropHighlight = false; captureView.repaint(); }
void VersebEditor::filesDropped(const juce::StringArray& files, int, int) {
    captureView.dropHighlight = false;
    for (auto& f : files) if (owner.loadAudioFile(juce::File(f))) break;
    captureView.refresh();
}
