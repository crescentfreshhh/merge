#include "PianoRoll.h"

namespace merge
{

static bool isBlackKey (int pitch)
{
    const int k = pitch % 12;
    return k == 1 || k == 3 || k == 6 || k == 8 || k == 10;
}

PianoRoll::PianoRoll (AppContext& c) : ctx (c)
{
    title.setFont (theme::font (15.0f, true));
    addAndMakeVisible (title);
    snapBox.addItem ("Snap 1/4", 1);
    snapBox.addItem ("Snap 1/8", 2);
    snapBox.addItem ("Snap 1/16", 3);
    snapBox.addItem ("Snap 1/32", 4);
    snapBox.addItem ("Snap 1/8 T", 5);
    snapBox.addItem ("Snap 1/16 T", 6);
    snapBox.addItem ("No snap", 7);
    snapBox.setSelectedId (3, juce::dontSendNotification);
    addAndMakeVisible (snapBox);
    chordBox.addItem ("Single notes", 1);
    chordBox.addItem ("Triads (in key)", 2);
    chordBox.addItem ("7th chords (in key)", 3);
    chordBox.addItem ("Power chord", 4);
    chordBox.addItem ("Octaves", 5);
    chordBox.setSelectedId (1, juce::dontSendNotification);
    chordBox.setTooltip ("Draw chords with one click");
    addAndMakeVisible (chordBox);
    scaleToggle.setToggleState (true, juce::dontSendNotification);
    scaleToggle.setTooltip ("Highlight notes in the project key");
    scaleToggle.onClick = [this] { repaint(); };
    addAndMakeVisible (scaleToggle);
    quantizeBtn.onClick = [this]
    {
        auto notes = currentNotes();
        for (auto& n : notes)
        {
            n.start = snap (n.start);
            n.length = juce::jmax (snapBeats(), std::round (n.length / snapBeats()) * snapBeats());
        }
        commit (notes);
    };
    addAndMakeVisible (quantizeBtn);
    clearBtn.onClick = [this] { commit ({}); };
    addAndMakeVisible (clearBtn);
    zoom.setSliderStyle (juce::Slider::LinearHorizontal);
    zoom.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    zoom.setRange (20.0, 300.0);
    zoom.setValue (pxPerBeat, juce::dontSendNotification);
    zoom.onValueChange = [this] { pxPerBeat = zoom.getValue(); repaint(); };
    addAndMakeVisible (zoom);
    vscroll.setRangeLimits (0.0, 128.0 * rowH);
    vscroll.addListener (this);
    addAndMakeVisible (vscroll);
    setWantsKeyboardFocus (true);

    ctx.selectionChanged.addChangeListener (this);
    ctx.addChangeListener (this);
    startTimerHz (30);
}

PianoRoll::~PianoRoll()
{
    ctx.selectionChanged.removeChangeListener (this);
    ctx.removeChangeListener (this);
}

void PianoRoll::changeListenerCallback (juce::ChangeBroadcaster*)
{
    if (auto* ch = ctx.project.findChannel (ctx.selectedChannelId))
        title.setText (ch->name + "  -  Pattern " + Project::patternName (ctx.selectedPattern())
                           + (ctx.project.key.isValid() ? "  -  " + ctx.project.key.toString() : juce::String()),
                       juce::dontSendNotification);
    repaint();
}

void PianoRoll::timerCallback()
{
    const auto& t = ctx.engine.beat().transport;
    const int s = t.playing.load() && ! t.songMode.load() ? (int) (t.positionBeats.load() * 8) : -1;
    if (s != playStep) { playStep = s; repaint(); }
}

juce::Rectangle<int> PianoRoll::keyboardArea() const
{
    auto r = getLocalBounds().withTrimmedTop (36 + rulerH).withTrimmedBottom (velH + 6);
    return r.removeFromLeft (keyW);
}

juce::Rectangle<int> PianoRoll::gridArea() const
{
    return getLocalBounds().withTrimmedTop (36 + rulerH).withTrimmedBottom (velH + 6).withTrimmedLeft (keyW).withTrimmedRight (14);
}

juce::Rectangle<int> PianoRoll::velocityArea() const
{
    return getLocalBounds().removeFromBottom (velH).withTrimmedLeft (keyW).withTrimmedRight (14);
}

double PianoRoll::patternBeats() const
{
    return ctx.project.getPatterns()[(size_t) ctx.selectedPattern()].lengthBeats();
}

float PianoRoll::beatToX (double beat) const { return (float) (gridArea().getX() + beat * pxPerBeat); }
double PianoRoll::xToBeat (float x) const { return (x - gridArea().getX()) / pxPerBeat; }
float PianoRoll::pitchToY (int pitch) const { return (float) (gridArea().getY() + (127 - pitch) * rowH - vscroll.getCurrentRangeStart()); }
int PianoRoll::yToPitch (float y) const { return juce::jlimit (0, 127, 127 - (int) std::floor ((y - gridArea().getY() + vscroll.getCurrentRangeStart()) / rowH)); }

double PianoRoll::snapBeats() const
{
    switch (snapBox.getSelectedId())
    {
        case 1: return 1.0;
        case 2: return 0.5;
        case 3: return 0.25;
        case 4: return 0.125;
        case 5: return 1.0 / 3.0;
        case 6: return 1.0 / 6.0;
        default: return 1.0 / 64.0;
    }
}

double PianoRoll::snap (double beat) const
{
    const double s = snapBeats();
    return std::round (beat / s) * s;
}

std::vector<Note> PianoRoll::currentNotes() const
{
    if (auto* n = ctx.project.getNotes (ctx.selectedPattern(), ctx.selectedChannelId))
        return *n;
    return {};
}

void PianoRoll::commit (std::vector<Note> notes)
{
    ctx.project.checkpoint();
    ctx.project.setNotes (ctx.selectedPattern(), ctx.selectedChannelId, std::move (notes));
    ctx.projectEdited();
    repaint();
}

std::vector<int> PianoRoll::chordFor (int root) const
{
    const auto key = ctx.project.key;
    auto inKeyStack = [&] (int count)
    {
        std::vector<int> out { root };
        int p = root;
        while ((int) out.size() < count && p < 127)
        {
            // next scale tone a third above (skip one scale degree)
            int steps = 0;
            do { ++p; if (! key.isValid() || key.isInScale (p)) ++steps; } while (steps < 2 && p < 127);
            out.push_back (p);
        }
        return out;
    };
    switch (chordBox.getSelectedId())
    {
        case 2: return key.isValid() ? inKeyStack (3) : std::vector<int> { root, root + 4, root + 7 };
        case 3: return key.isValid() ? inKeyStack (4) : std::vector<int> { root, root + 4, root + 7, root + 10 };
        case 4: return { root, root + 7, root + 12 };
        case 5: return { root, root + 12 };
        default: return { root };
    }
}

int PianoRoll::noteAt (juce::Point<float> p, bool& onRightEdge) const
{
    const auto notes = currentNotes();
    for (int i = (int) notes.size(); --i >= 0;)
    {
        const auto& n = notes[(size_t) i];
        juce::Rectangle<float> r (beatToX (n.start), pitchToY (n.pitch), (float) (n.length * pxPerBeat), (float) rowH);
        if (r.contains (p))
        {
            onRightEdge = p.x > r.getRight() - 6.0f;
            return i;
        }
    }
    onRightEdge = false;
    return -1;
}

void PianoRoll::audition (int pitch)
{
    const int idx = ctx.project.indexOfChannel (ctx.selectedChannelId);
    if (idx < 0) return;
    if (auditionNote >= 0) ctx.engine.beat().noteOff (idx, auditionNote);
    auditionNote = pitch;
    if (pitch >= 0) ctx.engine.beat().noteOn (idx, pitch, 0.8f);
}

void PianoRoll::resized()
{
    auto top = getLocalBounds().removeFromTop (30);
    title.setBounds (top.removeFromLeft (juce::jmax (200, top.getWidth() / 3)));
    zoom.setBounds (top.removeFromRight (120));
    top.removeFromRight (6);
    clearBtn.setBounds (top.removeFromRight (60));
    top.removeFromRight (6);
    quantizeBtn.setBounds (top.removeFromRight (80));
    top.removeFromRight (6);
    scaleToggle.setBounds (top.removeFromRight (70));
    top.removeFromRight (6);
    chordBox.setBounds (top.removeFromRight (150));
    top.removeFromRight (6);
    snapBox.setBounds (top.removeFromRight (110));

    auto g = gridArea();
    vscroll.setBounds (g.getRight(), g.getY(), 14, g.getHeight());
    vscroll.setCurrentRange (vscroll.getCurrentRangeStart(), g.getHeight());
    if (vscroll.getCurrentRangeStart() == 0.0)
        vscroll.setCurrentRangeStart ((127 - 84) * rowH); // start around C6..C2
}

void PianoRoll::paint (juce::Graphics& g)
{
    g.fillAll (theme::bg0);
    auto* ch = ctx.project.findChannel (ctx.selectedChannelId);
    const auto grid = gridArea();
    const auto keys = keyboardArea();
    const auto key = ctx.project.key;
    const bool showScale = scaleToggle.getToggleState() && key.isValid();
    const double len = patternBeats();

    // rows
    {
        juce::Graphics::ScopedSaveState s (g);
        g.reduceClipRegion (grid.getUnion (keys));
        for (int p = 0; p < 128; ++p)
        {
            const float y = pitchToY (p);
            if (y > grid.getBottom() || y + rowH < grid.getY()) continue;
            juce::Colour row = isBlackKey (p) ? theme::bg1 : theme::bg1.brighter (0.04f);
            if (showScale && key.isInScale (p)) row = row.interpolatedWith (theme::make, 0.06f);
            g.setColour (row);
            g.fillRect ((float) grid.getX(), y, (float) grid.getWidth(), (float) rowH);
            g.setColour (theme::bg0);
            g.drawHorizontalLine ((int) y, (float) grid.getX(), (float) grid.getRight());

            // keyboard
            g.setColour (isBlackKey (p) ? juce::Colour (0xff20232b) : juce::Colour (0xffd8dbe2));
            g.fillRect ((float) keys.getX(), y, (float) keys.getWidth() - 1.0f, (float) rowH - 1.0f);
            if (p % 12 == 0)
            {
                g.setColour (juce::Colours::black);
                g.setFont (theme::font (10.0f, true));
                g.drawText ("C" + juce::String (p / 12 - 1), keys.withY ((int) y).withHeight (rowH).reduced (4, 0),
                            juce::Justification::centredRight);
            }
        }
        // beat lines
        for (int s = 0; s <= (int) std::ceil (len / 0.25); ++s)
        {
            const float x = beatToX (s * 0.25);
            g.setColour (s % 16 == 0 ? theme::textFaint : (s % 4 == 0 ? theme::line.brighter (0.2f) : theme::line.withAlpha (0.5f)));
            g.drawVerticalLine ((int) x, (float) grid.getY(), (float) grid.getBottom());
        }
        // beyond pattern end
        g.setColour (juce::Colours::black.withAlpha (0.4f));
        g.fillRect (juce::Rectangle<float> (beatToX (len), (float) grid.getY(), (float) grid.getRight(), (float) grid.getHeight()));

        // notes
        const auto notes = currentNotes();
        const auto colour = ch != nullptr ? ch->colour : theme::make;
        for (int i = 0; i < (int) notes.size(); ++i)
        {
            const auto& n = notes[(size_t) i];
            juce::Rectangle<float> r (beatToX (n.start), pitchToY (n.pitch) + 1.0f, (float) (n.length * pxPerBeat) - 1.0f, (float) rowH - 2.0f);
            const bool sel = std::find (selected.begin(), selected.end(), i) != selected.end();
            g.setColour (colour.withAlpha (0.45f + 0.55f * n.velocity));
            g.fillRoundedRectangle (r, 2.5f);
            g.setColour (sel ? juce::Colours::white : colour.brighter (0.4f));
            g.drawRoundedRectangle (r, 2.5f, sel ? 1.5f : 1.0f);
        }

        if (playStep >= 0)
        {
            const double beat = std::fmod (ctx.engine.beat().transport.positionBeats.load(), juce::jmax (0.25, len));
            g.setColour (theme::text.withAlpha (0.7f));
            g.drawVerticalLine ((int) beatToX (beat), (float) grid.getY(), (float) grid.getBottom());
        }
    }

    // ruler
    auto ruler = juce::Rectangle<int> (grid.getX(), 36, grid.getWidth(), rulerH);
    g.setColour (theme::bg2);
    g.fillRect (ruler);
    g.setFont (theme::font (10.5f, true));
    for (int b = 0; b <= (int) std::ceil (len); ++b)
    {
        const float x = beatToX (b);
        g.setColour (b % 4 == 0 ? theme::text : theme::textFaint);
        g.drawVerticalLine ((int) x, (float) ruler.getY() + (b % 4 == 0 ? 2.0f : 12.0f), (float) ruler.getBottom());
        if (b % 4 == 0)
            g.drawText (juce::String (b / 4 + 1), (int) x + 3, ruler.getY(), 30, rulerH, juce::Justification::centredLeft);
    }

    // velocity lane
    auto vel = velocityArea();
    g.setColour (theme::bg1);
    g.fillRect (vel);
    g.setColour (theme::textFaint);
    g.setFont (theme::font (10.0f, true));
    g.drawText ("VEL", getLocalBounds().removeFromBottom (velH).withWidth (keyW).reduced (6), juce::Justification::topLeft);
    {
        juce::Graphics::ScopedSaveState s (g);
        g.reduceClipRegion (vel);
        const auto notes = currentNotes();
        for (auto& n : notes)
        {
            const float x = beatToX (n.start);
            const float h = (vel.getHeight() - 6) * n.velocity;
            g.setColour (ch != nullptr ? ch->colour : theme::make);
            g.fillRect (x, vel.getBottom() - h, 3.0f, h);
            g.fillEllipse (x - 2.0f, vel.getBottom() - h - 3.0f, 7.0f, 7.0f);
        }
    }

    if (ch == nullptr)
    {
        g.setColour (theme::textDim);
        g.drawText ("Select a channel to edit its notes", grid, juce::Justification::centred);
    }
}

void PianoRoll::mouseMove (const juce::MouseEvent& e)
{
    bool edge = false;
    const int idx = gridArea().contains (e.getPosition()) ? noteAt (e.position, edge) : -1;
    setMouseCursor (idx >= 0 ? (edge ? juce::MouseCursor::LeftRightResizeCursor : juce::MouseCursor::DraggingHandCursor)
                             : juce::MouseCursor::NormalCursor);
}

void PianoRoll::mouseDown (const juce::MouseEvent& e)
{
    grabKeyboardFocus();
    if (ctx.project.findChannel (ctx.selectedChannelId) == nullptr) return;

    if (keyboardArea().contains (e.getPosition()))
    {
        audition (yToPitch (e.position.y));
        return;
    }

    if (velocityArea().contains (e.getPosition()))
    {
        drag = Drag::Velocity;
        dragNotes = currentNotes();
        ctx.project.checkpoint();
        mouseDrag (e);
        return;
    }

    if (! gridArea().contains (e.getPosition())) return;

    auto notes = currentNotes();
    bool edge = false;
    const int idx = noteAt (e.position, edge);

    if (e.mods.isPopupMenu())
    {
        if (idx >= 0)
        {
            notes.erase (notes.begin() + idx);
            selected.clear();
            commit (notes);
        }
        return;
    }

    ctx.project.checkpoint();
    if (idx >= 0)
    {
        dragIndex = idx;
        dragNotes = notes;
        if (std::find (selected.begin(), selected.end(), idx) == selected.end())
            selected = { idx };
        drag = edge ? Drag::Resize : Drag::Move;
        dragBeatOffset = xToBeat (e.position.x) - notes[(size_t) idx].start;
        dragPitchOffset = 0;
        audition (notes[(size_t) idx].pitch);
    }
    else
    {
        const double start = juce::jmax (0.0, std::floor (xToBeat (e.position.x) / snapBeats()) * snapBeats());
        if (start >= patternBeats()) return;
        const int pitch = yToPitch (e.position.y);
        dragNotes = notes;
        selected.clear();
        for (int p : chordFor (pitch))
        {
            dragNotes.push_back ({ start, lastLength, juce::jlimit (0, 127, p), 0.8f });
            selected.push_back ((int) dragNotes.size() - 1);
        }
        dragIndex = (int) dragNotes.size() - (int) selected.size();
        drag = Drag::Create;
        ctx.project.setNotes (ctx.selectedPattern(), ctx.selectedChannelId, dragNotes);
        ctx.projectEdited();
        audition (pitch);
    }
    repaint();
}

void PianoRoll::mouseDrag (const juce::MouseEvent& e)
{
    if (drag == Drag::None) return;
    auto notes = dragNotes;
    if (drag == Drag::Velocity)
    {
        const auto vel = velocityArea();
        const double beat = xToBeat (e.position.x);
        const float v = juce::jlimit (0.05f, 1.0f, (vel.getBottom() - e.position.y) / (float) (vel.getHeight() - 6));
        for (auto& n : dragNotes)
            if (std::abs (n.start - beat) < 6.0 / pxPerBeat)
                n.velocity = v;
        ctx.project.setNotes (ctx.selectedPattern(), ctx.selectedChannelId, dragNotes);
        ctx.projectEdited();
        repaint();
        return;
    }
    if (dragIndex < 0 || dragIndex >= (int) notes.size()) return;

    if (drag == Drag::Create || drag == Drag::Resize)
    {
        for (int i : selected)
        {
            if (i < 0 || i >= (int) notes.size()) continue;
            const double end = juce::jmax (notes[(size_t) i].start + snapBeats(), snap (xToBeat (e.position.x)));
            notes[(size_t) i].length = juce::jmin (end, patternBeats()) - notes[(size_t) i].start;
            lastLength = notes[(size_t) i].length;
        }
    }
    else if (drag == Drag::Move)
    {
        const auto& anchor = dragNotes[(size_t) dragIndex];
        const double newStart = juce::jmax (0.0, snap (xToBeat (e.position.x) - dragBeatOffset));
        const double dBeat = newStart - anchor.start;
        const int dPitch = yToPitch (e.position.y) - anchor.pitch;
        for (int i : selected)
        {
            if (i < 0 || i >= (int) notes.size()) continue;
            notes[(size_t) i].start = juce::jlimit (0.0, patternBeats() - notes[(size_t) i].length, dragNotes[(size_t) i].start + dBeat);
            notes[(size_t) i].pitch = juce::jlimit (0, 127, dragNotes[(size_t) i].pitch + dPitch);
        }
        if (dPitch != dragPitchOffset)
        {
            dragPitchOffset = dPitch;
            audition (notes[(size_t) dragIndex].pitch);
        }
    }
    ctx.project.setNotes (ctx.selectedPattern(), ctx.selectedChannelId, notes);
    ctx.projectEdited();
    repaint();
}

void PianoRoll::mouseUp (const juce::MouseEvent&)
{
    if (drag == Drag::Create)
        selected.clear();
    drag = Drag::None;
    audition (-1);
}

void PianoRoll::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    if (e.mods.isCtrlDown() || e.mods.isCommandDown())
        zoom.setValue (zoom.getValue() * (w.deltaY > 0 ? 1.15 : 0.87));
    else
        vscroll.setCurrentRangeStart (vscroll.getCurrentRangeStart() - w.deltaY * 200.0);
}

bool PianoRoll::keyPressed (const juce::KeyPress& k)
{
    if (k == juce::KeyPress::deleteKey || k == juce::KeyPress::backspaceKey)
    {
        auto notes = currentNotes();
        std::sort (selected.rbegin(), selected.rend());
        for (int i : selected)
            if (i >= 0 && i < (int) notes.size()) notes.erase (notes.begin() + i);
        selected.clear();
        commit (notes);
        return true;
    }
    if (k == juce::KeyPress ('a', juce::ModifierKeys::commandModifier, 0))
    {
        selected.clear();
        for (int i = 0; i < (int) currentNotes().size(); ++i) selected.push_back (i);
        repaint();
        return true;
    }
    return false;
}

} // namespace merge
