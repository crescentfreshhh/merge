#include "Playlist.h"
#include "core/Paths.h"

namespace merge
{

Playlist::Playlist (AppContext& c) : ctx (c)
{
    zoomIn.onClick = [this] { pxPerBeat = juce::jmin (120.0, pxPerBeat * 1.4); repaint(); };
    zoomOut.onClick = [this] { pxPerBeat = juce::jmax (4.0, pxPerBeat / 1.4); repaint(); };
    loopBtn.setClickingTogglesState (true);
    loopBtn.setColour (juce::TextButton::buttonOnColourId, theme::make);
    loopBtn.onClick = [this] { ctx.engine.beat().transport.loopEnabled.store (loopBtn.getToggleState()); repaint(); };
    snapBox.addItem ("Snap: bar", 1);
    snapBox.addItem ("Snap: beat", 2);
    snapBox.addItem ("Snap: 1/16", 3);
    snapBox.setSelectedId (1, juce::dontSendNotification);
    snapBox.onChange = [this] { snapBeats = snapBox.getSelectedId() == 1 ? 4.0 : (snapBox.getSelectedId() == 2 ? 1.0 : 0.25); };
    for (auto* b : { (juce::Component*) &zoomIn, (juce::Component*) &zoomOut, (juce::Component*) &loopBtn, (juce::Component*) &snapBox })
        addAndMakeVisible (b);
    ctx.addChangeListener (this);
    ctx.selectionChanged.addChangeListener (this);
    startTimerHz (30);
}

Playlist::~Playlist()
{
    ctx.removeChangeListener (this);
    ctx.selectionChanged.removeChangeListener (this);
}

void Playlist::timerCallback()
{
    const double p = ctx.engine.beat().transport.positionBeats.load();
    if (std::abs (p - lastPos) > 1.0e-4 && ctx.engine.beat().transport.songMode.load())
    {
        lastPos = p;
        repaint();
    }
}

juce::Rectangle<int> Playlist::patternList() const { return getLocalBounds().withTrimmedTop (34).removeFromLeft (listW); }
juce::Rectangle<int> Playlist::ruler() const { return getLocalBounds().withTrimmedTop (34).withTrimmedLeft (listW + 8 + headerW).removeFromTop (rulerH); }
juce::Rectangle<int> Playlist::timeline() const { return getLocalBounds().withTrimmedTop (34 + rulerH).withTrimmedLeft (listW + 8 + headerW); }
float Playlist::beatToX (double b) const { return (float) (timeline().getX() + (b - scrollBeats) * pxPerBeat); }
double Playlist::xToBeat (float x) const { return (x - timeline().getX()) / pxPerBeat + scrollBeats; }

int Playlist::laneAt (int y) const
{
    const int lane = (y - timeline().getY()) / laneH;
    return lane >= 0 && lane < (int) ctx.project.getLanes().size() ? lane : -1;
}

Playlist::ClipHit Playlist::clipAt (juce::Point<int> p) const
{
    const int lane = laneAt (p.y);
    if (lane < 0) return {};
    const double beat = xToBeat ((float) p.x);
    auto& audio = ctx.project.getAudioClips();
    for (int i = (int) audio.size(); --i >= 0;)
    {
        const auto& a = audio[(size_t) i];
        if (a.lane == lane && beat >= a.start && beat < a.start + a.length)
            return { true, i, (float) p.x > beatToX (a.start + a.length) - 6.0f };
    }
    auto& clips = ctx.project.getClips();
    for (int i = (int) clips.size(); --i >= 0;)
    {
        const auto& c = clips[(size_t) i];
        if (c.lane == lane && beat >= c.start && beat < c.start + c.length)
            return { false, i, (float) p.x > beatToX (c.start + c.length) - 6.0f };
    }
    return {};
}

void Playlist::resized()
{
    auto top = getLocalBounds().removeFromTop (28);
    zoomIn.setBounds (top.removeFromRight (30));
    zoomOut.setBounds (top.removeFromRight (30));
    top.removeFromRight (8);
    snapBox.setBounds (top.removeFromRight (120));
    top.removeFromRight (8);
    loopBtn.setBounds (top.removeFromRight (60));
    loopBtn.setToggleState (ctx.engine.beat().transport.loopEnabled.load(), juce::dontSendNotification);
}

void Playlist::paint (juce::Graphics& g)
{
    g.fillAll (theme::bg0);
    g.setColour (theme::textDim);
    g.setFont (theme::font (12.0f));
    g.drawText ("Pick a pattern, click a lane to place it. Drag audio files in. Drag in the ruler to set the loop.",
                getLocalBounds().removeFromTop (28).withTrimmedRight (330), juce::Justification::centredLeft);

    // pattern list
    auto list = patternList();
    g.setColour (theme::bg1);
    g.fillRoundedRectangle (list.toFloat(), 6.0f);
    for (int i = 0; i < kNumPatterns; ++i)
    {
        auto r = juce::Rectangle<int> (list.getX() + 6, list.getY() + 6 + i * 26, list.getWidth() - 12, 22);
        const bool empty = ctx.project.getPatterns()[(size_t) i].isEmpty();
        g.setColour (i == selectedPatternForDraw ? Project::colourForIndex (i).withAlpha (0.9f) : theme::bg2);
        g.fillRoundedRectangle (r.toFloat(), 4.0f);
        g.setColour (i == selectedPatternForDraw ? juce::Colours::black : (empty ? theme::textFaint : theme::text));
        g.setFont (theme::font (12.5f, true));
        g.drawText ("Pattern " + Project::patternName (i) + (empty ? juce::String (" (empty)") : juce::String()),
                    r.reduced (8, 0), juce::Justification::centredLeft);
    }

    auto tl = timeline();
    auto& lanes = ctx.project.getLanes();
    const auto& t = ctx.engine.beat().transport;

    // lanes
    for (int l = 0; l < (int) lanes.size(); ++l)
    {
        auto row = juce::Rectangle<int> (tl.getX(), tl.getY() + l * laneH, tl.getWidth(), laneH);
        if (row.getY() > getHeight()) break;
        g.setColour (l % 2 ? theme::bg1 : theme::bg1.brighter (0.03f));
        g.fillRect (row);
        auto head = juce::Rectangle<int> (tl.getX() - headerW, row.getY(), headerW - 4, laneH - 2);
        g.setColour (theme::bg2);
        g.fillRect (head);
        g.setColour (lanes[(size_t) l].muted ? theme::textFaint : theme::text);
        g.setFont (theme::font (12.5f, true));
        g.drawText (lanes[(size_t) l].name, head.reduced (8, 0), juce::Justification::centredLeft);
        g.setColour (lanes[(size_t) l].muted ? theme::warn : theme::bg3);
        g.fillRoundedRectangle (head.removeFromRight (26).reduced (4, 12).toFloat(), 3.0f);
        g.setColour (lanes[(size_t) l].muted ? juce::Colours::black : theme::textDim);
        g.setFont (theme::font (10.0f, true));
        g.drawText ("M", juce::Rectangle<int> (tl.getX() - 30, row.getY(), 26, laneH), juce::Justification::centred);
    }

    {
        juce::Graphics::ScopedSaveState s (g);
        g.reduceClipRegion (tl);
        // grid
        const double firstBeat = std::floor (scrollBeats);
        for (double b = firstBeat; beatToX (b) < tl.getRight(); b += 1.0)
        {
            const int bi = (int) b;
            if (pxPerBeat < 8.0 && bi % 4 != 0) continue;
            g.setColour (bi % 16 == 0 ? theme::textFaint : (bi % 4 == 0 ? theme::line.brighter (0.15f) : theme::line.withAlpha (0.4f)));
            g.drawVerticalLine ((int) beatToX (b), (float) tl.getY(), (float) tl.getBottom());
        }
        // loop shading
        if (t.loopEnabled.load())
        {
            g.setColour (theme::make.withAlpha (0.06f));
            g.fillRect (juce::Rectangle<float>::leftTopRightBottom (beatToX (t.loopStart.load()), (float) tl.getY(),
                                                                    beatToX (t.loopEnd.load()), (float) tl.getBottom()));
        }
        // pattern clips
        for (auto& c : ctx.project.getClips())
        {
            auto r = juce::Rectangle<float> (beatToX (c.start), (float) (tl.getY() + c.lane * laneH + 2),
                                             (float) (c.length * pxPerBeat) - 1.0f, (float) laneH - 5.0f);
            const auto col = Project::colourForIndex (c.pattern);
            g.setColour (col.withAlpha (0.28f));
            g.fillRoundedRectangle (r, 4.0f);
            g.setColour (col);
            g.drawRoundedRectangle (r, 4.0f, 1.2f);
            // mini steps preview
            const auto& p = ctx.project.getPatterns()[(size_t) juce::jlimit (0, kNumPatterns - 1, c.pattern)];
            const double pl = p.lengthBeats();
            const int rows = (int) ctx.project.getChannels().size();
            if (pl > 0 && rows > 0)
            {
                const float rowHgt = juce::jmax (1.0f, (r.getHeight() - 16.0f) / (float) juce::jmin (rows, 16));
                for (double cycle = 0; cycle < c.length; cycle += pl)
                    for (auto& [id, steps] : p.steps)
                    {
                        const int ci = ctx.project.indexOfChannel (id);
                        if (ci < 0 || ci >= 16) continue;
                        for (size_t sIdx = 0; sIdx < steps.size(); ++sIdx)
                            if (steps[sIdx] > 0 && cycle + sIdx * kStepBeats < c.length)
                            {
                                g.setColour (col.withAlpha (0.8f));
                                g.fillRect (r.getX() + (float) ((cycle + sIdx * kStepBeats) * pxPerBeat), r.getY() + 14.0f + ci * rowHgt,
                                            juce::jmax (1.0f, (float) (kStepBeats * pxPerBeat) - 1.0f), juce::jmax (1.0f, rowHgt - 0.5f));
                            }
                    }
            }
            g.setColour (theme::text);
            g.setFont (theme::font (11.0f, true));
            g.drawText (Project::patternName (c.pattern), r.reduced (5, 1).withHeight (13), juce::Justification::topLeft);
        }
        // audio clips
        for (auto& a : ctx.project.getAudioClips())
        {
            auto r = juce::Rectangle<float> (beatToX (a.start), (float) (tl.getY() + a.lane * laneH + 2),
                                             (float) (a.length * pxPerBeat) - 1.0f, (float) laneH - 5.0f);
            g.setColour (theme::mix.withAlpha (0.25f));
            g.fillRoundedRectangle (r, 4.0f);
            g.setColour (theme::mix);
            g.drawRoundedRectangle (r, 4.0f, 1.2f);
            if (a.sample != nullptr)
            {
                const auto& au = a.sample->getAudio();
                const double secPerBeat = 60.0 / ctx.project.bpm;
                const float mid = r.getCentreY() + 5.0f, half = (r.getHeight() - 16.0f) * 0.5f;
                for (int x = 0; x < (int) r.getWidth(); x += 2)
                {
                    const double sec = a.offsetSeconds + (x / pxPerBeat) * secPerBeat;
                    const int s0 = (int) (sec * a.sample->getSampleRate());
                    const int s1 = (int) (s0 + 2.0 / pxPerBeat * secPerBeat * a.sample->getSampleRate());
                    if (s0 >= au.getNumSamples()) break;
                    const auto mm = au.findMinMax (0, s0, juce::jmax (1, juce::jmin (s1, au.getNumSamples()) - s0));
                    g.drawVerticalLine ((int) r.getX() + x, mid - mm.getEnd() * half, mid - mm.getStart() * half + 1.0f);
                }
            }
            g.setColour (theme::text);
            g.setFont (theme::font (11.0f, true));
            g.drawText (a.name, r.reduced (5, 1).withHeight (13), juce::Justification::topLeft);
        }
        // playhead
        if (t.songMode.load())
        {
            g.setColour (theme::text);
            g.drawVerticalLine ((int) beatToX (t.positionBeats.load()), (float) tl.getY(), (float) tl.getBottom());
        }
    }

    // ruler
    auto ru = ruler();
    g.setColour (theme::bg2);
    g.fillRect (ru);
    if (t.loopEnabled.load())
    {
        g.setColour (theme::make.withAlpha (0.5f));
        g.fillRect (juce::Rectangle<float>::leftTopRightBottom (juce::jmax ((float) ru.getX(), beatToX (t.loopStart.load())), (float) ru.getY() + 2.0f,
                                                                juce::jmin ((float) ru.getRight(), beatToX (t.loopEnd.load())), (float) ru.getY() + 8.0f));
    }
    g.setFont (theme::font (10.5f, true));
    for (double b = std::floor (scrollBeats / 4.0) * 4.0; beatToX (b) < ru.getRight(); b += 4.0)
    {
        const float x = beatToX (b);
        if (x < ru.getX()) continue;
        g.setColour (theme::text);
        g.drawVerticalLine ((int) x, (float) ru.getY() + 10.0f, (float) ru.getBottom());
        if (pxPerBeat * 4 > 24 || ((int) b / 4) % 4 == 0)
            g.drawText (juce::String ((int) b / 4 + 1), (int) x + 3, ru.getY() + 8, 30, rulerH - 8, juce::Justification::centredLeft);
    }
}

void Playlist::mouseMove (const juce::MouseEvent& e)
{
    const auto hit = clipAt (e.getPosition());
    setMouseCursor (hit.index >= 0 ? (hit.rightEdge ? juce::MouseCursor::LeftRightResizeCursor : juce::MouseCursor::DraggingHandCursor)
                                   : juce::MouseCursor::NormalCursor);
}

void Playlist::mouseDown (const juce::MouseEvent& e)
{
    const auto p = e.getPosition();
    // pattern list
    if (patternList().contains (p))
    {
        const int i = (p.y - patternList().getY() - 6) / 26;
        if (i >= 0 && i < kNumPatterns)
        {
            selectedPatternForDraw = i;
            if (e.getNumberOfClicks() > 1)
                ctx.selectPattern (i);
            repaint();
        }
        return;
    }
    // lane mute buttons
    if (p.x >= timeline().getX() - 30 && p.x < timeline().getX() && p.y >= timeline().getY())
    {
        const int lane = laneAt (p.y);
        if (lane >= 0)
        {
            ctx.project.getLanes()[(size_t) lane].muted = ! ctx.project.getLanes()[(size_t) lane].muted;
            ctx.projectEdited();
        }
        return;
    }
    if (ruler().contains (p))
    {
        dragStartBeat = juce::jmax (0.0, snap (xToBeat ((float) p.x)));
        drag = e.mods.isShiftDown() || e.getNumberOfClicks() > 1 ? Drag::Loop : Drag::Locate;
        loopAnchor = dragStartBeat;
        if (drag == Drag::Locate)
            ctx.engine.beat().pushEvent ({ BeatEvent::Locate, 0, 0, 0.0f, xToBeat ((float) p.x) });
        return;
    }
    if (! timeline().contains (p)) return;

    auto hit = clipAt (p);
    if (e.mods.isPopupMenu())
    {
        if (hit.index >= 0)
        {
            ctx.project.checkpoint();
            if (hit.audio) ctx.project.getAudioClips().erase (ctx.project.getAudioClips().begin() + hit.index);
            else ctx.project.getClips().erase (ctx.project.getClips().begin() + hit.index);
            ctx.projectEdited();
        }
        return;
    }
    ctx.project.checkpoint();
    if (hit.index < 0)
    {
        const int lane = laneAt (p.y);
        if (lane < 0) return;
        const double len = ctx.project.getPatterns()[(size_t) selectedPatternForDraw].lengthBeats();
        const double start = juce::jmax (0.0, std::floor (xToBeat ((float) p.x) / snapBeats) * snapBeats);
        ctx.project.getClips().push_back ({ selectedPatternForDraw, lane, start, std::ceil (len / 4.0) * 4.0, 0.0 });
        hit = { false, (int) ctx.project.getClips().size() - 1, false };
        ctx.projectEdited();
    }
    dragClip = hit;
    drag = hit.rightEdge ? Drag::Resize : Drag::Move;
    dragStartBeat = xToBeat ((float) p.x);
    if (hit.audio)
    {
        auto& a = ctx.project.getAudioClips()[(size_t) hit.index];
        clipStartAtDrag = a.start; clipLenAtDrag = a.length; clipLaneAtDrag = a.lane;
    }
    else
    {
        auto& c = ctx.project.getClips()[(size_t) hit.index];
        clipStartAtDrag = c.start; clipLenAtDrag = c.length; clipLaneAtDrag = c.lane;
        selectedPatternForDraw = c.pattern;
    }
    repaint();
}

void Playlist::mouseDrag (const juce::MouseEvent& e)
{
    const auto p = e.getPosition();
    auto& t = ctx.engine.beat().transport;
    if (drag == Drag::Loop || (drag == Drag::Locate && std::abs (e.getDistanceFromDragStartX()) > 6))
    {
        drag = Drag::Loop;
        const double b = juce::jmax (0.0, snap (xToBeat ((float) p.x)));
        t.loopStart.store (juce::jmin (loopAnchor, b));
        t.loopEnd.store (juce::jmax (loopAnchor, b) + (std::abs (b - loopAnchor) < 1e-9 ? snapBeats : 0.0));
        t.loopEnabled.store (true);
        loopBtn.setToggleState (true, juce::dontSendNotification);
        repaint();
        return;
    }
    if (dragClip.index < 0) return;
    const double delta = xToBeat ((float) p.x) - dragStartBeat;
    const int lane = juce::jmax (0, laneAt (p.y) >= 0 ? laneAt (p.y) : clipLaneAtDrag);
    auto apply = [&] (double& start, double& length, int& l)
    {
        if (drag == Drag::Move)
        {
            start = juce::jmax (0.0, snap (clipStartAtDrag + delta));
            l = lane;
        }
        else
        {
            length = juce::jmax (snapBeats, snap (clipStartAtDrag + clipLenAtDrag + delta) - start);
        }
    };
    if (dragClip.audio)
    {
        auto& a = ctx.project.getAudioClips()[(size_t) dragClip.index];
        apply (a.start, a.length, a.lane);
    }
    else
    {
        auto& c = ctx.project.getClips()[(size_t) dragClip.index];
        apply (c.start, c.length, c.lane);
    }
    ctx.projectEdited();
    repaint();
}

void Playlist::mouseUp (const juce::MouseEvent&)
{
    drag = Drag::None;
    dragClip = {};
}

void Playlist::mouseDoubleClick (const juce::MouseEvent& e)
{
    auto hit = clipAt (e.getPosition());
    if (hit.index >= 0 && ! hit.audio)
        ctx.selectPattern (ctx.project.getClips()[(size_t) hit.index].pattern);
}

void Playlist::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    if (e.mods.isCtrlDown() || e.mods.isCommandDown())
        pxPerBeat = juce::jlimit (4.0, 120.0, pxPerBeat * (w.deltaY > 0 ? 1.15 : 0.87));
    else
        scrollBeats = juce::jmax (0.0, scrollBeats - (w.deltaX != 0 ? w.deltaX : w.deltaY) * 400.0 / pxPerBeat);
    repaint();
}

void Playlist::addAudioClip (const juce::File& f, juce::Point<int> where)
{
    auto sample = SampleCache::get().load (f);
    if (sample == nullptr) return;
    ctx.project.checkpoint();
    AudioClipModel a;
    a.sample = sample;
    a.sampleRef = paths::toRef (f);
    a.name = f.getFileNameWithoutExtension();
    a.lane = juce::jmax (0, laneAt (where.y));
    a.start = juce::jmax (0.0, std::floor (xToBeat ((float) where.x) / snapBeats) * snapBeats);
    a.length = sample->getLengthSeconds() * ctx.project.bpm / 60.0;
    ctx.project.getAudioClips().push_back (a);
    ctx.projectEdited();
}

void Playlist::itemDropped (const SourceDetails& d)
{
    if (auto f = fileFromDrag (d); f.existsAsFile())
        addAudioClip (f, d.localPosition);
}

void Playlist::filesDropped (const juce::StringArray& files, int x, int y)
{
    int lane = 0;
    for (auto& f : files)
        addAudioClip (juce::File (f), { x, y + (lane++) * laneH });
}

} // namespace merge
