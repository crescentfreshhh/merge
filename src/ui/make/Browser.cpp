#include "Browser.h"
#include "core/Paths.h"
#include "engine/PluginHost.h"

namespace merge
{

struct Browser::KitModel : public juce::ListBoxModel
{
    explicit KitModel (Browser& b) : owner (b) {}
    int getNumRows() override { return (int) owner.ctx.kits.getKits().size(); }

    void paintListBoxItem (int row, juce::Graphics& g, int w, int h, bool selected) override
    {
        const auto& kits = owner.ctx.kits.getKits();
        if (row >= (int) kits.size()) return;
        const auto& k = kits[(size_t) row];
        const bool current = k.name == owner.ctx.project.kitName;
        g.fillAll (selected ? theme::bg3 : theme::bg1);
        g.setColour (current ? theme::make : theme::text);
        g.setFont (theme::font (14.0f, true));
        g.drawText (k.name, 12, 4, w - 20, 18, juce::Justification::left);
        g.setColour (theme::textDim);
        g.setFont (theme::font (11.0f));
        juce::String sub = k.genre;
        if (k.bpm > 0) sub << "  |  " << juce::String (k.bpm, 0) << " BPM";
        if (k.keyName.isNotEmpty()) sub << "  |  " << k.keyName;
        g.drawText (sub, 12, 22, w - 20, 14, juce::Justification::left);
        g.setColour (k.source == "Factory" ? theme::textFaint : theme::mix);
        g.setFont (theme::font (9.5f, true));
        g.drawText (k.source.toUpperCase(), 0, 4, w - 8, 14, juce::Justification::right);
        g.setColour (theme::line);
        g.drawHorizontalLine (h - 1, 8.0f, (float) w - 8.0f);
    }

    void listBoxItemClicked (int row, const juce::MouseEvent&) override
    {
        const auto& kits = owner.ctx.kits.getKits();
        if (row < (int) kits.size())
            owner.ctx.loadKit (kits[(size_t) row], owner.loadGroove.getToggleState());
    }

    Browser& owner;
};

struct Browser::InstrumentModel : public juce::ListBoxModel
{
    explicit InstrumentModel (Browser& b) : owner (b) {}

    struct Item { juce::String name, detail; int type; int index; };
    std::vector<Item> items;

    void rebuild()
    {
        items.clear();
        items.push_back ({ "808 Bass", "Sub bass synth with glide", 0, 0 });
        items.push_back ({ "Poly Synth", "Two-oscillator synth", 1, 0 });
        const auto& samples = owner.ctx.kits.getInstrumentSamples();
        for (size_t i = 0; i < samples.size(); ++i)
            items.push_back ({ samples[i].name, "Keys sampler", 2, (int) i });
        const auto plugs = owner.ctx.plugins.getInstruments();
        for (size_t i = 0; i < plugs.size(); ++i)
            items.push_back ({ plugs[i].name, plugs[i].manufacturerName + " (VST3)", 3, (int) i });
    }

    int getNumRows() override { return (int) items.size(); }

    void paintListBoxItem (int row, juce::Graphics& g, int w, int h, bool selected) override
    {
        if (row >= (int) items.size()) return;
        g.fillAll (selected ? theme::bg3 : theme::bg1);
        g.setColour (theme::text);
        g.setFont (theme::font (13.5f, true));
        g.drawText (items[(size_t) row].name, 12, 3, w - 20, 17, juce::Justification::left);
        g.setColour (theme::textDim);
        g.setFont (theme::font (11.0f));
        g.drawText (items[(size_t) row].detail, 12, 19, w - 20, 14, juce::Justification::left);
        g.setColour (theme::line);
        g.drawHorizontalLine (h - 1, 8.0f, (float) w - 8.0f);
    }

    void listBoxItemDoubleClicked (int row, const juce::MouseEvent&) override { owner.addInstrument (row); }

    Browser& owner;
};

struct Browser::TreeListener : public juce::FileBrowserListener
{
    explicit TreeListener (Browser& b) : owner (b) {}
    void selectionChanged() override {}
    void fileClicked (const juce::File& f, const juce::MouseEvent&) override
    {
        if (owner.autoPreview.getToggleState() && f.existsAsFile())
            if (auto s = SampleCache::get().load (f))
                owner.ctx.engine.preview (s);
    }
    void fileDoubleClicked (const juce::File& f) override
    {
        // load onto the selected pad / sampler channel
        if (! f.existsAsFile()) return;
        auto* ch = owner.ctx.project.findChannel (owner.ctx.selectedChannelId);
        if (ch == nullptr || (ch->kind != ChannelKind::Sampler && ch->kind != ChannelKind::Keys)) return;
        if (auto s = SampleCache::get().load (f))
        {
            owner.ctx.project.setChannelSample (ch->id, s, paths::toRef (f), ch->kind == ChannelKind::Keys ? 60 : ch->stepNote);
            ch->name = f.getFileNameWithoutExtension();
            owner.ctx.projectEdited();
        }
    }
    void browserRootChanged (const juce::File&) override {}
    Browser& owner;
};

Browser::Browser (AppContext& c) : ctx (c)
{
    for (auto* b : { &tabKits, &tabSounds, &tabInst })
    {
        b->setClickingTogglesState (false);
        addAndMakeVisible (*b);
    }
    tabKits.onClick = [this] { showTab (0); };
    tabSounds.onClick = [this] { showTab (1); };
    tabInst.onClick = [this] { showTab (2); };

    kitModel = std::make_unique<KitModel> (*this);
    kitList.setModel (kitModel.get());
    kitList.setRowHeight (42);
    addAndMakeVisible (kitList);
    loadGroove.setToggleState (true, juce::dontSendNotification);
    addAndMakeVisible (loadGroove);

    scanThread.startThread (juce::Thread::Priority::low);
    treeListener = std::make_unique<TreeListener> (*this);
    tree.addListener (treeListener.get());
    tree.setDragAndDropDescription ("merge-filetree");
    tree.setColour (juce::FileTreeComponent::backgroundColourId, theme::bg1);
    tree.setColour (juce::DirectoryContentsDisplayComponent::textColourId, theme::text);
    tree.setItemHeight (22);
    addChildComponent (tree);
    autoPreview.setToggleState (true, juce::dontSendNotification);
    addChildComponent (autoPreview);
    addChildComponent (rootBox);
    addChildComponent (openFolder);
    rootBox.onChange = [this] { setSoundsRoot (rootBox.getSelectedItemIndex()); };
    openFolder.onClick = [this]
    {
        auto chooser = std::make_shared<juce::FileChooser> ("Browse a folder of samples", paths::userSamplesDir());
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
                              [this, chooser] (const juce::FileChooser& fc)
                              {
                                  if (fc.getResult().isDirectory())
                                  {
                                      roots.add (fc.getResult());
                                      rootBox.addItem (fc.getResult().getFileName(), roots.size());
                                      rootBox.setSelectedItemIndex (roots.size() - 1);
                                  }
                              });
    };

    if (auto f = paths::factorySamplesDir(); f.isDirectory()) { roots.add (f); rootBox.addItem ("Factory Library", roots.size()); }
    if (auto p = paths::bundledPacksDir(); p.isDirectory()) { roots.add (p); rootBox.addItem ("Bundled Packs", roots.size()); }
    roots.add (paths::userSamplesDir()); rootBox.addItem ("My Samples", roots.size());
    rootBox.setSelectedItemIndex (0, juce::dontSendNotification);
    setSoundsRoot (0);

    instModel = std::make_unique<InstrumentModel> (*this);
    instModel->rebuild();
    instList.setModel (instModel.get());
    instList.setRowHeight (38);
    addChildComponent (instList);
    addChildComponent (addInst);
    addChildComponent (scanPlugins);
    addInst.onClick = [this] { addInstrument (instList.getSelectedRow()); };
    scanPlugins.onClick = [this] { ctx.plugins.startScan(); ctx.setStatus ("Scanning VST3 plugins..."); };
    ctx.plugins.addChangeListener (this);
    ctx.addChangeListener (this);

    showTab (0);
}

Browser::~Browser()
{
    ctx.plugins.removeChangeListener (this);
    ctx.removeChangeListener (this);
    tree.removeListener (treeListener.get());
    scanThread.stopThread (1000);
}

void Browser::changeListenerCallback (juce::ChangeBroadcaster* source)
{
    if (source == &ctx.plugins)
    {
        if (ctx.plugins.isScanning())
            scanPlugins.setButtonText ("Scanning " + juce::String ((int) (ctx.plugins.getScanProgress() * 100)) + "%");
        else
            scanPlugins.setButtonText ("Scan VST3");
        instModel->rebuild();
        instList.updateContent();
    }
    kitList.repaint();
}

void Browser::setSoundsRoot (int index)
{
    if (index >= 0 && index < roots.size())
        contents.setDirectory (roots[index], true, true);
}

void Browser::addInstrument (int row)
{
    if (row < 0 || row >= (int) instModel->items.size()) return;
    const auto item = instModel->items[(size_t) row];
    ChannelModel* added = nullptr;
    if (item.type == 0)
        added = &ctx.project.addChannel (ChannelKind::Bass808, "808 Bass");
    else if (item.type == 1)
        added = &ctx.project.addChannel (ChannelKind::Synth, "Synth");
    else if (item.type == 2)
    {
        const auto& s = ctx.kits.getInstrumentSamples()[(size_t) item.index];
        added = &ctx.project.addChannel (ChannelKind::Keys, s.name);
        ctx.project.setChannelSample (added->id, SampleCache::get().load (s.file), paths::toRef (s.file), s.root);
        added->name = s.name;
    }
    else if (item.type == 3)
    {
        const auto plugs = ctx.plugins.getInstruments();
        juce::String err;
        auto inst = ctx.plugins.wrapInstrument (ctx.plugins.createInstance (plugs[(size_t) item.index], ctx.engine.getSampleRate(),
                                                                             ctx.engine.getBlockSize(), err));
        if (inst == nullptr)
        {
            ctx.setStatus ("Couldn't load " + item.name + ": " + err);
            return;
        }
        added = &ctx.project.addChannel (ChannelKind::Plugin, item.name, inst);
    }
    if (added != nullptr)
    {
        const int id = added->id;
        ctx.projectEdited();
        ctx.selectChannel (id);
        ctx.setStatus ("Added " + item.name + " - draw notes in the Piano Roll or play your MIDI keyboard");
    }
}

void Browser::showTab (int index)
{
    currentTab = index;
    tabKits.setToggleState (index == 0, juce::dontSendNotification);
    tabSounds.setToggleState (index == 1, juce::dontSendNotification);
    tabInst.setToggleState (index == 2, juce::dontSendNotification);
    kitList.setVisible (index == 0);
    loadGroove.setVisible (index == 0);
    tree.setVisible (index == 1);
    rootBox.setVisible (index == 1);
    autoPreview.setVisible (index == 1);
    openFolder.setVisible (index == 1);
    instList.setVisible (index == 2);
    addInst.setVisible (index == 2);
    scanPlugins.setVisible (index == 2);
    resized();
}

void Browser::paint (juce::Graphics& g)
{
    g.setColour (theme::bg1);
    g.fillRoundedRectangle (getLocalBounds().toFloat(), 6.0f);
}

void Browser::resized()
{
    auto r = getLocalBounds().reduced (8);
    auto tabs = r.removeFromTop (28);
    const int w = tabs.getWidth() / 3;
    tabKits.setBounds (tabs.removeFromLeft (w).reduced (2, 0));
    tabSounds.setBounds (tabs.removeFromLeft (w).reduced (2, 0));
    tabInst.setBounds (tabs.reduced (2, 0));
    r.removeFromTop (8);
    if (currentTab == 0)
    {
        loadGroove.setBounds (r.removeFromBottom (26));
        kitList.setBounds (r);
    }
    else if (currentTab == 1)
    {
        auto top = r.removeFromTop (26);
        openFolder.setBounds (top.removeFromRight (90));
        rootBox.setBounds (top.withTrimmedRight (6));
        r.removeFromTop (6);
        autoPreview.setBounds (r.removeFromBottom (26));
        tree.setBounds (r);
    }
    else
    {
        auto bottom = r.removeFromBottom (28);
        addInst.setBounds (bottom.removeFromLeft (bottom.getWidth() / 2).reduced (2, 0));
        scanPlugins.setBounds (bottom.reduced (2, 0));
        r.removeFromBottom (6);
        instList.setBounds (r);
    }
}

} // namespace merge
