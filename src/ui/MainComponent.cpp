#include "MainComponent.h"
#include <juce_audio_utils/juce_audio_utils.h>
#include "core/Paths.h"
#include "engine/OfflineRenderer.h"
#include "ui/common/PluginWindow.h"
#include "app/LibraryService.h"
#include "app/DjController.h"

namespace merge
{

namespace
{
    /** Renders on a background thread with a progress window, then writes the file. */
    class ExportJob : public juce::ThreadWithProgressWindow
    {
    public:
        ExportJob (std::unique_ptr<Project> c, RenderOptions o, juce::File f)
            : juce::ThreadWithProgressWindow ("Exporting " + f.getFileName(), true, true), clone (std::move (c)), opts (o), file (f) {}

        void run() override
        {
            auto audio = renderClone (*clone, opts, [this] (float p) { setProgress (p); return ! threadShouldExit(); });
            if (threadShouldExit()) return;
            setStatusMessage ("Writing file...");
            ok = writeAudioFile (file, audio, opts.sampleRate, 24);
        }

        std::unique_ptr<Project> clone;
        RenderOptions opts;
        juce::File file;
        bool ok = false;
    };

    /** Audio/MIDI device settings plus a few DJ routing options. */
    class SettingsPanel : public juce::Component
    {
    public:
        explicit SettingsPanel (AppContext& c)
            : ctx (c), selector (c.engine.getDeviceManager(), 0, 2, 2, 4, true, false, true, false)
        {
            addAndMakeVisible (selector);
            splitCue.setToggleState (ctx.engine.splitCue.load(), juce::dontSendNotification);
            splitCue.onClick = [this] { ctx.engine.splitCue.store (splitCue.getToggleState()); ctx.settings().setValue ("splitCue", splitCue.getToggleState()); };
            addAndMakeVisible (splitCue);
            padsMidi.setToggleState (ctx.engine.padsFromMidi.load(), juce::dontSendNotification);
            padsMidi.onClick = [this] { ctx.engine.padsFromMidi.store (padsMidi.getToggleState()); };
            addAndMakeVisible (padsMidi);
            info.setText ("Outputs 1-2 are the master. Choose an interface with 4 outputs to get headphone cueing on 3-4, "
                          "or use split cue with a stereo Y-cable.", juce::dontSendNotification);
            info.setFont (theme::font (12.0f));
            info.setColour (juce::Label::textColourId, theme::textDim);
            addAndMakeVisible (info);
            setSize (560, 560);
        }

        void resized() override
        {
            auto r = getLocalBounds().reduced (10);
            auto bottom = r.removeFromBottom (90);
            info.setBounds (bottom.removeFromTop (36));
            splitCue.setBounds (bottom.removeFromTop (26));
            padsMidi.setBounds (bottom.removeFromTop (26));
            selector.setBounds (r);
        }

    private:
        AppContext& ctx;
        juce::AudioDeviceSelectorComponent selector;
        juce::ToggleButton splitCue { "Split cue: headphone cue on the left channel, master on the right" };
        juce::ToggleButton padsMidi { "MIDI notes 36-51 play the pads" };
        juce::Label info;
    };
}

MainComponent::MainComponent()
{
    ctx = std::make_unique<AppContext>();
    ctx->engine.splitCue.store (ctx->settings().getBoolValue ("splitCue", false));

    topBar = std::make_unique<TopBar> (*ctx);
    makeView = std::make_unique<MakeView> (*ctx);
    mixView = std::make_unique<MixView> (*ctx);
    addAndMakeVisible (*topBar);
    addAndMakeVisible (*makeView);
    addChildComponent (*mixView);

    statusBar.setFont (theme::font (12.0f));
    statusBar.setColour (juce::Label::textColourId, theme::textDim);
    addAndMakeVisible (statusBar);

    topBar->onMenu = [this] { showMainMenu(); };
    makeView->onSendToMix = [this] { sendToMix(); };
    mixView->onRecordSet = [this] { toggleSetRecording(); };
    topBar->onSave = [this] { saveProject (false); };
    ctx->onModeChange = [this] (bool make) { setMode (make); };
    ctx->addChangeListener (this);
    ctx->setStatus ("Welcome to Merge. Press Space to play, tap Z X C V / A S D F / Q W E R / 1 2 3 4 to play the pads.");

    setWantsKeyboardFocus (true);
    setSize (1440, 900);
    startTimer (500);
}

MainComponent::~MainComponent()
{
    PluginWindow::closeAll();
    ctx->removeChangeListener (this);
    mixView.reset();
    makeView.reset();
    topBar.reset();
    ctx.reset();
}

void MainComponent::changeListenerCallback (juce::ChangeBroadcaster*)
{
    statusBar.setText (ctx->getStatus(), juce::dontSendNotification);
}

void MainComponent::timerCallback()
{
    if (! hasKeyboardFocus (true) && isShowing() && juce::Component::getCurrentlyFocusedComponent() == nullptr)
        grabKeyboardFocus();
}

void MainComponent::setMode (bool make)
{
    ctx->makeMode = make;
    makeView->setVisible (make);
    mixView->setVisible (! make);
    // In Mix mode the beatmaker becomes a remix deck on its own mixer channel.
    if (make)
        ctx->engine.beatLevel.store (1.0f);
    topBar->refresh();
    ctx->selectionChanged.sendChangeMessage();
    ctx->setStatus (make ? "Make mode: build beats with pads, steps, piano roll and song."
                         : "Mix mode: load tracks onto decks, sync, cue, loop, split stems and flip them onto pads.");
}

void MainComponent::paint (juce::Graphics& g)
{
    g.fillAll (theme::bg0);
}

void MainComponent::resized()
{
    auto r = getLocalBounds();
    topBar->setBounds (r.removeFromTop (54));
    statusBar.setBounds (r.removeFromBottom (24).reduced (10, 0));
    makeView->setBounds (r);
    mixView->setBounds (r);
}

bool MainComponent::keyPressed (const juce::KeyPress& key)
{
    const auto mods = key.getModifiers();
    if (mods.isCommandDown())
    {
        const auto c = juce::CharacterFunctions::toLowerCase (key.getTextCharacter() != 0 ? key.getTextCharacter() : (juce::juce_wchar) key.getKeyCode());
        if (c == 's') { saveProject (mods.isShiftDown()); return true; }
        if (c == 'o') { openProjectDialog(); return true; }
        if (c == 'n') { ctx->newProject(); return true; }
        if (c == 'z') { mods.isShiftDown() ? ctx->project.redo() : ctx->project.undo(); return true; }
        if (c == 'y') { ctx->project.redo(); return true; }
        if (c == 'e') { exportAudio (ctx->engine.beat().transport.songMode.load(), ".wav"); return true; }
        if (c == 'r')
        {
            auto& t = ctx->engine.beat().transport;
            t.recording.store (! t.recording.load());
            topBar->refresh();
            return true;
        }
        return false;
    }

    if (key == juce::KeyPress::spaceKey)
    {
        ctx->togglePlay();
        topBar->refresh();
        return true;
    }
    if (key == juce::KeyPress::tabKey)
    {
        setMode (! ctx->makeMode);
        return true;
    }
    if (ctx->makeMode)
    {
        const int pad = PadGrid::padForKey (key.getTextCharacter() != 0 ? key.getTextCharacter() : (juce::juce_wchar) key.getKeyCode());
        if (pad >= 0)
        {
            if (std::find (heldPadKeys.begin(), heldPadKeys.end(), pad) == heldPadKeys.end())
            {
                heldPadKeys.push_back (pad);
                ctx->engine.beat().noteOn (pad, -1, mods.isShiftDown() ? 1.0f : 0.85f);
            }
            return true;
        }
        if (key.getKeyCode() >= '1' && key.getKeyCode() <= '9' && mods.isAltDown())
        {
            ctx->selectPattern (key.getKeyCode() - '1');
            return true;
        }
    }
    return false;
}

bool MainComponent::keyStateChanged (bool isKeyDown)
{
    if (isKeyDown) return false;
    static const char* padKeys = "ZXCVASDFQWER1234";
    for (auto it = heldPadKeys.begin(); it != heldPadKeys.end();)
    {
        const int pad = *it;
        const auto ch = (int) padKeys[pad];
        const bool down = juce::KeyPress::isKeyCurrentlyDown (ch) || juce::KeyPress::isKeyCurrentlyDown (juce::CharacterFunctions::toLowerCase ((juce::juce_wchar) ch));
        if (! down)
        {
            ctx->engine.beat().noteOff (pad, -1);
            it = heldPadKeys.erase (it);
        }
        else
        {
            ++it;
        }
    }
    return false;
}

void MainComponent::showMainMenu()
{
    juce::PopupMenu m;
    m.addSectionHeader ("Project");
    m.addItem (1, "New project");
    m.addItem (2, "Open...");
    m.addItem (3, "Save");
    m.addItem (4, "Save as...");
    m.addSeparator();
    juce::PopupMenu exp;
    exp.addItem (10, "Pattern loop as WAV...");
    exp.addItem (11, "Song as WAV...");
    exp.addItem (12, "Song as FLAC...");
    exp.addItem (13, "Song as OGG...");
    m.addSubMenu ("Export", exp);
    m.addItem (14, "Send beat to Mix library");
    m.addSeparator();
    m.addItem (15, "Import Rekordbox collection (XML)...");
    m.addItem (16, ctx->recorder.isRecording() ? "Stop recording set" : "Record DJ set");
    m.addItem (20, "Audio & MIDI settings...");
    m.addItem (21, "Rescan kits and samples");
    m.addItem (22, "Scan VST3 plugins");
    m.addItem (23, "Open my Merge folder");
    m.addSeparator();
    m.addItem (30, "Keyboard shortcuts");
    m.addItem (31, "About Merge");

    showMenu (m, &topBar->getMenuButton(), [this] (int r)
    {
        switch (r)
        {
            case 1: ctx->newProject(); break;
            case 2: openProjectDialog(); break;
            case 3: saveProject (false); break;
            case 4: saveProject (true); break;
            case 10: exportAudio (false, ".wav"); break;
            case 11: exportAudio (true, ".wav"); break;
            case 12: exportAudio (true, ".flac"); break;
            case 13: exportAudio (true, ".ogg"); break;
            case 14: sendToMix(); break;
            case 15:
            {
                chooser = std::make_shared<juce::FileChooser> ("Rekordbox collection XML", juce::File(), "*.xml");
                chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                                      [this] (const juce::FileChooser& fc)
                                      {
                                          if (! fc.getResult().existsAsFile()) return;
                                          auto r = ctx->library->importRekordbox (fc.getResult());
                                          ctx->setStatus ("Imported " + juce::String (r.tracks) + " tracks and " + juce::String (r.playlists) + " playlists from Rekordbox.");
                                      });
                break;
            }
            case 16: toggleSetRecording(); break;
            case 20: showSettings(); break;
            case 21: ctx->kits.scan(); ctx->setStatus ("Rescanned kits: " + juce::String ((int) ctx->kits.getKits().size()) + " found."); break;
            case 22: ctx->plugins.startScan(); ctx->setStatus ("Scanning VST3 plugins..."); break;
            case 23: paths::userDir().startAsProcess(); break;
            case 30: showShortcuts(); break;
            case 31:
                juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::InfoIcon, "Merge " MERGE_VERSION_STRING,
                    "The beatmaker and DJ app in one.\n\nFree and open source under the GNU GPL v3.\n"
                    "Factory sounds are original and released as CC0.\n\nBuilt with JUCE, Signalsmith Stretch and SQLite.");
                break;
            default: break;
        }
    });
}

void MainComponent::saveProject (bool saveAs)
{
    if (! saveAs && ctx->project.file.existsAsFile())
    {
        ctx->saveProject (ctx->project.file);
        topBar->repaint();
        return;
    }
    chooser = std::make_shared<juce::FileChooser> ("Save project",
                                                   paths::projectsDir().getChildFile (ctx->project.name + ".merge"), "*.merge");
    chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting,
                          [this] (const juce::FileChooser& fc)
                          {
                              auto f = fc.getResult();
                              if (f == juce::File()) return;
                              ctx->saveProject (f.withFileExtension (".merge"));
                              topBar->repaint();
                          });
}

void MainComponent::openProjectDialog()
{
    chooser = std::make_shared<juce::FileChooser> ("Open project", paths::projectsDir(), "*.merge");
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [this] (const juce::FileChooser& fc)
                          {
                              if (fc.getResult().existsAsFile())
                                  openFile (fc.getResult());
                          });
}

void MainComponent::openFile (const juce::File& f)
{
    if (f.hasFileExtension ("merge"))
    {
        ctx->openProject (f);
        topBar->refresh();
        setMode (true);
    }
}

void MainComponent::exportAudio (bool song, const juce::String& ext)
{
    const auto suggested = paths::beatsDir().getChildFile (ctx->project.name + (song ? "" : " (pattern " + Project::patternName (ctx->selectedPattern()) + ")") + ext);
    chooser = std::make_shared<juce::FileChooser> ("Export audio", suggested, "*" + ext);
    chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting,
                          [this, song, ext] (const juce::FileChooser& fc)
                          {
                              auto f = fc.getResult();
                              if (f == juce::File()) return;
                              f = f.withFileExtension (ext);
                              RenderOptions o;
                              o.sampleRate = ctx->engine.getSampleRate();
                              o.songMode = song;
                              o.pattern = ctx->selectedPattern();
                              o.loops = song ? 1 : 4;
                              runExport (o, f, [this, f] (bool ok)
                              {
                                  ctx->setStatus (ok ? "Exported " + f.getFullPathName() : "Export failed");
                                  if (ok) f.revealToUser();
                              });
                          });
}

void MainComponent::runExport (RenderOptions o, juce::File f, std::function<void (bool)> onDone)
{
    auto* job = new ExportJob (ctx->project.cloneForRender(), o, f);
    job->launchThread();
    auto checker = std::make_shared<std::function<void()>>();
    *checker = [job, checker, onDone]
    {
        if (job->isThreadRunning())
        {
            juce::Timer::callAfterDelay (150, *checker);
            return;
        }
        const bool ok = job->ok;
        delete job;
        if (onDone) onDone (ok);
    };
    juce::Timer::callAfterDelay (150, *checker);
}

void MainComponent::sendToMix()
{
    const bool song = ctx->engine.beat().transport.songMode.load() && ctx->project.getClips().size() > 1;
    const auto name = ctx->project.name == "Untitled" ? "Beat " + juce::Time::getCurrentTime().formatted ("%Y-%m-%d %H%M") : ctx->project.name;
    auto file = paths::beatsDir().getNonexistentChildFile (juce::File::createLegalFileName (name), ".wav");
    RenderOptions o;
    o.sampleRate = ctx->engine.getSampleRate();
    o.songMode = song;
    o.pattern = ctx->selectedPattern();
    o.loops = song ? 1 : 16; // 16 loops of the pattern = a DJ-friendly length
    o.tailSeconds = 0.5;
    const double bpm = ctx->project.bpm;
    const int key = ctx->project.key.toIndex();
    runExport (o, file, [this, file, bpm, key, name] (bool ok)
    {
        if (! ok) { ctx->setStatus ("Couldn't render the beat"); return; }
        ctx->library->addBeat (file, bpm, key, name);
        ctx->setStatus ("Sent \"" + name + "\" to the Mix library (My Beats) - " + juce::String (bpm, 1) + " BPM. Switch to MIX to play it.");
    });
}

void MainComponent::showSettings()
{
    juce::DialogWindow::LaunchOptions o;
    o.content.setOwned (new SettingsPanel (*ctx));
    o.dialogTitle = "Audio & MIDI settings";
    o.dialogBackgroundColour = theme::bg1;
    o.useNativeTitleBar = true;
    o.resizable = true;
    o.launchAsync();
}

void MainComponent::showShortcuts()
{
    juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::NoIcon, "Keyboard shortcuts",
        "Space - play / pause\n"
        "Tab - switch Make / Mix\n"
        "Z X C V / A S D F / Q W E R / 1 2 3 4 - pads 1-16 (Shift = accent)\n"
        "Alt+1..9 - select pattern\n"
        "Ctrl+Z / Ctrl+Y - undo / redo\n"
        "Ctrl+S / Ctrl+Shift+S - save / save as\n"
        "Ctrl+O - open,  Ctrl+N - new\n"
        "Ctrl+E - export,  Ctrl+R - record\n\n"
        "Step grid: click = on/off, Shift+click = accent, right-click = clear, drag = paint, wheel = velocity\n"
        "Piano roll: click = draw, drag = move/resize, right-click = delete, Delete key = remove selected\n"
        "Song: click a lane to place the selected pattern, drag in the ruler for a loop region");
}

void MainComponent::toggleSetRecording()
{
    auto& rec = ctx->recorder;
    if (rec.isRecording())
    {
        rec.stop();
        const auto folder = rec.getFolder();
        const double bpm = rec.bpmAtStart;
        ctx->setStatus ("Set saved to " + folder.getFullPathName());
        auto* aw = new juce::AlertWindow ("Set recorded", "Your set (master and each deck) was saved to:\n" + folder.getFullPathName(),
                                          juce::MessageBoxIconType::NoIcon);
        aw->addButton ("Open in Make (as stems)", 1);
        aw->addButton ("Show files", 2);
        aw->addButton ("Close", 0, juce::KeyPress (juce::KeyPress::escapeKey));
        aw->enterModalState (true, juce::ModalCallbackFunction::create ([this, folder, bpm] (int r)
        {
            if (r == 1) openSetInMake (folder, bpm);
            else if (r == 2) folder.getChildFile ("master.wav").revealToUser();
        }), true);
        return;
    }
    const auto folder = paths::recordingsDir().getChildFile ("Set " + juce::Time::getCurrentTime().formatted ("%Y-%m-%d %H-%M-%S"));
    const double bpm = ctx->dj.clockOut.valid.load() ? ctx->dj.clockOut.bpm.load() : ctx->project.bpm;
    rec.bpmAtStart = bpm;
    if (rec.start (folder, ctx->engine.getSampleRate(), true, ctx->dj.numDecks.load()))
        ctx->setStatus ("Recording your set to " + folder.getFullPathName());
    else
        ctx->setStatus ("Couldn't start recording in " + folder.getFullPathName());
}

void MainComponent::openSetInMake (const juce::File& folder, double bpm)
{
    ctx->project.checkpoint();
    ctx->project.getAudioClips().clear();
    ctx->project.getClips().clear();
    ctx->project.bpm = bpm > 0 ? bpm : 120.0;
    int lane = 0;
    for (int d = 0; d < 4; ++d)
    {
        auto f = folder.getChildFile ("deck-" + juce::String (d + 1) + ".wav");
        if (! f.existsAsFile()) continue;
        auto sample = SampleCache::get().load (f);
        if (sample == nullptr || sample->getAudio().getMagnitude (0, sample->getNumFrames()) < 1.0e-4f) continue;
        AudioClipModel a;
        a.sample = sample;
        a.sampleRef = paths::toRef (f);
        a.name = "Deck " + juce::String::charToString ((juce::juce_wchar) ('A' + d));
        a.lane = lane;
        a.start = 0.0;
        a.length = sample->getLengthSeconds() * ctx->project.bpm / 60.0;
        ctx->project.getAudioClips().push_back (a);
        if (lane < (int) ctx->project.getLanes().size())
            ctx->project.getLanes()[(size_t) lane].name = a.name;
        ++lane;
    }
    ctx->syncTransportFromProject();
    ctx->projectEdited();
    setMode (true);
    makeView->showTab (2);
    ctx->setStatus ("Your set is in the Song view: one lane per deck at " + juce::String (ctx->project.bpm, 1) + " BPM.");
}

bool MainComponent::quitRequested()
{
    return true;
}

} // namespace merge
