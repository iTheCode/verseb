#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <cmath>

namespace {
constexpr const char* effectPrefix[] = { "pitch", "sat", "sweep", "mod", "trem" };
constexpr float effectDefaults[][2] = { { 0.5f, 0.5f }, { 0.3f, 0.5f }, { 0.3f, 0.2f }, { 0.4f, 0.35f }, { 0.5f, 0.5f } };
constexpr int maxLiveBuckets = static_cast<int>(VersebProcessor::maxCaptureSeconds * 192000 / VersebProcessor::liveBucket) + 1;

juce::NormalisableRange<float> frequencyRange() {
    return { 20.0f, 20000.0f,
        [](float s, float e, float v) { return s * std::pow(e / s, v); },
        [](float s, float e, float v) { return std::log(v / s) / std::log(e / s); },
        [](float s, float e, float v) { return juce::jlimit(s, e, v); } };
}

struct FactoryPreset { const char* name; std::vector<std::pair<juce::String, float>> values; };
const std::vector<FactoryPreset>& factoryPresets() {
    static const std::vector<FactoryPreset> presets {
        { "DEFAULT", {} },
        { "VOCAL SWELL", { { "division", 4 }, { "shape", 0.62f }, { "width", 1.2f }, { "hp", 180 }, { "lp", 12000 }, { "modOn", 1 } } },
        { "BIG HALL INTRO", { { "reverb", 1 }, { "division", 5 }, { "shape", 0.72f }, { "width", 1.4f }, { "hp", 120 }, { "modOn", 0 } } },
        { "DARK ROOM", { { "reverb", 2 }, { "division", 3 }, { "shape", 0.35f }, { "lp", 4000 }, { "modOn", 0 } } },
        { "TAPE RISER", { { "reverb", 3 }, { "division", 4 }, { "hp", 150 }, { "modOn", 0 }, { "pitchOn", 1 }, { "pitchA", 0.5f }, { "pitchB", 0.6f }, { "satOn", 1 }, { "satA", 0.35f }, { "satB", 0.6f } } },
        { "SPRING SUCK", { { "reverb", 4 }, { "division", 2 }, { "shape", 0.3f }, { "modOn", 0 }, { "tremOn", 1 }, { "tremA", 0.6f }, { "tremB", 0.4f } } },
        { "CATHEDRAL LIFT", { { "reverb", 5 }, { "division", 5 }, { "width", 1.6f }, { "modOn", 0 }, { "sweepOn", 1 }, { "sweepA", 0.25f }, { "sweepB", 0.25f } } },
    };
    return presets;
}

bool writeBuffer(juce::AudioFormat& format, std::unique_ptr<juce::OutputStream> stream, const juce::AudioBuffer<float>& buffer, double rate, int bits) {
    if (stream == nullptr) return false;
    auto writer = format.createWriterFor(stream, juce::AudioFormatWriterOptions {}.withSampleRate(rate)
        .withNumChannels(buffer.getNumChannels()).withBitsPerSample(bits));
    return writer != nullptr && writer->writeFromAudioSampleBuffer(buffer, 0, buffer.getNumSamples());
}
}

class VersebProcessor::RenderThread final : public juce::Thread {
public:
    explicit RenderThread(VersebProcessor& p) : Thread("Verseb render"), owner(p) {}
    void run() override {
        while (!threadShouldExit()) { owner.renderIfNeeded(); wait(35); }
    }
    juce::String lastKey;
private:
    VersebProcessor& owner;
};

juce::String VersebProcessor::effectId(int effect, const char* suffix) { return juce::String(effectPrefix[effect]) + suffix; }

juce::AudioProcessorValueTreeState::ParameterLayout VersebProcessor::layout() {
    juce::AudioProcessorValueTreeState::ParameterLayout p;
    const auto addFloat = [&p](const juce::String& id, const juce::String& name, juce::NormalisableRange<float> range, float initial) {
        p.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID { id, 1 }, name, range, initial));
    };
    juce::StringArray lengths, reverbs;
    for (auto* n : verseb::lengthNames) lengths.add(n);
    for (auto* n : verseb::reverbNames) reverbs.add(n);
    p.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID { "sync", 1 }, "Length Sync", true));
    p.add(std::make_unique<juce::AudioParameterChoice>(juce::ParameterID { "division", 1 }, "Length (bars)", lengths, 3));
    auto time = juce::NormalisableRange<float>(0.05f, 12.0f, 0.001f); time.setSkewForCentre(1.5f);
    addFloat("time", "Length (seconds)", time, 1.0f);
    addFloat("bpm", "Manual BPM", { 40.0f, 240.0f, 0.1f }, 120.0f);
    p.add(std::make_unique<juce::AudioParameterChoice>(juce::ParameterID { "reverb", 1 }, "Reverb Type", reverbs, 0));
    addFloat("shape", "Shape", { 0.0f, 1.0f, 0.001f }, 0.5f);
    addFloat("width", "Width", { 0.0f, 2.0f, 0.001f }, 1.0f);
    addFloat("level", "Level", { -24.0f, 6.0f, 0.1f }, 0.0f);
    addFloat("hp", "High-pass", frequencyRange(), 20.0f);
    addFloat("lp", "Low-pass", frequencyRange(), 20000.0f);
    for (int e = 0; e < verseb::effectCount; ++e) {
        const juce::String name(verseb::effectNames[e]);
        p.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID { effectId(e, "On"), 1 }, name + " On", e == verseb::Modulation));
        addFloat(effectId(e, "A"), name + " " + verseb::effectParamNames[e][0], { 0.0f, 1.0f, 0.001f }, effectDefaults[e][0]);
        addFloat(effectId(e, "B"), name + " " + verseb::effectParamNames[e][1], { 0.0f, 1.0f, 0.001f }, effectDefaults[e][1]);
    }
    return p;
}

VersebProcessor::VersebProcessor()
    : AudioProcessor(BusesProperties().withInput("Input", juce::AudioChannelSet::stereo(), true)
        .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      parameters(*this, &undo, "VERSEB", layout()), livePeaks(static_cast<size_t>(maxLiveBuckets)) {
    for (int e = 0; e < verseb::effectCount; ++e) order[static_cast<size_t>(e)].store(e);
    parameters.state.setProperty("order", "0,1,2,3,4", nullptr);
    parameters.state.addListener(this);
    renderer = std::make_unique<RenderThread>(*this);
    renderer->startThread(juce::Thread::Priority::low);
}

VersebProcessor::~VersebProcessor() {
    renderer->stopThread(4000);
    parameters.state.removeListener(this);
}

void VersebProcessor::prepareToPlay(double rate, int) {
    hostRate.store(rate);
    captureState.store(0);
    captureBuffer.setSize(2, static_cast<int>(rate * maxCaptureSeconds) + 1, false, true, false);
    setLatencySamples(0);
}

bool VersebProcessor::isBusesLayoutSupported(const BusesLayout& buses) const {
    const auto out = buses.getMainOutputChannelSet();
    return buses.getMainInputChannelSet() == out && (out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo());
}

void VersebProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) {
    juce::ScopedNoDenormals guard;
    const int n = buffer.getNumSamples(), channels = buffer.getNumChannels();
    double bpm = 0;
    if (auto* playhead = getPlayHead())
        if (auto position = playhead->getPosition()) {
            if (auto b = position->getBpm(); b && std::isfinite(*b) && *b >= 40 && *b <= 240) bpm = *b;
            if (auto sig = position->getTimeSignature(); sig && sig->numerator > 0 && sig->denominator > 0)
                barBeats.store(sig->numerator * 4.0 / sig->denominator);
        }
    hostBpm.store(bpm);

    if (const int state = captureState.load(); state != 0 && channels > 0) {
        int length = captureLength.load();
        const int room = juce::jmin(n, captureBuffer.getNumSamples() - length);
        if (state == 1 && room > 0) {
            for (int c = 0; c < 2; ++c) captureBuffer.copyFrom(c, length, buffer, juce::jmin(c, channels - 1), 0, room);
            for (int i = 0; i < room; ++i) {
                const int bucket = (length + i) / liveBucket;
                float level = 0;
                for (int c = 0; c < 2; ++c) level = juce::jmax(level, std::abs(captureBuffer.getSample(c, length + i)));
                if (bucket < maxLiveBuckets) {
                    auto& peak = livePeaks[static_cast<size_t>(bucket)];
                    if ((length + i) % liveBucket == 0) peak.store(level); else if (level > peak.load()) peak.store(level);
                }
            }
            length += room;
            captureLength.store(length);
        }
        if (state == 2 || length >= captureBuffer.getNumSamples()) { captureState.store(0); captureReady.store(true); }
    }

    const juce::SpinLock::ScopedTryLockType lock(resultLock);
    if (lock.isLocked() && result != nullptr && playing.load() && result->preview.getNumSamples() > 1) {
        const auto& preview = result->preview;
        const int length = preview.getNumSamples();
        const double step = result->sampleRate / hostRate.load();
        for (int i = 0; i < n && playing.load(); ++i) {
            const auto index = static_cast<int>(playPosition);
            const auto fraction = static_cast<float>(playPosition - index);
            for (int c = 0; c < channels; ++c) {
                const int from = channels == 1 ? -1 : juce::jmin(c, 1);
                const auto sample = [&](int ch, int k) { return k < length ? preview.getSample(ch, k) : 0.0f; };
                const auto at = [&](int ch) { return sample(ch, index) + fraction * (sample(ch, index + 1) - sample(ch, index)); };
                buffer.addSample(c, i, from < 0 ? 0.5f * (at(0) + at(1)) : at(from));
            }
            playPosition += step;
            if (playPosition >= length - 1) {
                playPosition = 0;
                if (!looping.load()) playing.store(false);
            }
        }
        playTime.store(playPosition / result->sampleRate);
    }
}

// ---------------------------------------------------------------- parameters
void VersebProcessor::setControlValue(const juce::String& id, float v) {
    if (auto* p = parameters.getParameter(id)) {
        p->beginChangeGesture(); p->setValueNotifyingHost(p->convertTo0to1(v)); p->endChangeGesture();
    }
}

std::array<int, verseb::effectCount> VersebProcessor::getOrder() const {
    std::array<int, verseb::effectCount> current {};
    for (size_t i = 0; i < current.size(); ++i) current[i] = order[i].load();
    return current;
}

void VersebProcessor::setOrder(const std::array<int, verseb::effectCount>& newOrder) {
    juce::StringArray text;
    for (auto e : newOrder) text.add(juce::String(e));
    parameters.state.setProperty("order", text.joinIntoString(","), &undo);
}

void VersebProcessor::syncOrderFromTree() {
    auto tokens = juce::StringArray::fromTokens(parameters.state.getProperty("order").toString(), ",", "");
    std::array<bool, verseb::effectCount> seen {};
    std::array<int, verseb::effectCount> parsed {};
    int count = 0;
    for (auto& t : tokens) {
        const int e = t.getIntValue();
        if (count < verseb::effectCount && e >= 0 && e < verseb::effectCount && !seen[static_cast<size_t>(e)]) {
            seen[static_cast<size_t>(e)] = true; parsed[static_cast<size_t>(count++)] = e;
        }
    }
    for (int e = 0; e < verseb::effectCount; ++e) if (!seen[static_cast<size_t>(e)]) parsed[static_cast<size_t>(count++)] = e;
    for (size_t i = 0; i < parsed.size(); ++i) order[i].store(parsed[i]);
}

void VersebProcessor::valueTreePropertyChanged(juce::ValueTree& tree, const juce::Identifier& property) {
    if (tree == parameters.state && property.toString() == "order") syncOrderFromTree();
}
void VersebProcessor::valueTreeRedirected(juce::ValueTree&) { syncOrderFromTree(); }

double VersebProcessor::currentBpm() const {
    const double host = hostBpm.load();
    return host > 0 ? host : static_cast<double>(value("bpm"));
}

verseb::Settings VersebProcessor::currentSettings(double sampleRate) const {
    verseb::Settings s;
    s.sampleRate = sampleRate;
    const int division = juce::jlimit(0, 6, static_cast<int>(value("division")));
    s.seconds = value("sync") > 0.5f ? verseb::lengthBars[division] * barBeats.load() * 60.0 / currentBpm() : value("time");
    s.reverbType = static_cast<int>(value("reverb"));
    s.shape = value("shape"); s.width = value("width"); s.levelDb = value("level");
    const float hp = value("hp"), lp = value("lp");
    s.highPass = hp > 20.5f ? hp : 0.0f;
    s.lowPass = lp < 19900.0f ? lp : 0.0f;
    const auto currentOrder = getOrder();
    for (size_t i = 0; i < currentOrder.size(); ++i) {
        const int e = currentOrder[i];
        s.chain[i] = { e, value(effectId(e, "On").toRawUTF8()) > 0.5f, value(effectId(e, "A").toRawUTF8()), value(effectId(e, "B").toRawUTF8()) };
    }
    return s;
}

// ---------------------------------------------------------------- capture and source
void VersebProcessor::startCapture() {
    setPlaying(false);
    captureLength.store(0);
    for (auto& p : livePeaks) p.store(0);
    captureReady.store(false);
    captureState.store(1);
}

void VersebProcessor::stopCapture() { if (captureState.load() == 1) captureState.store(2); }

bool VersebProcessor::pollCapture() {
    // If the device stopped calling processBlock, finish the capture from here.
    static thread_local juce::uint32 stopRequested = 0;
    if (captureState.load() == 2) {
        if (stopRequested == 0) stopRequested = juce::Time::getMillisecondCounter();
        else if (juce::Time::getMillisecondCounter() - stopRequested > 400) { captureState.store(0); captureReady.store(true); }
    } else stopRequested = 0;
    if (!captureReady.exchange(false)) return false;
    const int length = captureLength.load();
    if (length < 64) return false;
    juce::AudioBuffer<float> copy(2, length);
    for (int c = 0; c < 2; ++c) copy.copyFrom(c, 0, captureBuffer, c, 0, length);
    setSource(std::move(copy), hostRate.load(), true);
    return true;
}

bool VersebProcessor::loadAudioFile(const juce::File& file) {
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(file));
    if (reader == nullptr || reader->sampleRate <= 0) return false;
    const auto length = static_cast<int>(juce::jmin<juce::int64>(reader->lengthInSamples, static_cast<juce::int64>(reader->sampleRate * maxCaptureSeconds)));
    if (length < 64) return false;
    juce::AudioBuffer<float> audio(2, length);
    audio.clear();
    reader->read(&audio, 0, length, 0, true, true);
    if (reader->numChannels == 1) audio.copyFrom(1, 0, audio, 0, 0, length);
    setSource(std::move(audio), reader->sampleRate, false);
    return true;
}

void VersebProcessor::setSource(juce::AudioBuffer<float> audio, double rate, bool trimToOnset) {
    setPlaying(false);
    const int length = audio.getNumSamples();
    int start = 0, end = length;
    if (trimToOnset && length > 0) {
        const auto loud = [&audio](int i) { return std::abs(audio.getSample(0, i)) > 0.003f || std::abs(audio.getSample(1, i)) > 0.003f; };
        while (start < length && !loud(start)) ++start;
        while (end > start && !loud(end - 1)) --end;
        if (start >= end) { start = 0; end = length; }
        start = juce::jmax(0, start - static_cast<int>(rate * 0.005));
        end = juce::jmin(length, end + static_cast<int>(rate * 0.05));
    }
    {
        const juce::ScopedLock lock(sourceLock);
        source = std::move(audio);
        sourceSampleRate.store(rate);
        sourceSamples.store(length);
        selectionStart.store(start); selectionEnd.store(end);
        sourceCounter.fetch_add(1);
    }
}

void VersebProcessor::clearSource() { setSource({}, sourceSampleRate.load(), false); }

std::vector<std::pair<float, float>> VersebProcessor::sourcePeaks(int columns) const {
    std::vector<std::pair<float, float>> peaks(static_cast<size_t>(juce::jmax(0, columns)), { 0.0f, 0.0f });
    const juce::ScopedLock lock(sourceLock);
    const int length = source.getNumSamples();
    if (length == 0 || columns <= 0) return peaks;
    for (int col = 0; col < columns; ++col) {
        const int a = static_cast<int>(static_cast<juce::int64>(length) * col / columns);
        const int b = juce::jmax(a + 1, static_cast<int>(static_cast<juce::int64>(length) * (col + 1) / columns));
        float low = 0, high = 0;
        for (int c = 0; c < source.getNumChannels(); ++c) {
            const auto range = juce::FloatVectorOperations::findMinAndMax(source.getReadPointer(c, a), juce::jmin(b, length) - a);
            low = juce::jmin(low, range.getStart()); high = juce::jmax(high, range.getEnd());
        }
        peaks[static_cast<size_t>(col)] = { low, high };
    }
    return peaks;
}

void VersebProcessor::setSelection(int start, int end) {
    const int length = sourceSamples.load();
    start = juce::jlimit(0, length, start); end = juce::jlimit(0, length, end);
    if (end < start) std::swap(start, end);
    if (end - start < 32) end = juce::jmin(length, start + 32);
    selectionStart.store(start); selectionEnd.store(end);
}

// ---------------------------------------------------------------- render and playback
void VersebProcessor::renderIfNeeded() {
    const double rate = sourceSampleRate.load();
    const auto s = currentSettings(rate);
    juce::String key;
    key << sourceCounter.load() << '|' << selectionStart.load() << '|' << selectionEnd.load() << '|' << rate << '|' << s.seconds
        << '|' << s.reverbType << '|' << s.shape << '|' << s.width << '|' << s.levelDb << '|' << s.highPass << '|' << s.lowPass;
    for (auto& slot : s.chain) key << '|' << slot.type << (slot.enabled ? 'y' : 'n') << slot.a << ',' << slot.b;
    if (key == renderer->lastKey) return;
    renderer->lastKey = key;

    juce::AudioBuffer<float> phrase;
    {
        const juce::ScopedLock lock(sourceLock);
        const int start = juce::jlimit(0, source.getNumSamples(), selectionStart.load());
        const int end = juce::jlimit(start, source.getNumSamples(), selectionEnd.load());
        phrase.setSize(2, end - start);
        for (int c = 0; c < 2 && end > start; ++c) phrase.copyFrom(c, 0, source, juce::jmin(c, source.getNumChannels() - 1), start, end - start);
    }
    auto next = std::make_shared<Result>();
    next->sampleRate = rate; next->seconds = s.seconds; next->bpm = currentBpm(); next->hostTempo = hostBpm.load() > 0;
    next->sync = value("sync") > 0.5f; next->division = static_cast<int>(value("division"));
    if (phrase.getNumSamples() > 0) next->swell = verseb::renderSwell(phrase, s);
    next->swellLength = next->swell.getNumSamples();
    next->preview.setSize(2, next->swellLength + phrase.getNumSamples());
    next->preview.clear();
    for (int c = 0; c < 2; ++c) {
        if (next->swellLength > 0) next->preview.copyFrom(c, 0, next->swell, c, 0, next->swellLength);
        if (phrase.getNumSamples() > 0) next->preview.copyFrom(c, next->swellLength, phrase, c, 0, phrase.getNumSamples());
    }
    std::shared_ptr<const Result> old;
    {
        const juce::SpinLock::ScopedLockType lock(resultLock);
        old = std::move(result);
        result = std::move(next);
        if (playPosition >= result->preview.getNumSamples()) playPosition = 0;
    }
    resultCounter.fetch_add(1);
}

std::shared_ptr<const VersebProcessor::Result> VersebProcessor::getResult() const {
    const juce::SpinLock::ScopedLockType lock(resultLock);
    return result;
}

void VersebProcessor::setPlaying(bool shouldPlay) {
    const juce::SpinLock::ScopedLockType lock(resultLock);
    playPosition = 0;
    playTime.store(0);
    playing.store(shouldPlay && result != nullptr && result->swellLength > 0);
}

bool VersebProcessor::writeWav(const juce::File& file, bool includePhrase) const {
    const auto r = getResult();
    if (r == nullptr || r->swellLength == 0) return false;
    file.deleteFile();
    juce::WavAudioFormat wav;
    return writeBuffer(wav, file.createOutputStream(), includePhrase ? r->preview : r->swell, r->sampleRate, 24);
}

juce::File VersebProcessor::exportForDrag() const {
    const auto r = getResult();
    if (r == nullptr || r->swellLength == 0) return {};
    const auto folder = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("Verseb");
    folder.createDirectory();
    juce::String length = r->sync ? juce::String(verseb::lengthNames[juce::jlimit(0, 6, r->division)]).replace("/", "-").replace(" ", "")
                                  : juce::String(r->seconds, 2) + "s";
    const auto file = folder.getChildFile("Verseb " + juce::String(verseb::reverbNames[static_cast<int>(value("reverb"))]) + " " + length + " "
        + juce::String(juce::roundToInt(r->bpm)) + "bpm " + juce::Time::getCurrentTime().formatted("%H%M%S") + ".wav");
    return writeWav(file, false) ? file : juce::File();
}

// ---------------------------------------------------------------- presets
int VersebProcessor::numFactoryPresets() const { return static_cast<int>(factoryPresets().size()); }

juce::File VersebProcessor::presetFolder() {
    return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory).getChildFile("Verseb/Presets");
}

juce::StringArray VersebProcessor::presetNames() const {
    juce::StringArray names;
    for (auto& p : factoryPresets()) names.add(p.name);
    auto files = presetFolder().findChildFiles(juce::File::findFiles, false, "*.xml");
    files.sort();
    for (auto& f : files) names.add(f.getFileNameWithoutExtension().toUpperCase());
    return names;
}

void VersebProcessor::loadPreset(int index) {
    const auto names = presetNames();
    if (!juce::isPositiveAndBelow(index, names.size())) return;
    undo.beginNewTransaction("Preset");
    if (index < numFactoryPresets()) {
        juce::XmlElement xml("VERSEB_PRESET");
        for (auto& [id, v] : factoryPresets()[static_cast<size_t>(index)].values) {
            auto* child = xml.createNewChildElement("PARAM"); child->setAttribute("id", id); child->setAttribute("value", v);
        }
        xml.setAttribute("order", "0,1,2,3,4");
        applyPresetXml(xml);
    } else {
        auto files = presetFolder().findChildFiles(juce::File::findFiles, false, "*.xml");
        files.sort();
        if (auto xml = juce::XmlDocument::parse(files[index - numFactoryPresets()])) applyPresetXml(*xml);
    }
    currentPreset = names[index];
}

void VersebProcessor::applyPresetXml(const juce::XmlElement& xml) {
    // Unlisted parameters return to their defaults; tempo is a session setting and is kept.
    for (auto* parameter : getParameters())
        if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*>(parameter); ranged && ranged->getParameterID() != "bpm") {
            float target = ranged->convertFrom0to1(ranged->getDefaultValue());
            for (auto* child : xml.getChildWithTagNameIterator("PARAM"))
                if (child->getStringAttribute("id") == ranged->getParameterID()) target = static_cast<float>(child->getDoubleAttribute("value"));
            setControlValue(ranged->getParameterID(), target);
        }
    parameters.copyState();  // flush into the current undo transaction now, not on the APVTS timer
    parameters.state.setProperty("order", xml.getStringAttribute("order", "0,1,2,3,4"), &undo);
}

bool VersebProcessor::saveUserPreset(const juce::String& name) {
    const auto clean = juce::File::createLegalFileName(name.trim());
    if (clean.isEmpty()) return false;
    juce::XmlElement xml("VERSEB_PRESET");
    for (auto* parameter : getParameters())
        if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*>(parameter); ranged && ranged->getParameterID() != "bpm") {
            auto* child = xml.createNewChildElement("PARAM");
            child->setAttribute("id", ranged->getParameterID());
            child->setAttribute("value", ranged->convertFrom0to1(ranged->getValue()));
        }
    xml.setAttribute("order", parameters.state.getProperty("order").toString());
    presetFolder().createDirectory();
    if (!xml.writeTo(presetFolder().getChildFile(clean + ".xml"))) return false;
    currentPreset = clean.toUpperCase();
    return true;
}

// ---------------------------------------------------------------- state
void VersebProcessor::getStateInformation(juce::MemoryBlock& data) {
    auto state = parameters.copyState();
    state.setProperty("preset", currentPreset, nullptr);
    juce::ValueTree audio("SOURCE");
    {
        const juce::ScopedLock lock(sourceLock);
        if (source.getNumSamples() > 0) {
            juce::MemoryBlock flac;
            {
                juce::FlacAudioFormat format;
                writeBuffer(format, std::make_unique<juce::MemoryOutputStream>(flac, false), source, sourceSampleRate.load(), 24);
            }
            audio.setProperty("flac", flac, nullptr);
            audio.setProperty("rate", sourceSampleRate.load(), nullptr);
            audio.setProperty("start", selectionStart.load(), nullptr);
            audio.setProperty("end", selectionEnd.load(), nullptr);
        }
    }
    state.appendChild(audio, nullptr);
    if (auto xml = state.createXml()) copyXmlToBinary(*xml, data);
}

void VersebProcessor::setStateInformation(const void* data, int size) {
    auto xml = getXmlFromBinary(data, size);
    if (xml == nullptr || !xml->hasTagName("VERSEB")) return;
    auto state = juce::ValueTree::fromXml(*xml);
    const auto audio = state.getChildWithName("SOURCE");
    state.removeChild(audio, nullptr);
    currentPreset = state.getProperty("preset", "DEFAULT").toString();
    parameters.replaceState(state);
    syncOrderFromTree();
    if (const auto* flac = audio.getProperty("flac").getBinaryData(); flac != nullptr && flac->getSize() > 0) {
        juce::FlacAudioFormat format;
        std::unique_ptr<juce::AudioFormatReader> reader(format.createReaderFor(new juce::MemoryInputStream(*flac, true), true));
        if (reader != nullptr && reader->lengthInSamples > 0) {
            const auto length = static_cast<int>(reader->lengthInSamples);
            juce::AudioBuffer<float> buffer(2, length);
            reader->read(&buffer, 0, length, 0, true, true);
            setSource(std::move(buffer), static_cast<double>(audio.getProperty("rate", reader->sampleRate)), false);
            setSelection(audio.getProperty("start", 0), audio.getProperty("end", length));
        }
    }
    undo.clearUndoHistory();
}

juce::AudioProcessorEditor* VersebProcessor::createEditor() { return new VersebEditor(*this); }
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new VersebProcessor(); }
