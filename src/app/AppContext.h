#pragma once

#include "engine/AudioEngine.h"
#include "engine/Project.h"
#include "engine/KitLibrary.h"
#include "engine/PluginHost.h"
#include "dj/DjCore.h"
#include "dj/Recorder.h"

namespace merge
{

class LibraryService;
class StemService;
class ControllerManager;
class DjController;

/** Everything the app owns, created once at startup and shared by the UI. */
class AppContext : public juce::ChangeBroadcaster, private juce::Timer, private juce::ChangeListener
{
public:
    AppContext();
    ~AppContext() override;

    juce::PropertiesFile& settings() { return *properties.getUserSettings(); }

    AudioEngine engine;
    Project project;
    KitLibrary kits;
    PluginHost plugins;
    DjCore dj;
    Recorder recorder;
    std::unique_ptr<LibraryService> library;
    std::unique_ptr<StemService> stems;
    std::unique_ptr<DjController> decks;
    std::unique_ptr<ControllerManager> controllers;

    // --- beatmaker helpers
    int selectedChannelId = 1;
    int selectedPattern() const { return engine.beat().transport.currentPattern.load(); }
    void selectPattern (int index);
    void selectChannel (int id);
    void setBpm (double bpm);
    void setSwing (double swing);
    void togglePlay();
    void stop();
    bool isPlaying() const { return engine.beat().transport.playing.load(); }
    void setSongMode (bool song);
    void loadKit (const KitInfo& kit, bool withPattern);
    void newProject();
    bool openProject (const juce::File& f);
    bool saveProject (const juce::File& f);
    void projectEdited (bool structural = true);
    void syncTransportFromProject();

    /** Status line text shown at the bottom of the window. */
    void setStatus (const juce::String& text);
    juce::String getStatus() const { return status; }

    /** Selection / transport changes (pattern, channel...) for views to refresh. */
    juce::ChangeBroadcaster selectionChanged;

    bool makeMode = true;
    std::function<void (bool make)> onModeChange;
    /** Ask for stem separation of a deck's track (set by the stem service). */
    std::function<void (int deck)> stemsRequest;
    /** Flip a deck's loop/bars onto the beatmaker pads (set by the stem service). */
    std::function<void (int deck)> flipRequest;
    /** Split stems for a library track (pre-processing). */
    std::function<void (juce::int64 trackId)> stemsForTrack;

private:
    void timerCallback() override;
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void autosave();

    juce::ApplicationProperties properties;
    juce::String status;
    juce::uint32 lastAutosave = 0;
    bool dirty = false;
};

} // namespace merge
