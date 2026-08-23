#include "PluginHost.h"

namespace audio
{

PluginEditorWindow::PluginEditorWindow (juce::AudioProcessorEditor* editor,
                                        const juce::String& title,
                                        std::function<void()> onCloseCallback)
    : juce::DocumentWindow (title,
                            juce::Desktop::getInstance().getDefaultLookAndFeel()
                                .findColour (juce::ResizableWindow::backgroundColourId),
                            juce::DocumentWindow::closeButton),
      onClose (std::move (onCloseCallback))
{
    setUsingNativeTitleBar (true);
    setContentOwned (editor, true);
    setResizable (editor->isResizable(), false);
    centreWithSize (getWidth(), getHeight());
    setVisible (true);
}

void PluginEditorWindow::closeButtonPressed()
{
    if (onClose != nullptr)
        onClose();
}

//==============================================================================
PluginHost::PluginHost()
{
    formatManager.addDefaultFormats();

    juce::PropertiesFile::Options options;
    options.applicationName     = "MidiCoach";
    options.filenameSuffix      = ".settings";
    options.folderName          = "MidiCoach";
    options.osxLibrarySubFolder = "Application Support";

    applicationProperties = std::make_unique<juce::ApplicationProperties>();
    applicationProperties->setStorageParameters (options);

    restorePluginList();
}

PluginHost::~PluginHost()
{
    closeEditor();
    unloadPlugin();
}

juce::PropertiesFile* PluginHost::getPropertiesFile() const
{
    return applicationProperties->getUserSettings();
}

juce::File PluginHost::getDeadMansPedalFile() const
{
    return getPropertiesFile()->getFile().getSiblingFile ("MidiCoachPluginScan.tmp");
}

//==============================================================================
void PluginHost::prepare (double sampleRate, int maximumBlockSize)
{
    currentSampleRate = sampleRate;
    currentBlockSize  = maximumBlockSize;

    const juce::ScopedLock sl (pluginLock);

    if (plugin != nullptr)
    {
        plugin->prepareToPlay (sampleRate, maximumBlockSize);
        resizeScratchBuffer();
    }
}

/** Must be called with pluginLock held. */
void PluginHost::resizeScratchBuffer()
{
    const auto channels = plugin != nullptr
        ? juce::jmax (2, plugin->getTotalNumInputChannels(), plugin->getTotalNumOutputChannels())
        : 2;

    scratch.setSize (channels, juce::jmax (1, currentBlockSize), false, true, true);
}

void PluginHost::releaseResources()
{
    const juce::ScopedLock sl (pluginLock);

    if (plugin != nullptr)
        plugin->releaseResources();
}

void PluginHost::process (juce::AudioBuffer<float>& destination, juce::MidiBuffer& midi)
{
    const juce::ScopedTryLock sl (pluginLock);

    // If the message thread is mid-swap, skip this block rather than block the
    // audio thread waiting for it.
    if (! sl.isLocked() || plugin == nullptr)
        return;

    const auto numSamples = destination.getNumSamples();

    if (numSamples <= 0 || numSamples > scratch.getNumSamples())
        return;

    scratch.clear (0, numSamples);

    // Hand the plugin a view onto the scratch buffer that is exactly this
    // block long, without copying or reallocating anything.
    juce::AudioBuffer<float> view (scratch.getArrayOfWritePointers(),
                                   scratch.getNumChannels(), numSamples);

    plugin->processBlock (view, midi);

    const auto channelsToMix = juce::jmin (destination.getNumChannels(), scratch.getNumChannels());

    for (int channel = 0; channel < channelsToMix; ++channel)
        destination.addFrom (channel, 0, scratch, channel, 0, numSamples);
}

bool PluginHost::hasPlugin() const
{
    const juce::ScopedLock sl (pluginLock);
    return plugin != nullptr;
}

juce::String PluginHost::getPluginName() const
{
    const juce::ScopedLock sl (pluginLock);
    return plugin != nullptr ? plugin->getName() : juce::String();
}

//==============================================================================
juce::String PluginHost::loadPlugin (const juce::PluginDescription& description)
{
    juce::String error;

    auto instance = formatManager.createPluginInstance (description, currentSampleRate,
                                                        currentBlockSize, error);

    if (instance == nullptr)
        return error.isNotEmpty() ? error : juce::String ("Could not create the plugin.");

    // An instrument has no audio input and has to produce at least stereo out.
    instance->setPlayConfigDetails (0, juce::jmax (2, instance->getTotalNumOutputChannels()),
                                    currentSampleRate, currentBlockSize);
    instance->prepareToPlay (currentSampleRate, currentBlockSize);

    closeEditor();

    std::unique_ptr<juce::AudioPluginInstance> previous;

    {
        const juce::ScopedLock sl (pluginLock);
        previous = std::move (plugin);
        plugin = std::move (instance);
        resizeScratchBuffer();
    }

    // Destroy the old plugin outside the lock - releasing it can be slow.
    if (previous != nullptr)
        previous->releaseResources();

    return {};
}

void PluginHost::unloadPlugin()
{
    closeEditor();

    std::unique_ptr<juce::AudioPluginInstance> previous;

    {
        const juce::ScopedLock sl (pluginLock);
        previous = std::move (plugin);
    }

    if (previous != nullptr)
        previous->releaseResources();
}

//==============================================================================
void PluginHost::showEditor()
{
    if (editorWindow != nullptr)
    {
        editorWindow->toFront (true);
        return;
    }

    juce::AudioProcessor* processor = nullptr;

    {
        const juce::ScopedLock sl (pluginLock);
        processor = plugin.get();
    }

    if (processor == nullptr)
        return;

    if (auto* editor = processor->createEditorIfNeeded())
        editorWindow = std::make_unique<PluginEditorWindow> (editor, processor->getName(),
                                                             [this] { closeEditor(); });
}

void PluginHost::closeEditor()
{
    editorWindow.reset();
}

//==============================================================================
void PluginHost::savePluginList()
{
    if (auto* settings = getPropertiesFile())
    {
        if (auto xml = knownPlugins.createXml())
            settings->setValue ("pluginList", xml.get());

        settings->saveIfNeeded();
    }
}

void PluginHost::restorePluginList()
{
    if (auto* settings = getPropertiesFile())
        if (auto xml = settings->getXmlValue ("pluginList"))
            knownPlugins.recreateFromXml (*xml);
}

} // namespace audio
