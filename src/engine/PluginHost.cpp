#include "PluginHost.h"
#include "core/Paths.h"

namespace merge
{

static juce::File pluginListFile() { return paths::appDataDir().getChildFile ("plugins.xml"); }
static juce::File deadMansPedal() { return paths::appDataDir().getChildFile ("plugin-scan.crash"); }

PluginHost::PluginHost()
{
    juce::addDefaultFormatsToManager (formatManager);
    if (auto xml = juce::XmlDocument::parse (pluginListFile()))
        knownPlugins.recreateFromXml (*xml);
}

PluginHost::~PluginHost()
{
    stopTimer();
    scanner.reset();
}

void PluginHost::startScan()
{
    if (scanner != nullptr)
        return;
    for (int i = 0; i < formatManager.getNumFormats(); ++i)
    {
        auto* format = formatManager.getFormat (i);
        if (format->getName() != "VST3")
            continue;
        scanner = std::make_unique<juce::PluginDirectoryScanner> (knownPlugins, *format,
                                                                  format->getDefaultLocationsToSearch(),
                                                                  true, deadMansPedal(), true);
        scanProgress = 0.0f;
        startTimer (20);
        sendChangeMessage();
        return;
    }
}

void PluginHost::timerCallback()
{
    if (scanner == nullptr)
    {
        stopTimer();
        return;
    }
    juce::String name;
    currentFile = scanner->getNextPluginFileThatWillBeScanned();
    const bool more = scanner->scanNextFile (true, name);
    scanProgress = scanner->getProgress();
    if (! more)
    {
        scanner.reset();
        stopTimer();
        saveList();
        currentFile.clear();
    }
    sendChangeMessage();
}

void PluginHost::saveList()
{
    if (auto xml = knownPlugins.createXml())
        xml->writeTo (pluginListFile());
}

std::unique_ptr<juce::AudioPluginInstance> PluginHost::createInstance (const juce::PluginDescription& desc, double sr,
                                                                       int bs, juce::String& error)
{
    return formatManager.createPluginInstance (desc, sr, bs, error);
}

std::vector<juce::PluginDescription> PluginHost::getInstruments() const
{
    std::vector<juce::PluginDescription> v;
    for (auto& d : knownPlugins.getTypes())
        if (d.isInstrument) v.push_back (d);
    return v;
}

std::vector<juce::PluginDescription> PluginHost::getEffects() const
{
    std::vector<juce::PluginDescription> v;
    for (auto& d : knownPlugins.getTypes())
        if (! d.isInstrument) v.push_back (d);
    return v;
}

juce::var pluginStateToVar (juce::AudioPluginInstance& p)
{
    auto* obj = new juce::DynamicObject();
    if (auto xml = p.getPluginDescription().createXml())
        obj->setProperty ("description", xml->toString());
    juce::MemoryBlock mb;
    p.getStateInformation (mb);
    obj->setProperty ("state", mb.toBase64Encoding());
    return juce::var (obj);
}

static std::unique_ptr<juce::AudioPluginInstance> restorePlugin (PluginHost& host, const juce::var& state,
                                                                 double sr, int bs)
{
    auto xml = juce::XmlDocument::parse (state["description"].toString());
    if (xml == nullptr)
        return nullptr;
    juce::PluginDescription desc;
    if (! desc.loadFromXml (*xml))
        return nullptr;
    juce::String error;
    auto instance = host.createInstance (desc, sr, bs, error);
    if (instance == nullptr)
        return nullptr;
    juce::MemoryBlock mb;
    if (mb.fromBase64Encoding (state["state"].toString()) && mb.getSize() > 0)
        instance->setStateInformation (mb.getData(), (int) mb.getSize());
    return instance;
}

Instrument::Ptr PluginHost::wrapInstrument (std::unique_ptr<juce::AudioPluginInstance> p)
{
    if (p == nullptr) return nullptr;
    Instrument::Ptr inst (new PluginInstrument (std::move (p), *this));
    ReleasePool::get().add (inst);
    return inst;
}

Effect::Ptr PluginHost::wrapEffect (std::unique_ptr<juce::AudioPluginInstance> p)
{
    if (p == nullptr) return nullptr;
    Effect::Ptr fx (new PluginEffect (std::move (p), *this));
    ReleasePool::get().add (fx);
    return fx;
}

Instrument::Ptr PluginHost::createPluginInstrument (const juce::var& state, double sr, int bs)
{
    return wrapInstrument (restorePlugin (*this, state, sr, bs));
}

Effect::Ptr PluginHost::createPluginEffect (const juce::var& state, double sr, int bs)
{
    return wrapEffect (restorePlugin (*this, state, sr, bs));
}

//==============================================================================
PluginInstrument::PluginInstrument (std::unique_ptr<juce::AudioPluginInstance> p, PluginHost& h)
    : plugin (std::move (p)), host (h)
{
}

PluginInstrument::~PluginInstrument()
{
    if (plugin != nullptr)
        plugin->releaseResources();
}

void PluginInstrument::prepare (double sr, int bs)
{
    if (plugin == nullptr) return;
    maxBlock = juce::jmax (bs, 32);
    plugin->enableAllBuses();
    plugin->setRateAndBufferSizeDetails (sr, maxBlock);
    plugin->prepareToPlay (sr, maxBlock);
    temp.setSize (juce::jmax (2, plugin->getTotalNumInputChannels(), plugin->getTotalNumOutputChannels()), maxBlock);
    midiCopy.ensureSize (8192);
    preparedRate = sr;
}

void PluginInstrument::render (juce::AudioBuffer<float>& out, const juce::MidiBuffer& midi, int n)
{
    if (plugin == nullptr || preparedRate <= 0 || n > maxBlock)
        return;
    midiCopy.clear();
    if (pendingAllOff.exchange (false))
        for (int ch = 1; ch <= 16; ++ch)
            midiCopy.addEvent (juce::MidiMessage::allNotesOff (ch), 0);
    midiCopy.addEvents (midi, 0, n, 0);
    juce::AudioBuffer<float> view (temp.getArrayOfWritePointers(), temp.getNumChannels(), n);
    view.clear();
    plugin->processBlock (view, midiCopy);
    out.addFrom (0, 0, view, 0, 0, n);
    out.addFrom (1, 0, view, juce::jmin (1, view.getNumChannels() - 1), 0, n);
}

Instrument::Ptr PluginInstrument::clone() const
{
    if (plugin == nullptr) return nullptr;
    auto state = pluginStateToVar (*plugin);
    return host.createPluginInstrument (state, preparedRate > 0 ? preparedRate : 44100.0, juce::jmax (512, maxBlock));
}

juce::var PluginInstrument::saveState() const
{
    return plugin != nullptr ? pluginStateToVar (*plugin) : juce::var();
}

//==============================================================================
PluginEffect::PluginEffect (std::unique_ptr<juce::AudioPluginInstance> p, PluginHost& h)
    : plugin (std::move (p)), host (h)
{
}

PluginEffect::~PluginEffect()
{
    if (plugin != nullptr)
        plugin->releaseResources();
}

void PluginEffect::prepare (double sr, int bs)
{
    if (plugin == nullptr) return;
    maxBlock = juce::jmax (bs, 32);
    plugin->enableAllBuses();
    plugin->setRateAndBufferSizeDetails (sr, maxBlock);
    plugin->prepareToPlay (sr, maxBlock);
    temp.setSize (juce::jmax (2, plugin->getTotalNumInputChannels(), plugin->getTotalNumOutputChannels()), maxBlock);
    emptyMidi.ensureSize (256);
    preparedRate = sr;
}

void PluginEffect::process (juce::AudioBuffer<float>& buffer, int n, const EffectContext&)
{
    if (plugin == nullptr || preparedRate <= 0 || n > maxBlock)
        return;
    juce::AudioBuffer<float> view (temp.getArrayOfWritePointers(), temp.getNumChannels(), n);
    view.clear();
    view.copyFrom (0, 0, buffer, 0, 0, n);
    view.copyFrom (1, 0, buffer, 1, 0, n);
    emptyMidi.clear();
    plugin->processBlock (view, emptyMidi);
    buffer.copyFrom (0, 0, view, 0, 0, n);
    buffer.copyFrom (1, 0, view, juce::jmin (1, view.getNumChannels() - 1), 0, n);
}

Effect::Ptr PluginEffect::clone() const
{
    if (plugin == nullptr) return nullptr;
    auto state = pluginStateToVar (*plugin);
    return host.createPluginEffect (state, preparedRate > 0 ? preparedRate : 44100.0, juce::jmax (512, maxBlock));
}

juce::var PluginEffect::saveState() const
{
    return plugin != nullptr ? pluginStateToVar (*plugin) : juce::var();
}

} // namespace merge
