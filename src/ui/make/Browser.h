#pragma once

#include "ui/common/Widgets.h"
#include "app/AppContext.h"

namespace merge
{

/** Left-hand browser: kits, sounds (file tree with preview) and instruments. */
class Browser : public juce::Component, private juce::ChangeListener
{
public:
    explicit Browser (AppContext& ctx);
    ~Browser() override;
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void showTab (int index);
    void setSoundsRoot (int index);
    void addInstrument (int row);

    struct KitModel;
    struct InstrumentModel;
    struct TreeListener;

    AppContext& ctx;
    PillButton tabKits { "Kits", theme::make, false }, tabSounds { "Sounds", theme::make, false },
               tabInst { "Instruments", theme::make, false };
    int currentTab = 0;

    juce::ListBox kitList;
    std::unique_ptr<KitModel> kitModel;
    juce::ToggleButton loadGroove { "Load the kit's groove" };

    juce::ComboBox rootBox;
    juce::TimeSliceThread scanThread { "Merge browser" };
    juce::WildcardFileFilter audioFilter { "*.wav;*.aif;*.aiff;*.flac;*.ogg;*.mp3", "*", "Audio" };
    juce::DirectoryContentsList contents { &audioFilter, scanThread };
    juce::FileTreeComponent tree { contents };
    std::unique_ptr<TreeListener> treeListener;
    juce::ToggleButton autoPreview { "Preview on click" };
    juce::TextButton openFolder { "Open folder" };
    juce::Array<juce::File> roots;

    juce::ListBox instList;
    std::unique_ptr<InstrumentModel> instModel;
    juce::TextButton addInst { "Add to rack" }, scanPlugins { "Scan VST3" };
};

} // namespace merge
