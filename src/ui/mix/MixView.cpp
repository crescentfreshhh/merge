#include "MixView.h"
#include "AutomixPanel.h"

namespace merge
{

MixView::MixView (AppContext& c) : ctx (c), mixer (c), library (c), remixPads (c, true)
{
    for (int d = 0; d < DjCore::kMaxDecks; ++d)
    {
        strips.push_back (std::make_unique<WaveStrip> (ctx, d));
        decks.push_back (std::make_unique<DeckPanel> (ctx, d, d >= 2));
        decks.back()->onFlip = [this] (int deck) { if (ctx.flipRequest) ctx.flipRequest (deck); };
        addChildComponent (*strips.back());
        addChildComponent (*decks.back());
    }
    addAndMakeVisible (mixer);
    mixer.onRecord = [this] { if (onRecordSet) onRecordSet(); };
    addAndMakeVisible (library);
    library.onSplitStems = [this] (juce::int64 id) { if (ctx.stemsForTrack) ctx.stemsForTrack (id); };
    addChildComponent (remixPads);
    for (int i = 0; i < kNumPatterns; ++i)
    {
        launchButtons[(size_t) i] = std::make_unique<juce::TextButton> (Project::patternName (i));
        launchButtons[(size_t) i]->setTooltip ("Launch pattern " + Project::patternName (i) + " on the next bar");
        launchButtons[(size_t) i]->onClick = [this, i]
        {
            ctx.selectPattern (i);
            if (! ctx.isPlaying())
            {
                ctx.engine.beat().transport.currentPattern.store (i);
                ctx.engine.beat().transport.queuedPattern.store (-1);
                ctx.togglePlay();
            }
        };
        addChildComponent (*launchButtons[(size_t) i]);
    }
    automix = std::make_unique<AutomixPanel> (ctx);
    addChildComponent (*automix);

    twoDecks.onClick = [this] { setNumDecks (2); };
    fourDecks.onClick = [this] { setNumDecks (4); };
    libTab.onClick = [this] { showBottom (0); };
    remixTab.onClick = [this] { showBottom (1); };
    automixTab.onClick = [this] { showBottom (2); };
    for (auto* b : { &twoDecks, &fourDecks, &libTab, &remixTab, &automixTab })
        addAndMakeVisible (*b);

    bpmRange.addItem ("BPM range 70-180", 1);
    bpmRange.addItem ("BPM range 88-175 (D&B)", 2);
    bpmRange.addItem ("BPM range 60-120 (hip hop)", 3);
    bpmRange.addItem ("BPM range 90-160", 4);
    bpmRange.setSelectedId (ctx.library->getMinBpm() >= 88 ? 2 : (ctx.library->getMaxBpm() <= 120 ? 3 : (ctx.library->getMinBpm() >= 90 ? 4 : 1)), juce::dontSendNotification);
    bpmRange.setTooltip ("Range used when detecting tempo (picks between double/half tempo)");
    bpmRange.onChange = [this]
    {
        static const double ranges[][2] { { 70, 180 }, { 70, 180 }, { 88, 175 }, { 60, 120 }, { 90, 160 } };
        const int id = bpmRange.getSelectedId();
        ctx.library->setBpmRange (ranges[id][0], ranges[id][1]);
        ctx.setStatus ("New BPM range applies to tracks analysed from now on (right-click a track > Analyse again).");
    };
    addAndMakeVisible (bpmRange);
    autoGain.setToggleState (ctx.settings().getBoolValue ("autoGain", true), juce::dontSendNotification);
    autoGain.setTooltip ("Level-match tracks using their measured loudness (LUFS)");
    autoGain.onClick = [this]
    {
        ctx.settings().setValue ("autoGain", autoGain.getToggleState());
        for (int d = 0; d < DjCore::kMaxDecks; ++d)
            if (auto& t = ctx.decks->state (d).track; t != nullptr)
                ctx.dj.channel (d).autoGainDb.store (autoGain.getToggleState() ? t->autoGainDb : 0.0f);
    };
    addAndMakeVisible (autoGain);

    setNumDecks (ctx.settings().getIntValue ("numDecks", 2));
    showBottom (0);
    startTimerHz (4);
}

MixView::~MixView() = default;

void MixView::timerCallback()
{
    const int cur = ctx.engine.beat().transport.currentPattern.load();
    const int queued = ctx.engine.beat().transport.queuedPattern.load();
    for (int i = 0; i < kNumPatterns; ++i)
    {
        auto& b = *launchButtons[(size_t) i];
        const bool empty = ctx.project.getPatterns()[(size_t) i].isEmpty();
        b.setColour (juce::TextButton::buttonColourId, i == cur && ctx.isPlaying() ? theme::make.withAlpha (0.8f)
                                                       : (i == queued ? theme::make.withAlpha (0.35f) : theme::bg2));
        b.setColour (juce::TextButton::textColourOffId, empty ? theme::textFaint : theme::text);
    }
}

void MixView::setNumDecks (int n)
{
    numDecks = n == 4 ? 4 : 2;
    ctx.dj.numDecks.store (numDecks);
    ctx.settings().setValue ("numDecks", numDecks);
    twoDecks.setToggleState (numDecks == 2, juce::dontSendNotification);
    fourDecks.setToggleState (numDecks == 4, juce::dontSendNotification);
    for (int d = 0; d < DjCore::kMaxDecks; ++d)
    {
        strips[(size_t) d]->setVisible (d < numDecks);
        decks[(size_t) d]->setVisible (d < numDecks);
    }
    mixer.setNumDecks (numDecks);
    resized();
}

void MixView::showBottom (int which)
{
    bottom = which;
    libTab.setToggleState (which == 0, juce::dontSendNotification);
    remixTab.setToggleState (which == 1, juce::dontSendNotification);
    automixTab.setToggleState (which == 2, juce::dontSendNotification);
    library.setVisible (which == 0);
    remixPads.setVisible (which == 1);
    for (auto& b : launchButtons) b->setVisible (which == 1);
    automix->setVisible (which == 2);
    resized();
}

void MixView::paint (juce::Graphics& g)
{
    g.fillAll (theme::bg0);
    if (bottom == 1)
    {
        g.setColour (theme::textDim);
        g.setFont (theme::font (12.0f));
        auto area = remixPads.getBounds();
        g.drawFittedText ("Remix deck: your beatmaker pads and patterns, live. Turn on SYNC in the mixer's BEAT channel to lock "
                          "it to the master deck. Patterns launch on the next bar.",
                          area.withX (area.getRight() + 12).withWidth (getWidth() - area.getRight() - 24).withHeight (40),
                          juce::Justification::topLeft, 2);
    }
}

void MixView::resized()
{
    auto r = getLocalBounds().reduced (8);
    auto toolbar = r.removeFromTop (28);
    twoDecks.setBounds (toolbar.removeFromLeft (80));
    toolbar.removeFromLeft (4);
    fourDecks.setBounds (toolbar.removeFromLeft (80));
    toolbar.removeFromLeft (16);
    libTab.setBounds (toolbar.removeFromLeft (90));
    toolbar.removeFromLeft (4);
    remixTab.setBounds (toolbar.removeFromLeft (110));
    toolbar.removeFromLeft (4);
    automixTab.setBounds (toolbar.removeFromLeft (90));
    autoGain.setBounds (toolbar.removeFromRight (100));
    toolbar.removeFromRight (8);
    bpmRange.setBounds (toolbar.removeFromRight (220));
    r.removeFromTop (6);

    const int stripH = numDecks == 2 ? 70 : 46;
    for (int d = 0; d < numDecks; ++d)
    {
        strips[(size_t) d]->setBounds (r.removeFromTop (stripH));
        r.removeFromTop (2);
    }
    r.removeFromTop (6);

    const int deckAreaH = juce::jlimit (300, numDecks == 2 ? 480 : 600, r.getHeight() - 210);
    auto deckArea = r.removeFromTop (deckAreaH);
    r.removeFromTop (6);
    const int mixerW = numDecks == 2 ? 300 : 380;
    auto mixerArea = deckArea.withSizeKeepingCentre (mixerW, deckArea.getHeight());
    mixer.setBounds (mixerArea);
    auto leftCol = deckArea.withRight (mixerArea.getX() - 6);
    auto rightCol = deckArea.withLeft (mixerArea.getRight() + 6);
    if (numDecks == 2)
    {
        decks[0]->setBounds (leftCol);
        decks[1]->setBounds (rightCol);
    }
    else
    {
        decks[0]->setBounds (leftCol.removeFromTop (leftCol.getHeight() / 2 - 3));
        leftCol.removeFromTop (6);
        decks[2]->setBounds (leftCol);
        decks[1]->setBounds (rightCol.removeFromTop (rightCol.getHeight() / 2 - 3));
        rightCol.removeFromTop (6);
        decks[3]->setBounds (rightCol);
    }

    library.setBounds (r);
    automix->setBounds (r);
    auto pads = r.withWidth (r.getHeight());
    remixPads.setBounds (pads);
    auto launch = r.withLeft (pads.getRight() + 12).withTrimmedTop (48);
    const int bw = juce::jmin (80, launch.getWidth() / 8);
    for (int i = 0; i < kNumPatterns; ++i)
        launchButtons[(size_t) i]->setBounds (launch.getX() + (i % 8) * (bw + 4), launch.getY() + (i / 8) * 40, bw, 34);
}

} // namespace merge
