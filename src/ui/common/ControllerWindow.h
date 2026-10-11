#pragma once

#include "Widgets.h"
#include "app/AppContext.h"
#include "app/ControllerManager.h"

namespace merge
{

/** Lists every controllable action with its MIDI binding; Learn assigns the next moved control. */
class ControllerPanel : public juce::Component, private juce::TableListBoxModel, private juce::ChangeListener
{
public:
    explicit ControllerPanel (AppContext& ctx);
    ~ControllerPanel() override;
    void resized() override;
    void paint (juce::Graphics&) override;

    static void show (AppContext& ctx);

private:
    int getNumRows() override;
    void paintRowBackground (juce::Graphics&, int row, int w, int h, bool selected) override;
    void paintCell (juce::Graphics&, int row, int col, int w, int h, bool selected) override;
    void cellClicked (int row, int col, const juce::MouseEvent&) override;
    void changeListenerCallback (juce::ChangeBroadcaster*) override;

    AppContext& ctx;
    juce::TableListBox table;
    juce::Label help, last;
    juce::TextEditor name;
    juce::TextButton saveBtn { "Save mapping" }, loadBtn { "Load mapping..." }, openFolder { "Mappings folder" };
    std::shared_ptr<juce::FileChooser> chooser;
};

} // namespace merge
