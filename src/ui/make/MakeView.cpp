#include "MakeView.h"
#include "PianoRoll.h"
#include "Playlist.h"
#include "MixerView.h"
#include "engine/KitLibrary.h"

namespace merge
{

MakeView::MakeView (AppContext& c)
    : ctx (c), browser (c), pads (c), inspector (c), steps (c)
{
    addAndMakeVisible (browser);
    addAndMakeVisible (pads);
    addAndMakeVisible (inspector);
    stepViewport.setViewedComponent (&steps, false);
    stepViewport.setScrollBarsShown (true, false);
    addAndMakeVisible (stepViewport);
    steps.onOpenPianoRoll = [this] (int id) { openPianoRoll (id); };

    const char* names[] { "Beat", "Piano Roll", "Song", "Mixer" };
    for (int i = 0; i < 4; ++i)
    {
        tabButtons[(size_t) i] = std::make_unique<PillButton> (names[i], theme::make, false);
        tabButtons[(size_t) i]->onClick = [this, i] { showTab (i); };
        addAndMakeVisible (*tabButtons[(size_t) i]);
    }

    for (int len : { 16, 32, 48, 64 })
        lengthBox.addItem (juce::String (len) + " steps", len);
    lengthBox.onChange = [this]
    {
        ctx.project.checkpoint();
        ctx.project.setPatternLength (ctx.selectedPattern(), lengthBox.getSelectedId());
        ctx.projectEdited();
    };
    lengthBox.setTooltip ("Pattern length");
    addAndMakeVisible (lengthBox);

    generate.setTooltip ("Generate a fresh drum pattern in the style of the loaded kit");
    generate.onClick = [this]
    {
        juce::Random rng;
        auto pat = generatePattern (ctx.project.genre.isNotEmpty() ? ctx.project.genre : ctx.project.kitName, rng);
        ctx.project.checkpoint();
        const int pi = ctx.selectedPattern();
        auto& chans = ctx.project.getChannels();
        for (int i = 0; i < kNumPads && i < (int) chans.size(); ++i)
            ctx.project.pattern (pi).steps.erase (chans[(size_t) i].id);
        ctx.project.setPatternLength (pi, 16);
        for (auto& [pad, s] : pat)
            if (pad >= 1 && pad <= (int) chans.size())
                ctx.project.setStepsFromString (pi, chans[(size_t) (pad - 1)].id, s);
        ctx.projectEdited();
        ctx.setStatus ("Generated a new " + (ctx.project.genre.isNotEmpty() ? ctx.project.genre : juce::String ("drum")) + " pattern - press Ctrl+Z to undo");
    };
    addAndMakeVisible (generate);

    clear.onClick = [this]
    {
        ctx.project.checkpoint();
        ctx.project.clearPattern (ctx.selectedPattern());
        ctx.projectEdited();
    };
    addAndMakeVisible (clear);

    addChannel.onClick = [this]
    {
        juce::PopupMenu m;
        m.addItem (1, "808 Bass");
        m.addItem (2, "Poly Synth");
        juce::PopupMenu keys;
        const auto& inst = ctx.kits.getInstrumentSamples();
        for (size_t i = 0; i < inst.size(); ++i)
            keys.addItem (100 + (int) i, inst[i].name);
        m.addSubMenu ("Keys sampler", keys);
        m.addItem (3, "Empty sampler");
        showMenu (m, &addChannel, [this] (int r)
        {
            ChannelModel* ch = nullptr;
            if (r == 1) ch = &ctx.project.addChannel (ChannelKind::Bass808, "808 Bass");
            else if (r == 2) ch = &ctx.project.addChannel (ChannelKind::Synth, "Synth");
            else if (r == 3) ch = &ctx.project.addChannel (ChannelKind::Sampler, "Sampler");
            else if (r >= 100)
            {
                const auto& s = ctx.kits.getInstrumentSamples()[(size_t) (r - 100)];
                ch = &ctx.project.addChannel (ChannelKind::Keys, s.name);
                ctx.project.setChannelSample (ch->id, SampleCache::get().load (s.file), {}, s.root);
                ch->name = s.name;
            }
            if (ch != nullptr)
            {
                const int id = ch->id;
                ctx.projectEdited();
                ctx.selectChannel (id);
            }
        });
    };
    addAndMakeVisible (addChannel);

    repeatBox.addItem ("Note repeat off", 1);
    repeatBox.addItem ("Repeat 1/4", 2);
    repeatBox.addItem ("Repeat 1/8", 3);
    repeatBox.addItem ("Repeat 1/16", 4);
    repeatBox.addItem ("Repeat 1/32", 5);
    repeatBox.addItem ("Repeat 1/8 T", 6);
    repeatBox.addItem ("Repeat 1/16 T", 7);
    repeatBox.setSelectedId (1, juce::dontSendNotification);
    repeatBox.setTooltip ("MPC-style note repeat: hold a pad to retrigger it in time");
    repeatBox.onChange = [this]
    {
        static const double rates[] { 0.0, 0.0, 1.0, 0.5, 0.25, 0.125, 1.0 / 3.0, 1.0 / 6.0 };
        ctx.engine.beat().transport.noteRepeat.store (rates[juce::jlimit (0, 7, repeatBox.getSelectedId())]);
    };
    addAndMakeVisible (repeatBox);

    pianoRoll = std::make_unique<PianoRoll> (ctx);
    playlist = std::make_unique<Playlist> (ctx);
    mixer = std::make_unique<MixerView> (ctx);
    addChildComponent (*pianoRoll);
    addChildComponent (*playlist);
    addChildComponent (*mixer);

    ctx.selectionChanged.addChangeListener (this);
    ctx.addChangeListener (this);
    showTab (0);
}

MakeView::~MakeView()
{
    ctx.selectionChanged.removeChangeListener (this);
    ctx.removeChangeListener (this);
}

void MakeView::changeListenerCallback (juce::ChangeBroadcaster*)
{
    lengthBox.setSelectedId (ctx.project.getPatterns()[(size_t) ctx.selectedPattern()].lengthSteps, juce::dontSendNotification);
    steps.setSize (stepViewport.getMaximumVisibleWidth(), steps.getIdealHeight());
}

void MakeView::showTab (int index)
{
    currentTab = index;
    for (int i = 0; i < 4; ++i)
        tabButtons[(size_t) i]->setToggleState (i == index, juce::dontSendNotification);
    const bool beat = index == 0;
    for (juce::Component* c : { (juce::Component*) &pads, (juce::Component*) &inspector, (juce::Component*) &stepViewport,
                                (juce::Component*) &lengthBox, (juce::Component*) &generate, (juce::Component*) &clear,
                                (juce::Component*) &addChannel, (juce::Component*) &repeatBox })
        c->setVisible (beat);
    pianoRoll->setVisible (index == 1);
    playlist->setVisible (index == 2);
    mixer->setVisible (index == 3);
    if (index == 2) ctx.setSongMode (true);
    else if (index == 0 || index == 1) ctx.setSongMode (false);
    resized();
}

void MakeView::openPianoRoll (int channelId)
{
    ctx.selectChannel (channelId);
    showTab (1);
}

void MakeView::paint (juce::Graphics& g)
{
    g.fillAll (theme::bg0);
}

void MakeView::layoutBeat (juce::Rectangle<int> area)
{
    auto top = area.removeFromTop (juce::jlimit (260, 380, area.getHeight() / 2));
    pads.setBounds (top.removeFromLeft (top.getHeight()));
    top.removeFromLeft (8);
    inspector.setBounds (top);
    area.removeFromTop (8);
    auto bar = area.removeFromTop (30);
    lengthBox.setBounds (bar.removeFromLeft (110));
    bar.removeFromLeft (6);
    generate.setBounds (bar.removeFromLeft (96));
    bar.removeFromLeft (6);
    clear.setBounds (bar.removeFromLeft (70));
    bar.removeFromLeft (6);
    addChannel.setBounds (bar.removeFromLeft (96));
    repeatBox.setBounds (bar.removeFromRight (150));
    area.removeFromTop (6);
    stepViewport.setBounds (area);
    steps.setSize (stepViewport.getMaximumVisibleWidth(), steps.getIdealHeight());
}

void MakeView::resized()
{
    auto r = getLocalBounds().reduced (8);
    browser.setBounds (r.removeFromLeft (juce::jlimit (220, 300, getWidth() / 5)));
    r.removeFromLeft (8);
    auto tabs = r.removeFromTop (30);
    for (auto& b : tabButtons)
        b->setBounds (tabs.removeFromLeft (110).reduced (2, 0));
    r.removeFromTop (8);
    if (currentTab == 0) layoutBeat (r);
    pianoRoll->setBounds (r);
    playlist->setBounds (r);
    mixer->setBounds (r);
}

} // namespace merge
