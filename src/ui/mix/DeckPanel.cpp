#include "DeckPanel.h"
#include "app/LibraryService.h"
#include "analysis/Analysis.h"

namespace merge
{

static const double loopSizes[8] { 0.25, 0.5, 1.0, 2.0, 4.0, 8.0, 16.0, 32.0 };
static const double rollSizes[8] { 1.0 / 32.0, 1.0 / 16.0, 1.0 / 8.0, 0.25, 0.5, 1.0, 2.0, 4.0 };

static juce::String sizeLabel (double beats)
{
    if (beats >= 1.0) return juce::String ((int) beats);
    return "1/" + juce::String ((int) std::round (1.0 / beats));
}

void DeckPanel::Pad::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced (2.0f);
    g.setColour (lit ? colour : theme::bg2.interpolatedWith (colour, 0.18f));
    g.fillRoundedRectangle (r, 4.0f);
    g.setColour (lit ? juce::Colours::black : theme::text);
    g.setFont (theme::font (11.5f, true));
    g.drawFittedText (label, getLocalBounds().reduced (3), juce::Justification::centred, 2, 0.8f);
}

DeckPanel::DeckPanel (AppContext& c, int d, bool comp) : ctx (c), deckIndex (d), compact (comp), overview (c, d), jog (c, d)
{
    const auto col = theme::deck (d);
    addAndMakeVisible (overview);
    addAndMakeVisible (jog);
    jog.setVisible (! compact);

    play.onClick = [this] { ctx.decks->togglePlay (deckIndex); };
    play.setTooltip ("Play / pause");
    cue.setTriggeredOnMouseDown (true);
    cue.onStateChange = [this]
    {
        const bool down = cue.isDown();
        static std::array<bool, 4> last {};
        if (down != last[(size_t) deckIndex])
        {
            last[(size_t) deckIndex] = down;
            ctx.decks->cue (deckIndex, down);
        }
    };
    cue.setTooltip ("CDJ cue: set / preview while held, jump back when playing");
    sync.onClick = [this] { ctx.decks->setSync (deckIndex, sync.getToggleState()); };
    sync.setTooltip ("Match tempo and beats to the master deck");
    master.setClickingTogglesState (false);
    master.onClick = [this] { ctx.decks->setMaster (deckIndex); };
    master.setTooltip ("Make this deck the tempo master (otherwise automatic)");
    keylock.onClick = [this] { ctx.decks->deck (deckIndex).keylock.store (keylock.getToggleState()); };
    keylock.setTooltip ("Keylock: change tempo without changing pitch");
    quant.onClick = [this] { ctx.decks->deck (deckIndex).quantize.store (quant.getToggleState()); };
    quant.setTooltip ("Quantize cues and loops to the beat grid");
    dbl.onClick = [this] { ctx.decks->doubleBpm (deckIndex); };
    half.onClick = [this] { ctx.decks->halveBpm (deckIndex); };
    gridHere.onClick = [this] { ctx.decks->setGridHere (deckIndex); };
    gridHere.setTooltip ("Set the downbeat of the beat grid at the playhead");
    dbl.setTooltip ("Double the detected BPM");
    half.setTooltip ("Halve the detected BPM");
    flip.setColour (juce::TextButton::buttonColourId, theme::make.withAlpha (0.25f));
    flip.setTooltip ("Flip: chop this deck's loop (or the next 4 bars) onto the beatmaker pads, stem by stem");
    flip.onClick = [this] { if (onFlip) onFlip (deckIndex); };
    for (juce::Component* b : { (juce::Component*) &play, (juce::Component*) &cue, (juce::Component*) &sync, (juce::Component*) &master,
                                (juce::Component*) &keylock, (juce::Component*) &quant, (juce::Component*) &dbl, (juce::Component*) &half,
                                (juce::Component*) &gridHere, (juce::Component*) &flip })
        addAndMakeVisible (b);

    pitch.setSliderStyle (juce::Slider::LinearVertical);
    pitch.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    pitch.setRange (-1.0, 1.0);
    pitch.setValue (0.0, juce::dontSendNotification);
    pitch.setDoubleClickReturnValue (true, 0.0);
    pitch.setColour (juce::Slider::trackColourId, col);
    pitch.onValueChange = [this] { ctx.decks->deck (deckIndex).pitch.store ((float) -pitch.getValue()); };
    pitch.setTooltip ("Tempo (double-click to reset)");
    addAndMakeVisible (pitch);
    range.onClick = [this]
    {
        auto& dk = ctx.decks->deck (deckIndex);
        const float r = dk.pitchRange.load();
        dk.pitchRange.store (r < 0.1f ? 0.16f : (r < 0.2f ? 0.5f : 0.08f));
        refresh();
    };
    addAndMakeVisible (range);

    for (int i = 0; i < 4; ++i)
    {
        modeButtons[(size_t) i].onClick = [this, i] { setPadMode (i); };
        addAndMakeVisible (modeButtons[(size_t) i]);
    }
    for (int i = 0; i < 8; ++i)
    {
        pads[(size_t) i].onPress = [this, i] (bool down, const juce::ModifierKeys& m) { padPressed (i, down, m); };
        addAndMakeVisible (pads[(size_t) i]);
    }
    jumpBack.onClick = [this] { ctx.decks->beatJump (deckIndex, -ctx.decks->deck (deckIndex).loopBeatsDefault.load()); };
    jumpFwd.onClick = [this] { ctx.decks->beatJump (deckIndex, ctx.decks->deck (deckIndex).loopBeatsDefault.load()); };
    loopHalf.onClick = [this] { ctx.decks->loopHalve (deckIndex); auto& dk = ctx.decks->deck (deckIndex); dk.loopBeatsDefault.store (juce::jmax (0.25f, dk.loopBeatsDefault.load() * 0.5f)); refresh(); };
    loopDbl.onClick = [this] { ctx.decks->loopDouble (deckIndex); auto& dk = ctx.decks->deck (deckIndex); dk.loopBeatsDefault.store (juce::jmin (32.0f, dk.loopBeatsDefault.load() * 2.0f)); refresh(); };
    loopIn.onClick = [this] { ctx.decks->loopIn (deckIndex); };
    loopOut.onClick = [this] { ctx.decks->loopOut (deckIndex); };
    jumpBack.setTooltip ("Beat jump back");
    jumpFwd.setTooltip ("Beat jump forward");
    for (juce::Component* b : { (juce::Component*) &jumpBack, (juce::Component*) &jumpFwd, (juce::Component*) &loopHalf,
                                (juce::Component*) &loopDbl, (juce::Component*) &loopIn, (juce::Component*) &loopOut })
        addAndMakeVisible (b);

    fxBox.addItem ("No FX", 1);
    const juce::StringArray fxTypes { "filter", "delay", "reverb", "flanger", "phaser", "bitcrush", "roll", "distortion" };
    for (int i = 0; i < fxTypes.size(); ++i)
        fxBox.addItem (builtInEffectName (fxTypes[i]), i + 2);
    fxBox.setSelectedId (1, juce::dontSendNotification);
    fxBox.onChange = [this, fxTypes]
    {
        auto& ch = ctx.decks->channel (deckIndex);
        const int id = fxBox.getSelectedId();
        ch.fx.store (nullptr);
        fx = id > 1 ? createBuiltInEffect (fxTypes[id - 2]) : nullptr;
        if (fx != nullptr)
        {
            fx->prepare (ctx.engine.getSampleRate(), ctx.engine.getBlockSize());
            if (auto* mix = fx->params.find ("mix")) mix->set (1.0f);
            if (fx->getTypeId() == "roll")
                if (auto* a = fx->params.find ("active")) a->set (1.0f);
        }
        ch.fx.store (fx.get());
    };
    addAndMakeVisible (fxBox);
    fxAmount.setRange (0.0, 1.0, 0.5);
    fxAmount.onChange = [this] (double v) { ctx.decks->channel (deckIndex).fxAmount.store ((float) v); };
    fxAmount.setTooltip ("FX amount");
    addAndMakeVisible (fxAmount);
    fxOn.onClick = [this] { ctx.decks->channel (deckIndex).fxOn.store (fxOn.getToggleState()); };
    addAndMakeVisible (fxOn);

    keylock.setToggleState (true, juce::dontSendNotification);
    quant.setToggleState (true, juce::dontSendNotification);
    setPadMode (0);
    ctx.decks->addChangeListener (this);
    ctx.library->addChangeListener (this);
    startTimerHz (15);
    refresh();
}

DeckPanel::~DeckPanel()
{
    ctx.decks->removeChangeListener (this);
    ctx.library->removeChangeListener (this);
    ctx.decks->channel (deckIndex).fx.store (nullptr);
}

void DeckPanel::setPadMode (int mode)
{
    padMode = mode;
    for (int i = 0; i < 4; ++i)
        modeButtons[(size_t) i].setToggleState (i == mode, juce::dontSendNotification);
    refresh();
}

void DeckPanel::padPressed (int pad, bool down, const juce::ModifierKeys& mods)
{
    auto& dc = *ctx.decks;
    switch (padMode)
    {
        case 0:
            if (down) dc.hotCue (deckIndex, pad, mods.isShiftDown() || mods.isPopupMenu());
            break;
        case 1:
            if (down)
            {
                dc.loopAuto (deckIndex, loopSizes[pad]);
                dc.deck (deckIndex).loopBeatsDefault.store ((float) loopSizes[pad]);
            }
            break;
        case 2:
            dc.loopRoll (deckIndex, rollSizes[pad], down);
            break;
        case 3:
        {
            if (! down) break;
            auto& s = dc.state (deckIndex);
            if (pad < 4)
            {
                if (! s.track || ! s.track->hasStems())
                {
                    if (ctx.stemsRequest) ctx.stemsRequest (deckIndex);
                    break;
                }
                auto& g = dc.deck (deckIndex).stemGain[(size_t) pad];
                g.store (g.load() > 0.5f ? 0.0f : 1.0f);
            }
            else if (s.track && s.track->hasStems())
            {
                // presets: acapella, instrumental, drums only, everything
                static const float presets[4][4] { { 0, 0, 1, 0 }, { 1, 1, 0, 1 }, { 1, 0, 0, 0 }, { 1, 1, 1, 1 } };
                for (int k = 0; k < 4; ++k) dc.deck (deckIndex).stemGain[(size_t) k].store (presets[pad - 4][k]);
            }
            else if (ctx.stemsRequest)
            {
                ctx.stemsRequest (deckIndex);
            }
            break;
        }
        default: break;
    }
    refresh();
}

void DeckPanel::timerCallback()
{
    auto& dk = ctx.decks->deck (deckIndex);
    play.setToggleState (dk.playing.load(), juce::dontSendNotification);
    sync.setToggleState (dk.sync.load(), juce::dontSendNotification);
    master.setToggleState (ctx.dj.currentMasterDeck.load() == deckIndex, juce::dontSendNotification);
    if (! pitch.isMouseButtonDown() && ! dk.sync.load())
        pitch.setValue (-dk.pitch.load(), juce::dontSendNotification);
    if (padMode == 1 || padMode == 2 || padMode == 3)
        refresh();
    repaint (getLocalBounds().removeFromTop (compact ? 40 : 48));
}

void DeckPanel::refresh()
{
    auto& dk = ctx.decks->deck (deckIndex);
    auto& s = ctx.decks->state (deckIndex);
    const auto col = theme::deck (deckIndex);
    range.setButtonText (juce::String ((int) std::round (dk.pitchRange.load() * 100)) + "%");
    for (int i = 0; i < 8; ++i)
    {
        auto& p = pads[(size_t) i];
        switch (padMode)
        {
            case 0:
                p.colour = Project::colourForIndex (i);
                p.lit = s.hotCues[(size_t) i] >= 0.0;
                p.label = p.lit ? juce::String (i + 1) : "+" + juce::String (i + 1);
                break;
            case 1:
            {
                p.colour = theme::good;
                const double len = dk.loopOutSec.load() - dk.loopInSec.load();
                const double spb = s.track != nullptr ? s.track->grid.secondsPerBeat() : 0.5;
                p.lit = dk.loopActive.load() && std::abs (len - loopSizes[i] * spb) < 0.01;
                p.label = sizeLabel (loopSizes[i]);
                break;
            }
            case 2:
                p.colour = theme::make;
                p.lit = false;
                p.label = sizeLabel (rollSizes[i]);
                break;
            case 3:
            {
                const bool ready = s.track != nullptr && s.track->hasStems();
                if (i < 4)
                {
                    static const juce::Colour stemCols[] { juce::Colour (0xffff5a5f), juce::Colour (0xff3fa9f5), juce::Colour (0xfff2d43d), juce::Colour (0xff3fd28a) };
                    p.colour = stemCols[i];
                    p.lit = ready && dk.stemGain[(size_t) i].load() > 0.5f;
                    p.label = stemName (i);
                    if (! ready && i == 0)
                        p.label = s.stemProgress >= 0.0f && s.stemProgress < 1.5f
                                    ? "Splitting " + juce::String ((int) (s.stemProgress * 100)) + "%" : juce::String ("Split stems");
                }
                else
                {
                    static const char* names[] { "Acapella", "Instrumental", "Drums only", "All" };
                    p.colour = theme::mix;
                    p.lit = false;
                    p.label = names[i - 4];
                }
                break;
            }
            default: break;
        }
        p.repaint();
    }
    loopHalf.setVisible (padMode == 1);
    loopDbl.setVisible (padMode == 1);
    loopIn.setVisible (padMode == 1);
    loopOut.setVisible (padMode == 1);
    juce::ignoreUnused (col);
    repaint();
}

void DeckPanel::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    const auto col = theme::deck (deckIndex);
    g.setColour (theme::bg1);
    g.fillRoundedRectangle (r, 6.0f);
    if (dragOver)
    {
        g.setColour (col);
        g.drawRoundedRectangle (r.reduced (1.0f), 6.0f, 2.0f);
    }
    auto& s = ctx.decks->state (deckIndex);
    auto& dk = ctx.decks->deck (deckIndex);

    auto header = getLocalBounds().reduced (10, 6).removeFromTop (compact ? 30 : 38);
    auto badge = header.removeFromLeft (26).withSizeKeepingCentre (26, 26).toFloat();
    g.setColour (col);
    g.fillRoundedRectangle (badge, 5.0f);
    g.setColour (juce::Colours::black);
    g.setFont (theme::font (15.0f, true));
    g.drawText (juce::String::charToString ((juce::juce_wchar) ('A' + deckIndex)), badge, juce::Justification::centred);
    header.removeFromLeft (8);

    // tempo & key on the right
    auto right = header.removeFromRight (170);
    const double eff = dk.effectiveBpm.load();
    g.setColour (theme::text);
    g.setFont (theme::font (compact ? 17.0f : 21.0f, true));
    g.drawText (eff > 0 ? juce::String (eff, 1) : juce::String ("--.-"), right.removeFromRight (78), juce::Justification::centredRight);
    const double pct = (dk.tempoRatio.load() - 1.0) * 100.0;
    if (s.track != nullptr)
    {
        auto info = right;
        g.setFont (theme::font (10.5f));
        g.setColour (theme::textDim);
        g.drawText ((pct >= 0 ? "+" : "") + juce::String (pct, 1) + "%", info.removeFromTop (info.getHeight() / 2), juce::Justification::centredRight);
        const auto key = s.track->key;
        if (key.isValid())
        {
            // green when it mixes harmonically with the master deck
            const int m = ctx.dj.currentMasterDeck.load();
            bool compatible = true;
            if (m >= 0 && m != deckIndex && ctx.decks->state (m).track != nullptr)
                compatible = analysis::keysCompatible (key, ctx.decks->state (m).track->key);
            auto kb = info.removeFromRight (40).reduced (2, 1).toFloat();
            g.setColour (compatible ? theme::good.withAlpha (0.25f) : theme::bg3);
            g.fillRoundedRectangle (kb, 3.0f);
            g.setColour (compatible ? theme::good : theme::textDim);
            g.setFont (theme::font (11.0f, true));
            g.drawText (key.toCamelot(), kb, juce::Justification::centred);
        }
    }

    g.setColour (theme::text);
    g.setFont (theme::font (compact ? 13.0f : 15.0f, true));
    auto titleArea = header.removeFromTop (header.getHeight() / 2 + 2);
    g.drawFittedText (s.track != nullptr ? s.info.title : (s.loading ? juce::String ("Loading...") : juce::String ("Empty deck")),
                      titleArea, juce::Justification::centredLeft, 1, 0.85f);
    g.setColour (theme::textDim);
    g.setFont (theme::font (11.5f));
    juce::String sub = s.info.artist;
    if (s.track != nullptr)
    {
        const double pos = dk.positionSec.load();
        sub << (sub.isNotEmpty() ? "   " : "") << juce::String ((int) pos / 60) << ":" << juce::String ((int) pos % 60).paddedLeft ('0', 2)
            << " / " << juce::String ((int) s.track->lengthSeconds() / 60) << ":" << juce::String ((int) s.track->lengthSeconds() % 60).paddedLeft ('0', 2);
        if (s.stemProgress >= 0.0f && s.stemProgress < 1.5f)
            sub << "   stems " << (int) (s.stemProgress * 100) << "%";
        else if (s.track->hasStems())
            sub << (s.stemsHQ ? "   stems HQ" : "   stems");
    }
    g.drawFittedText (sub, header, juce::Justification::centredLeft, 1, 0.85f);
}

void DeckPanel::layoutPads (juce::Rectangle<int> area)
{
    auto tabs = area.removeFromTop (24);
    const int tw = tabs.getWidth() / 4;
    for (auto& b : modeButtons)
        b.setBounds (tabs.removeFromLeft (tw).reduced (2, 0));
    area.removeFromTop (4);
    auto loopRow = area.removeFromBottom (26);
    const int lw = loopRow.getWidth() / 6;
    jumpBack.setBounds (loopRow.removeFromLeft (lw).reduced (2, 0));
    loopIn.setBounds (loopRow.removeFromLeft (lw).reduced (2, 0));
    loopOut.setBounds (loopRow.removeFromLeft (lw).reduced (2, 0));
    loopHalf.setBounds (loopRow.removeFromLeft (lw).reduced (2, 0));
    loopDbl.setBounds (loopRow.removeFromLeft (lw).reduced (2, 0));
    jumpFwd.setBounds (loopRow.reduced (2, 0));
    area.removeFromBottom (4);
    const int pw = area.getWidth() / 4, ph = area.getHeight() / 2;
    for (int i = 0; i < 8; ++i)
        pads[(size_t) i].setBounds (area.getX() + (i % 4) * pw, area.getY() + (i / 4) * ph, pw, ph);
}

void DeckPanel::resized()
{
    auto r = getLocalBounds().reduced (10, 6);
    r.removeFromTop (compact ? 32 : 40);
    overview.setBounds (r.removeFromTop (compact ? 24 : 34));
    r.removeFromTop (6);

    auto fxRow = r.removeFromBottom (30);
    flip.setBounds (fxRow.removeFromRight (70));
    fxRow.removeFromRight (6);
    fxOn.setBounds (fxRow.removeFromLeft (40));
    fxRow.removeFromLeft (4);
    fxAmount.setBounds (fxRow.removeFromLeft (30));
    fxRow.removeFromLeft (4);
    fxBox.setBounds (fxRow.removeFromLeft (juce::jmin (130, fxRow.getWidth())));
    r.removeFromBottom (6);

    auto padsArea = r.removeFromBottom (compact ? 112 : 128);
    layoutPads (padsArea);
    r.removeFromBottom (6);

    // left: jog / transport, right: pitch
    auto pitchCol = r.removeFromRight (36);
    range.setBounds (pitchCol.removeFromBottom (22));
    pitch.setBounds (pitchCol);
    r.removeFromRight (6);
    if (! compact)
    {
        const int js = juce::jmin (r.getHeight(), r.getWidth() / 2);
        jog.setBounds (r.removeFromLeft (js));
        r.removeFromLeft (8);
    }
    auto buttons = r;
    const int bh = juce::jmax (22, juce::jmin (30, (buttons.getHeight() - 12) / 4));
    auto row1 = buttons.removeFromTop (bh);
    const int w4 = row1.getWidth() / 4;
    sync.setBounds (row1.removeFromLeft (w4).reduced (2, 0));
    master.setBounds (row1.removeFromLeft (w4).reduced (2, 0));
    keylock.setBounds (row1.removeFromLeft (w4).reduced (2, 0));
    quant.setBounds (row1.reduced (2, 0));
    buttons.removeFromTop (4);
    auto row2 = buttons.removeFromTop (bh);
    const int w3 = row2.getWidth() / 3;
    dbl.setBounds (row2.removeFromLeft (w3).reduced (2, 0));
    half.setBounds (row2.removeFromLeft (w3).reduced (2, 0));
    gridHere.setBounds (row2.reduced (2, 0));
    buttons.removeFromTop (6);
    auto row3 = buttons;
    cue.setBounds (row3.removeFromLeft (row3.getWidth() / 2).reduced (2, 0));
    play.setBounds (row3.reduced (2, 0));
}

void DeckPanel::itemDropped (const SourceDetails& d)
{
    dragOver = false;
    const auto desc = d.description.toString();
    if (desc.startsWith ("track:"))
        ctx.decks->loadTrack (deckIndex, desc.substring (6).getLargeIntValue());
    else if (auto f = fileFromDrag (d); f.existsAsFile())
        ctx.decks->loadFile (deckIndex, f);
    repaint();
}

void DeckPanel::filesDropped (const juce::StringArray& files, int, int)
{
    dragOver = false;
    if (! files.isEmpty())
        ctx.decks->loadFile (deckIndex, juce::File (files[0]));
}

} // namespace merge
