#include "PresetManager.h"

// ---------------------------------------------------------------------------
// State versioning
// ---------------------------------------------------------------------------

void PresetManager::stampVersion(juce::ValueTree& state)
{
    state.setProperty("stateVersion", kCurrentStateVersion, nullptr);
}

int PresetManager::readVersion(const juce::XmlElement& xml)
{
    // Absent attribute = pre-versioning = v1.
    return xml.getIntAttribute("stateVersion", 1);
}

void PresetManager::migrateState(juce::ValueTree& state, int fromVersion)
{
    if (fromVersion >= kCurrentStateVersion)
        return;

    // v1 -> v2: `phaseVocoder` PARAM was removed when PhaseVocoder/FrequencyShifter
    // were folded into MusicalQuantizer. APVTS::replaceState silently ignores
    // unknown PARAM children, so old presets load without error — but we strip
    // the stale child so saved-and-re-saved state stays clean.
    if (fromVersion < 2)
    {
        for (int i = state.getNumChildren() - 1; i >= 0; --i)
        {
            auto child = state.getChild(i);
            if (child.hasType("PARAM") &&
                child.getProperty("id").toString() == "phaseVocoder")
            {
                state.removeChild(i, nullptr);
            }
        }
    }

    // Stamp after migration so the resulting tree is current-shape.
    stampVersion(state);
}

PresetManager::PresetManager(juce::AudioProcessorValueTreeState& apvtsRef)
    : apvts(apvtsRef)
{
    // Ensure user preset directory exists
    getUserPresetDirectory().createDirectory();

    initFactoryPresets();

    currentPresetName = factoryPresets.empty() ? "" : factoryPresets[0].name;
}

juce::File PresetManager::getUserPresetDirectory() const
{
#if JUCE_MAC
    return juce::File::getSpecialLocation(juce::File::userHomeDirectory)
        .getChildFile("Library/Audio/Presets/HarmonicTools/Holy Shifter");
#else
    return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
        .getChildFile("HarmonicTools/Holy Shifter/Presets");
#endif
}

juce::File PresetManager::getPresetFile(const juce::String& name) const
{
    return getUserPresetDirectory().getChildFile(name + ".xml");
}

juce::StringArray PresetManager::getUserPresetNames() const
{
    juce::StringArray names;
    auto dir = getUserPresetDirectory();
    if (dir.isDirectory())
    {
        for (const auto& entry : juce::RangedDirectoryIterator(dir, false, "*.xml"))
            names.add(entry.getFile().getFileNameWithoutExtension());
    }
    names.sort(true);
    return names;
}

juce::StringArray PresetManager::getAllPresetNames() const
{
    juce::StringArray names;

    // Factory presets first
    for (const auto& fp : factoryPresets)
        names.add(fp.name);

    // Then user presets
    auto userNames = getUserPresetNames();
    for (const auto& name : userNames)
    {
        if (!isFactoryPreset(name))
            names.add(name);
    }

    return names;
}

juce::String PresetManager::getCurrentPresetName() const
{
    return currentPresetName;
}

bool PresetManager::isFactoryPreset(const juce::String& name) const
{
    for (const auto& fp : factoryPresets)
        if (fp.name == name)
            return true;
    return false;
}

int PresetManager::getCurrentPresetIndex() const
{
    auto names = getAllPresetNames();
    return names.indexOf(currentPresetName);
}

int PresetManager::getTotalPresetCount() const
{
    return getAllPresetNames().size();
}

void PresetManager::savePreset(const juce::String& name)
{
    if (isFactoryPreset(name))
        return; // Cannot overwrite factory presets

    auto state = apvts.copyState();
    stampVersion(state);
    auto xml = state.createXml();
    if (xml != nullptr)
    {
        auto file = getPresetFile(name);
        file.getParentDirectory().createDirectory();
        file.replaceWithText(xml->toString());
        currentPresetName = name;
    }
}

void PresetManager::loadPreset(const juce::String& name)
{
    auto applyPresetXml = [this](const juce::XmlElement& xml)
    {
        auto tree = juce::ValueTree::fromXml(xml);
        migrateState(tree, readVersion(xml));
        apvts.replaceState(tree);
    };

    // Try factory presets first
    for (const auto& fp : factoryPresets)
    {
        if (fp.name == name)
        {
            auto xml = juce::XmlDocument::parse(fp.xmlData);
            if (xml != nullptr && xml->hasTagName(apvts.state.getType().toString()))
            {
                applyPresetXml(*xml);
                currentPresetName = name;
            }
            return;
        }
    }

    // Try user preset file
    auto file = getPresetFile(name);
    if (file.existsAsFile())
    {
        auto xml = juce::XmlDocument::parse(file);
        if (xml != nullptr && xml->hasTagName(apvts.state.getType().toString()))
        {
            applyPresetXml(*xml);
            currentPresetName = name;
        }
    }
}

void PresetManager::deletePreset(const juce::String& name)
{
    if (isFactoryPreset(name))
        return; // Cannot delete factory presets

    auto file = getPresetFile(name);
    if (file.existsAsFile())
    {
        file.deleteFile();

        // If we deleted the current preset, reset to first factory
        if (currentPresetName == name)
            currentPresetName = factoryPresets.empty() ? "" : factoryPresets[0].name;
    }
}

void PresetManager::loadNextPreset()
{
    auto names = getAllPresetNames();
    if (names.isEmpty()) return;

    int idx = names.indexOf(currentPresetName);
    int next = (idx + 1) % names.size();
    loadPreset(names[next]);
}

void PresetManager::loadPreviousPreset()
{
    auto names = getAllPresetNames();
    if (names.isEmpty()) return;

    int idx = names.indexOf(currentPresetName);
    int prev = (idx - 1 + names.size()) % names.size();
    loadPreset(names[prev]);
}

// ========================================================================
// Factory Presets
// ========================================================================
// Each preset is a full APVTS ValueTree XML snapshot.
// The tag name must match the APVTS Identifier ("FrequencyShifter").

void PresetManager::initFactoryPresets()
{
    // Helper: build a ValueTree with specific parameter values, defaulting the rest
    // We create presets by setting only the interesting parameters; the APVTS will
    // use defaults for anything not specified in the XML.

    auto makePreset = [&](const juce::String& name,
                          std::initializer_list<std::pair<juce::String, float>> params) -> FactoryPreset
    {
        // Start with current default state
        juce::ValueTree state(apvts.state.getType());

        // Add each parameter as a child
        for (const auto& [paramId, value] : params)
        {
            juce::ValueTree paramTree("PARAM");
            paramTree.setProperty("id", paramId, nullptr);
            paramTree.setProperty("value", value, nullptr);
            state.addChild(paramTree, -1, nullptr);
        }

        auto xml = state.createXml();
        return { name, xml != nullptr ? xml->toString() : "" };
    };

    // processingMode: 0=Classic, 1=Spectral
    // lfoShape: 0=Sine, 1=Tri, 2=Saw, 3=InvSaw, 4=Random
    // scaleNote0..11: per-note toggles (C=0, C#=1, ..., B=11)

    factoryPresets.push_back(makePreset("Init", {
        {"shiftHz", 0.0f},
        {"dryWet", 100.0f},
        {"processingMode", 1.0f},  // Spectral
        {"smear", 93.0f},
        {"quantizeStrength", 0.0f},
        {"lfoDepth", 0.0f},
        {"delayEnabled", 0.0f},
        {"maskEnabled", 0.0f},
        {"preserve", 0.0f},
        {"warm", 0.0f}
    }));

    factoryPresets.push_back(makePreset("Subtle Shimmer", {
        {"shiftHz", 3.0f},
        {"dryWet", 50.0f},
        {"processingMode", 1.0f},
        {"smear", 93.0f},
        {"quantizeStrength", 0.0f},
        {"lfoDepth", 0.5f},
        {"lfoRate", 0.15f},
        {"lfoShape", 0.0f},  // Sine
        {"lfoEnabled", 1.0f},  // R3 enable toggle — without this the LFO is silent
        {"delayEnabled", 0.0f},
        {"maskEnabled", 0.0f},
        {"warm", 0.0f}
    }));

    factoryPresets.push_back(makePreset("Deep Drift", {
        {"shiftHz", -7.0f},
        {"dryWet", 80.0f},
        {"processingMode", 1.0f},
        {"smear", 93.0f},
        {"quantizeStrength", 0.0f},
        {"lfoDepth", 2.0f},
        {"lfoRate", 0.3f},
        {"lfoShape", 0.0f},  // Sine
        {"lfoEnabled", 1.0f},  // R3 enable toggle — without this the LFO is silent
        {"delayEnabled", 0.0f},
        {"maskEnabled", 0.0f},
        {"warm", 1.0f}
    }));

    factoryPresets.push_back(makePreset("Chorus Wobble", {
        {"shiftHz", 5.0f},
        {"dryWet", 60.0f},
        {"processingMode", 0.0f},  // Classic
        {"quantizeStrength", 0.0f},
        {"lfoDepth", 3.0f},
        {"lfoRate", 0.8f},
        {"lfoShape", 0.0f},  // Sine
        {"lfoEnabled", 1.0f},  // R3 enable toggle — without this the LFO is silent
        {"delayEnabled", 1.0f},
        {"delayTime", 30.0f},
        {"delayFeedback", 20.0f},
        {"delayDamping", 40.0f},
        {"dlyLfoDepth", 8.0f},
        {"dlyLfoRate", 0.5f},
        {"dlyLfoShape", 0.0f},
        {"dlyLfoEnabled", 1.0f},  // R3 enable toggle — without this the delay LFO is silent
        {"maskEnabled", 0.0f},
        {"warm", 0.0f}
    }));

    factoryPresets.push_back(makePreset("Metallic Ring", {
        {"shiftHz", 200.0f},
        {"dryWet", 60.0f},
        {"processingMode", 1.0f},
        {"smear", 46.0f},
        {"quantizeStrength", 0.0f},
        {"lfoDepth", 0.0f},
        {"delayEnabled", 0.0f},
        {"maskEnabled", 1.0f},
        {"maskMode", 2.0f},  // BandPass
        {"maskLowFreq", 500.0f},
        {"maskHighFreq", 3000.0f},
        {"maskTransition", 1.0f},
        {"warm", 0.0f}
    }));

    factoryPresets.push_back(makePreset("Dark Submarine", {
        {"shiftHz", -50.0f},
        {"dryWet", 100.0f},
        {"processingMode", 1.0f},
        {"smear", 93.0f},
        {"quantizeStrength", 0.0f},
        {"lfoDepth", 0.0f},
        {"delayEnabled", 1.0f},
        {"delayTime", 400.0f},
        {"delayFeedback", 40.0f},
        {"delayDamping", 50.0f},
        {"delayGain", 0.0f},
        {"maskEnabled", 0.0f},
        {"warm", 1.0f}
    }));

    factoryPresets.push_back(makePreset("Tape Warble", {
        {"shiftHz", 0.0f},
        {"dryWet", 100.0f},
        {"processingMode", 0.0f},  // Classic
        {"quantizeStrength", 0.0f},
        {"lfoDepth", 0.0f},
        {"delayEnabled", 1.0f},
        {"delayTime", 250.0f},
        {"delayFeedback", 35.0f},
        {"delayDamping", 45.0f},
        {"dlyLfoDepth", 15.0f},
        {"dlyLfoRate", 0.4f},
        {"dlyLfoShape", 0.0f},
        {"dlyLfoEnabled", 1.0f},  // R3 enable toggle — without this the delay LFO is silent
        {"maskEnabled", 0.0f},
        {"warm", 1.0f}
    }));

    factoryPresets.push_back(makePreset("Alien Voice", {
        {"shiftHz", 500.0f},
        {"dryWet", 100.0f},
        {"processingMode", 1.0f},
        {"smear", 23.0f},
        {"quantizeStrength", 80.0f},
        // C Minor scale notes: C, D, Eb, F, G, Ab, Bb
        {"scaleNote0", 1.0f},   // C
        {"scaleNote1", 0.0f},   // C#
        {"scaleNote2", 1.0f},   // D
        {"scaleNote3", 1.0f},   // Eb
        {"scaleNote4", 0.0f},   // E
        {"scaleNote5", 1.0f},   // F
        {"scaleNote6", 0.0f},   // F#
        {"scaleNote7", 1.0f},   // G
        {"scaleNote8", 1.0f},   // Ab
        {"scaleNote9", 0.0f},   // A
        {"scaleNote10", 1.0f},  // Bb
        {"scaleNote11", 0.0f},  // B
        {"lfoDepth", 0.0f},
        {"delayEnabled", 0.0f},
        {"maskEnabled", 0.0f},
        {"warm", 0.0f}
    }));

    factoryPresets.push_back(makePreset("Spectral Freeze", {
        {"shiftHz", 0.5f},
        {"dryWet", 100.0f},
        {"processingMode", 1.0f},
        {"smear", 123.0f},
        {"quantizeStrength", 0.0f},
        {"lfoDepth", 0.0f},
        {"delayEnabled", 1.0f},
        {"delayTime", 500.0f},
        {"delayFeedback", 30.0f},
        {"delayDamping", 20.0f},
        {"delayGain", -3.0f},
        {"maskEnabled", 0.0f},
        {"warm", 0.0f}
    }));

    factoryPresets.push_back(makePreset("Cathedral", {
        {"shiftHz", 12.0f},
        {"dryWet", 70.0f},
        {"processingMode", 1.0f},
        {"smear", 93.0f},
        {"quantizeStrength", 0.0f},
        {"lfoDepth", 0.0f},
        {"delayEnabled", 1.0f},
        {"delayTime", 800.0f},
        {"delayFeedback", 50.0f},
        {"delayDamping", 60.0f},
        {"delaySlope", 20.0f},
        {"delayGain", -2.0f},
        {"maskEnabled", 0.0f},
        {"warm", 0.0f}
    }));
}
