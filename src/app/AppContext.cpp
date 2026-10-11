#include "AppContext.h"
#include "core/Paths.h"
#include "LibraryService.h"
#include "StemService.h"
#include "ControllerManager.h"
#include "DjController.h"

namespace merge
{

AppContext::AppContext()
{
    juce::PropertiesFile::Options opts;
    opts.applicationName = "Merge";
    opts.filenameSuffix = ".settings";
    opts.folderName = paths::appDataDir().getFullPathName();
    opts.osxLibrarySubFolder = "Application Support";
    opts.storageFormat = juce::PropertiesFile::storeAsXML;
    properties.setStorageParameters (opts);

    project.setPluginHost (&plugins);
    project.onPublish = [this] (std::unique_ptr<BeatSnapshot> s) { engine.beat().publish (std::move (s)); };
    project.addChangeListener (this);

    engine.setDjCore (&dj);
    engine.setRecorder (&recorder);
    dj.recorder.store (&recorder);
    engine.beat().setClockSource (&dj.clockOut);
    engine.onAudioConfigChanged = [this] (double sr, int bs)
    {
        project.setAudioConfig (sr, bs);
        project.changed();
        if (decks != nullptr) decks->audioConfigChanged (sr, bs);
    };

    library = std::make_unique<LibraryService> (*this);
    stems = std::make_unique<StemService> (*this);
    decks = std::make_unique<DjController> (*this);
    controllers = std::make_unique<ControllerManager> (*this);

    auto saved = settings().getXmlValue ("audioDevice");
    engine.initialise (saved.get());
    project.setAudioConfig (engine.getSampleRate(), engine.getBlockSize());

    kits.scan();
    newProject();

    // restore an autosave if the last session didn't exit cleanly
    auto crashFlag = paths::autosaveDir().getChildFile ("running.flag");
    auto autosaveFile = paths::autosaveDir().getChildFile ("autosave.merge");
    if (crashFlag.existsAsFile() && autosaveFile.existsAsFile())
    {
        if (project.load (autosaveFile))
        {
            project.file = juce::File();
            project.name = "Recovered session";
            syncTransportFromProject();
            setStatus ("Recovered your last session after an unexpected exit.");
        }
    }
    crashFlag.create();

    startTimerHz (30);
}

AppContext::~AppContext()
{
    stopTimer();
    engine.beat().stop();
    if (auto xml = engine.saveState())
        settings().setValue ("audioDevice", xml.get());
    settings().saveIfNeeded();
    recorder.stop();
    controllers.reset();
    decks.reset();
    stems.reset();
    library.reset();
    engine.shutdown();
    engine.setDjCore (nullptr);
    engine.setRecorder (nullptr);
    paths::autosaveDir().getChildFile ("running.flag").deleteFile();
    project.removeChangeListener (this);
}

void AppContext::setStatus (const juce::String& text)
{
    status = text;
    sendChangeMessage();
}

void AppContext::selectPattern (int index)
{
    index = juce::jlimit (0, kNumPatterns - 1, index);
    auto& t = engine.beat().transport;
    if (! makeMode && t.playing.load())
        t.queuedPattern.store (index); // remix deck: launch on the next bar
    else
        t.currentPattern.store (index);
    selectionChanged.sendChangeMessage();
}

void AppContext::selectChannel (int id)
{
    selectedChannelId = id;
    const int index = project.indexOfChannel (id);
    auto* ch = project.findChannel (id);
    engine.keyboardChannel.store (ch != nullptr && ch->kind != ChannelKind::Sampler ? index : -1);
    selectionChanged.sendChangeMessage();
}

void AppContext::setBpm (double bpm)
{
    project.bpm = juce::jlimit (20.0, 400.0, bpm);
    engine.beat().transport.bpm.store (project.bpm);
    sendChangeMessage();
}

void AppContext::setSwing (double swing)
{
    project.swing = juce::jlimit (50.0, 75.0, swing);
    engine.beat().transport.swing.store (project.swing);
    sendChangeMessage();
}

void AppContext::togglePlay()
{
    if (isPlaying()) engine.beat().stop();
    else engine.beat().play();
    sendChangeMessage();
}

void AppContext::stop()
{
    engine.beat().stop();
    sendChangeMessage();
}

void AppContext::setSongMode (bool song)
{
    engine.beat().transport.songMode.store (song);
    sendChangeMessage();
}

void AppContext::syncTransportFromProject()
{
    auto& t = engine.beat().transport;
    t.bpm.store (project.bpm);
    t.swing.store (project.swing);
}

void AppContext::loadKit (const KitInfo& kit, bool withPattern)
{
    project.checkpoint();
    KitLibrary::applyKit (project, kit, withPattern, selectedPattern());
    syncTransportFromProject();
    projectEdited();
    setStatus ("Loaded kit " + kit.name + (kit.bpm > 0 ? " - " + juce::String (kit.bpm, 0) + " BPM" : juce::String()));
}

void AppContext::newProject()
{
    engine.beat().stop();
    project.resetToDefault();
    project.setAudioConfig (engine.getSampleRate(), engine.getBlockSize());
    selectedChannelId = project.getChannels().front().id;
    engine.beat().transport.currentPattern.store (0);
    engine.beat().transport.songMode.store (false);
    // Sound in seconds: start with the first factory kit and its groove.
    const auto& all = kits.getKits();
    if (! all.empty())
        KitLibrary::applyKit (project, all.front(), true, 0);
    syncTransportFromProject();
    project.changed();
    dirty = false;
    selectionChanged.sendChangeMessage();
    sendChangeMessage();
}

bool AppContext::openProject (const juce::File& f)
{
    engine.beat().stop();
    if (! project.load (f))
    {
        setStatus ("Couldn't open " + f.getFileName());
        return false;
    }
    if (! project.getChannels().empty())
        selectedChannelId = project.getChannels().front().id;
    syncTransportFromProject();
    dirty = false;
    selectionChanged.sendChangeMessage();
    setStatus ("Opened " + f.getFileName());
    return true;
}

bool AppContext::saveProject (const juce::File& f)
{
    if (! project.save (f))
    {
        setStatus ("Couldn't save " + f.getFullPathName());
        return false;
    }
    dirty = false;
    setStatus ("Saved " + f.getFileName());
    return true;
}

void AppContext::projectEdited (bool structural)
{
    dirty = true;
    project.changed (structural);
}

void AppContext::changeListenerCallback (juce::ChangeBroadcaster*)
{
    sendChangeMessage();
}

void AppContext::autosave()
{
    if (! dirty) return;
    project.save (paths::autosaveDir().getChildFile ("autosave.merge"));
    // keep the user's file association: autosave shouldn't rename the project
    dirty = false;
}

void AppContext::timerCallback()
{
    engine.beat().collectGarbage();

    // notes captured while recording
    RecordedNote r;
    bool any = false;
    while (engine.beat().popRecorded (r))
    {
        if (! any) project.checkpoint();
        project.applyRecorded (r);
        any = true;
    }
    if (any)
        projectEdited();

    const auto now = juce::Time::getMillisecondCounter();
    if (now - lastAutosave > 120000)
    {
        lastAutosave = now;
        const auto keepFile = project.file;
        const auto keepName = project.name;
        autosave();
        project.file = keepFile;
        project.name = keepName;
    }
}

} // namespace merge
