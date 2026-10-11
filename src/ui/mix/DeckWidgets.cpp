#include "DeckWidgets.h"

namespace merge
{

void drawWaveform (juce::Graphics& g, const analysis::Waveform& w, juce::Rectangle<float> area, double startSec,
                   double endSec, float alpha)
{
    if (w.points.empty() || w.pointsPerSecond <= 0.0 || endSec <= startSec) return;
    const float mid = area.getCentreY();
    const float half = area.getHeight() * 0.5f;
    const int width = (int) area.getWidth();
    const double secPerPx = (endSec - startSec) / juce::jmax (1, width);
    for (int x = 0; x < width; ++x)
    {
        const double t0 = startSec + x * secPerPx;
        if (t0 < 0.0) continue;
        int i0 = (int) (t0 * w.pointsPerSecond);
        int i1 = juce::jmax (i0 + 1, (int) ((t0 + secPerPx) * w.pointsPerSecond));
        if (i0 >= (int) w.points.size()) break;
        i1 = juce::jmin (i1, (int) w.points.size());
        int lo = 0, mi = 0, hi = 0, pk = 0;
        for (int i = i0; i < i1; ++i)
        {
            const auto& p = w.points[(size_t) i];
            lo = juce::jmax (lo, (int) p.low);
            mi = juce::jmax (mi, (int) p.mid);
            hi = juce::jmax (hi, (int) p.high);
            pk = juce::jmax (pk, (int) p.peak);
        }
        const float total = (float) (lo + mi + hi) + 1.0f;
        // Serato-style colouring: bass = red, mids = green, highs = blue
        const auto colour = juce::Colour::fromFloatRGBA (juce::jlimit (0.15f, 1.0f, (lo * 1.0f + mi * 0.35f) / total * 1.6f),
                                                         juce::jlimit (0.15f, 1.0f, (mi * 1.0f + hi * 0.3f) / total * 1.6f),
                                                         juce::jlimit (0.15f, 1.0f, (hi * 1.2f + lo * 0.15f) / total * 1.6f), alpha);
        const float h = pk / 255.0f * half;
        g.setColour (colour);
        g.drawVerticalLine ((int) area.getX() + x, mid - h, mid + h);
        // brighter core for the bass
        const float hl = lo / 255.0f * half * 0.8f;
        g.setColour (colour.brighter (0.4f).withAlpha (alpha));
        g.drawVerticalLine ((int) area.getX() + x, mid - hl, mid + hl);
    }
}

//==============================================================================
WaveStrip::WaveStrip (AppContext& c, int d) : ctx (c), deckIndex (d)
{
    startTimerHz (60);
}

void WaveStrip::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setColour (theme::bg0);
    g.fillRect (r);
    auto& s = ctx.decks->state (deckIndex);
    auto& deck = ctx.decks->deck (deckIndex);
    const auto colour = theme::deck (deckIndex);

    if (s.track == nullptr)
    {
        g.setColour (theme::textFaint);
        g.setFont (theme::font (12.0f));
        g.drawText (s.loading ? s.status : "Deck " + juce::String::charToString ((juce::juce_wchar) ('A' + deckIndex)) + " - drag a track here",
                    getLocalBounds(), juce::Justification::centred);
        return;
    }

    const double pos = deck.positionSec.load();
    const double half = secondsVisible * 0.5 / juce::jmax (0.25, deck.tempoRatio.load());
    const double start = pos - half, end = pos + half;
    auto toX = [&] (double t) { return (float) ((t - start) / (end - start) * r.getWidth()); };

    // loop region
    if (deck.loopActive.load())
    {
        g.setColour (theme::good.withAlpha (0.15f));
        g.fillRect (juce::Rectangle<float>::leftTopRightBottom (toX (deck.loopInSec.load()), 0.0f, toX (deck.loopOutSec.load()), r.getHeight()));
    }

    drawWaveform (g, s.waveform, r, start, end);

    // beat grid
    const auto& grid = s.track->grid;
    if (grid.isValid())
    {
        const double spb = grid.secondsPerBeat();
        for (double b = std::ceil (grid.beatAt (juce::jmax (0.0, start))); grid.timeOfBeat (b) < end; b += 1.0)
        {
            if (grid.timeOfBeat (b) < 0.0) continue;
            const float x = toX (grid.timeOfBeat (b));
            const bool bar = std::fmod (b, 4.0) == 0.0 || std::abs (std::fmod (b, 4.0)) < 1e-6;
            g.setColour (bar ? juce::Colours::white.withAlpha (0.55f) : juce::Colours::white.withAlpha (0.18f));
            g.drawVerticalLine ((int) x, 0.0f, bar ? r.getHeight() : r.getHeight() * 0.15f);
            if (! bar) g.drawVerticalLine ((int) x, r.getHeight() * 0.85f, r.getHeight());
        }
        juce::ignoreUnused (spb);
    }

    // cues
    g.setFont (theme::font (10.0f, true));
    for (int i = 0; i < 8; ++i)
    {
        const double c = s.hotCues[(size_t) i];
        if (c < 0.0 || c < start || c > end) continue;
        const float x = toX (c);
        g.setColour (Project::colourForIndex (i));
        g.fillRect (x - 1.0f, 0.0f, 2.0f, r.getHeight());
        g.fillRect (x, 0.0f, 14.0f, 13.0f);
        g.setColour (juce::Colours::black);
        g.drawText (juce::String (i + 1), (int) x, 0, 14, 13, juce::Justification::centred);
    }
    // main cue
    {
        const float x = toX (deck.cueSec.load());
        g.setColour (theme::warn);
        juce::Path tri;
        tri.addTriangle (x - 5.0f, r.getBottom(), x + 5.0f, r.getBottom(), x, r.getBottom() - 7.0f);
        g.fillPath (tri);
    }

    // playhead
    g.setColour (colour);
    g.fillRect (r.getCentreX() - 1.0f, 0.0f, 2.0f, r.getHeight());
    g.setColour (theme::text);
    g.setFont (theme::font (11.0f, true));
    g.drawText (juce::String::charToString ((juce::juce_wchar) ('A' + deckIndex)), 6, 2, 20, 14, juce::Justification::left);
}

void WaveStrip::mouseDown (const juce::MouseEvent& e)
{
    if (ctx.decks->state (deckIndex).track == nullptr) return;
    lastDragX = e.position.x;
    lastDragTime = juce::Time::getMillisecondCounterHiRes();
    ctx.decks->scratchStart (deckIndex);
}

void WaveStrip::mouseDrag (const juce::MouseEvent& e)
{
    const double now = juce::Time::getMillisecondCounterHiRes();
    const double dt = juce::jmax (1.0, now - lastDragTime) * 0.001;
    const double dx = e.position.x - lastDragX;
    const double secPerPx = secondsVisible / juce::jmax (1, getWidth());
    // dragging right pulls the record backwards (like a platter)
    const double rate = -dx * secPerPx / dt;
    ctx.decks->scratchMove (deckIndex, juce::jlimit (-8.0, 8.0, rate));
    lastDragX = e.position.x;
    lastDragTime = now;
}

void WaveStrip::mouseUp (const juce::MouseEvent&)
{
    ctx.decks->scratchEnd (deckIndex);
}

void WaveStrip::mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& w)
{
    secondsVisible = juce::jlimit (2.0, 32.0, secondsVisible * (w.deltaY > 0 ? 0.85 : 1.18));
}

//==============================================================================
OverviewWave::OverviewWave (AppContext& c, int d) : ctx (c), deckIndex (d)
{
    startTimerHz (15);
}

void OverviewWave::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setColour (theme::bg0);
    g.fillRoundedRectangle (r, 3.0f);
    auto& s = ctx.decks->state (deckIndex);
    if (s.track == nullptr) return;
    const double len = juce::jmax (0.1, s.track->lengthSeconds());

    if (cachedTrack != s.info.id || cachedWidth != getWidth() || cache.isNull())
    {
        cache = juce::Image (juce::Image::ARGB, juce::jmax (1, getWidth()), juce::jmax (1, getHeight()), true);
        juce::Graphics cg (cache);
        drawWaveform (cg, s.waveform, cache.getBounds().toFloat(), 0.0, len, 0.9f);
        cachedTrack = s.info.id;
        cachedWidth = getWidth();
    }
    g.drawImageAt (cache, 0, 0);
    const double pos = ctx.decks->deck (deckIndex).positionSec.load();
    const float x = (float) (pos / len * r.getWidth());
    g.setColour (juce::Colours::black.withAlpha (0.45f));
    g.fillRect (0.0f, 0.0f, x, r.getHeight());
    for (int i = 0; i < 8; ++i)
        if (s.hotCues[(size_t) i] >= 0.0)
        {
            g.setColour (Project::colourForIndex (i));
            g.fillRect ((float) (s.hotCues[(size_t) i] / len * r.getWidth()), 0.0f, 2.0f, 6.0f);
        }
    g.setColour (theme::text);
    g.fillRect (x - 1.0f, 0.0f, 2.0f, r.getHeight());
}

void OverviewWave::mouseDown (const juce::MouseEvent&)
{
    const auto p = getMouseXYRelative();
    ctx.decks->seekFraction (deckIndex, (double) p.x / juce::jmax (1, getWidth()));
}

//==============================================================================
JogWheel::JogWheel (AppContext& c, int d) : ctx (c), deckIndex (d)
{
    startTimerHz (40);
}

void JogWheel::timerCallback()
{
    if (! scratching)
        repaint();
}

float JogWheel::angleAt (juce::Point<float> p) const
{
    const auto c = getLocalBounds().toFloat().getCentre();
    return std::atan2 (p.y - c.y, p.x - c.x);
}

void JogWheel::paint (juce::Graphics& g)
{
    const auto r = getLocalBounds().toFloat().reduced (2.0f);
    const float size = juce::jmin (r.getWidth(), r.getHeight());
    const auto disc = juce::Rectangle<float> (size, size).withCentre (r.getCentre());
    const auto colour = theme::deck (deckIndex);
    auto& deck = ctx.decks->deck (deckIndex);

    g.setColour (theme::bg2);
    g.fillEllipse (disc);
    g.setColour (colour.withAlpha (deck.playing.load() ? 0.9f : 0.4f));
    g.drawEllipse (disc.reduced (2.0f), 3.0f);
    g.setColour (theme::bg0);
    g.fillEllipse (disc.reduced (size * 0.32f));

    // one rotation every 1.8 s at normal speed (33 1/3 rpm)
    const double pos = deck.positionSec.load();
    const float angle = (float) std::fmod (pos / 1.8, 1.0) * juce::MathConstants<float>::twoPi;
    const auto c = disc.getCentre();
    const float rad = size * 0.42f;
    g.setColour (theme::text);
    g.drawLine (c.x + std::sin (angle) * rad * 0.55f, c.y - std::cos (angle) * rad * 0.55f,
                c.x + std::sin (angle) * rad, c.y - std::cos (angle) * rad, 3.0f);

    auto& s = ctx.decks->state (deckIndex);
    if (s.track != nullptr && s.track->lengthSeconds() > 0)
    {
        const double remaining = s.track->lengthSeconds() - pos;
        g.setFont (theme::font (11.0f, true));
        g.setColour (remaining < 30.0 && deck.playing.load() ? theme::rec : theme::textDim);
        g.drawText ("-" + juce::String ((int) remaining / 60) + ":" + juce::String ((int) remaining % 60).paddedLeft ('0', 2),
                    disc.reduced (size * 0.32f).toNearestInt(), juce::Justification::centred);
    }
}

void JogWheel::mouseDown (const juce::MouseEvent& e)
{
    if (ctx.decks->state (deckIndex).track == nullptr) return;
    scratching = true;
    lastAngle = angleAt (e.position);
    lastTime = juce::Time::getMillisecondCounterHiRes();
    if (e.mods.isShiftDown())
        return; // shift = pitch bend (nudge) only
    ctx.decks->scratchStart (deckIndex);
}

void JogWheel::mouseDrag (const juce::MouseEvent& e)
{
    const float a = angleAt (e.position);
    float da = a - lastAngle;
    if (da > juce::MathConstants<float>::pi) da -= juce::MathConstants<float>::twoPi;
    if (da < -juce::MathConstants<float>::pi) da += juce::MathConstants<float>::twoPi;
    const double now = juce::Time::getMillisecondCounterHiRes();
    const double dt = juce::jmax (1.0, now - lastTime) * 0.001;
    // a full turn = 1.8 s of audio
    const double rate = (da / juce::MathConstants<float>::twoPi) * 1.8 / dt;
    if (e.mods.isShiftDown())
        ctx.decks->nudge (deckIndex, (float) juce::jlimit (-0.1, 0.1, rate * 0.05));
    else
        ctx.decks->scratchMove (deckIndex, juce::jlimit (-8.0, 8.0, rate));
    lastAngle = a;
    lastTime = now;
    repaint();
}

void JogWheel::mouseUp (const juce::MouseEvent& e)
{
    scratching = false;
    if (e.mods.isShiftDown())
        ctx.decks->nudge (deckIndex, 0.0f);
    else
        ctx.decks->scratchEnd (deckIndex);
    ctx.decks->nudge (deckIndex, 0.0f);
}

} // namespace merge
