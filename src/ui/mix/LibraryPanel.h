#pragma once

#include "DeckWidgets.h"
#include "app/LibraryService.h"

namespace merge
{

/** Music library: sources (all tracks, My Beats, Automix queue, crates), search, compatible-key filter, track table. */
class LibraryPanel : public juce::Component,
                     public juce::FileDragAndDropTarget,
                     private juce::ChangeListener,
                     private juce::TableListBoxModel,
                     private juce::ListBoxModel
{
public:
    explicit LibraryPanel (AppContext& ctx);
    ~LibraryPanel() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    bool isInterestedInFileDrag (const juce::StringArray&) override { return true; }
    void filesDropped (const juce::StringArray& files, int, int) override;

    std::function<void (juce::int64 trackId)> onSplitStems;

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override { rebuild(); }
    void rebuild();
    juce::int64 currentCrate() const;

    // table
    int getNumRows() override;
    void paintRowBackground (juce::Graphics&, int row, int w, int h, bool selected) override;
    void paintCell (juce::Graphics&, int row, int col, int w, int h, bool selected) override;
    void cellDoubleClicked (int row, int col, const juce::MouseEvent&) override;
    void cellClicked (int row, int col, const juce::MouseEvent&) override;
    void sortOrderChanged (int col, bool forwards) override;
    juce::var getDragSourceDescription (const juce::SparseSet<int>& rows) override;
    // sources list
    void paintListBoxItem (int row, juce::Graphics&, int w, int h, bool selected) override;
    void listBoxItemClicked (int row, const juce::MouseEvent&) override;

    void showTrackMenu (int row);

    AppContext& ctx;
    juce::ListBox sources;
    juce::TableListBox table;
    juce::TextEditor search;
    PillButton compatible { "Compatible keys", theme::good }, bpmMatch { "Near BPM", theme::good };
    juce::TextButton addFolder { "+ Folder" }, addCrate { "+ Crate" }, importXml { "Import Rekordbox" };
    juce::Label info;

    struct Source { juce::String name; juce::int64 crate; };
    std::vector<Source> sourceList;
    int currentSource = 0;
    std::vector<TrackInfo> rows;
    int sortColumn = 0;
    bool sortForwards = true;
    std::shared_ptr<juce::FileChooser> chooser;
};

} // namespace merge
