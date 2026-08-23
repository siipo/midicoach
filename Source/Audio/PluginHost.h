#pragma once

#include <JuceHeader.h>

namespace audio
{

/** Holds a hosted instrument plugin's own editor in a resizable window. */
class PluginEditorWindow : public juce::DocumentWindow
{
public:
    PluginEditorWindow (juce::AudioProcessorEditor* editor, const juce::String& title,
                        std::function<void()> onCloseCallback);

    void closeButtonPressed() override;

private:
    std::function<void()> onClose;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PluginEditorWindow)
};

//==============================================================================
/** Hosts a single VST3 instrument.

    VST3 only: JUCE ships the VST3 headers, whereas VST2 needs a Steinberg SDK
    that is no longer distributed. The plugin runs in place of - or alongside -
    the built-in piano, fed by exactly the same MIDI stream the keyboard and
    the MIDI input device produce.
*/
class PluginHost
{
public:
    PluginHost();
    ~PluginHost();

    void prepare (double sampleRate, int maximumBlockSize);
    void releaseResources();

    /** Audio thread. Renders the plugin into its own scratch buffer and mixes
        the result into the destination, so it sits alongside the built-in piano
        instead of overwriting it. Does nothing when no plugin is loaded. */
    void process (juce::AudioBuffer<float>& destination, juce::MidiBuffer& midi);

    bool hasPlugin() const;
    juce::String getPluginName() const;

    juce::AudioPluginFormatManager& getFormatManager() noexcept { return formatManager; }
    juce::KnownPluginList& getKnownPluginList() noexcept        { return knownPlugins; }
    juce::PropertiesFile* getPropertiesFile() const;
    juce::File getDeadMansPedalFile() const;

    /** Loads a plugin on the message thread. Returns an error string, or {} on
        success. */
    juce::String loadPlugin (const juce::PluginDescription& description);
    void unloadPlugin();

    void showEditor();
    void closeEditor();

    /** Called after the known plugin list changes so it survives a restart. */
    void savePluginList();
    void restorePluginList();

private:
    void resizeScratchBuffer();

    juce::AudioPluginFormatManager formatManager;
    juce::KnownPluginList knownPlugins;
    std::unique_ptr<juce::ApplicationProperties> applicationProperties;

    /** Guards the plugin pointer against the audio thread. Held only for the
        duration of a pointer swap or a processBlock call. */
    juce::CriticalSection pluginLock;
    std::unique_ptr<juce::AudioPluginInstance> plugin;

    /** Sized to whatever the loaded plugin asks for, always under the lock, so
        the audio thread never sees a buffer narrower than the plugin's bus. */
    juce::AudioBuffer<float> scratch;

    std::unique_ptr<PluginEditorWindow> editorWindow;

    double currentSampleRate = 44100.0;
    int currentBlockSize = 512;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PluginHost)
};

} // namespace audio
