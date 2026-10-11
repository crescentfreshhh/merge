#include "StepGrid.h"
#include "core/Paths.h"

namespace merge
{

StepGrid::StepGrid (AppContext& c) : ctx (c)
{
    ctx.selectionChanged.addChangeListener (this);
    ctx.addChangeListener (this);
    startTimerHz (30);
}

StepGrid::~StepGrid()
{
    ctx.selectionChanged.removeChangeListener (this);
    ctx.removeChangeListener (this);
}

void StepGrid::changeListenerCallback (juce::ChangeBroadcaster*)
{
    const int h = getIdealHeight();
    if (getHeight() != h)
        setSize (getWidth(), h);
    repaint();
}

int StepGrid::getIdealHeight() const
{
    return (int) ctx.project.getChannels().size() * rowHeight + 4;
}

int StepGrid::numSteps() const
{
    return ctx.project.getPatterns()[(size_t) ctx.selectedPattern()].lengthSteps;
}

juce::Rectangle<int> StepGrid::stepArea (int row) const
{
    return rowBounds (row).withTrimmedLeft (headerWidth).reduced (2, 3);
}

juce::Rectangle<int> StepGrid::stepBounds (int row, int step) const
{
    const auto area = stepArea (row);
    const int n = numSteps();
    const float w = (float) area.getWidth() / (float) n;
    const int x0 = area.getX() + (int) std::round (step * w);
    const int x1 = area.getX() + (int) std::round ((step + 1) * w);
    return { x0 + 1, area.getY(), juce::jmax (2, x1 - x0 - 2), area.getHeight() };
}

bool StepGrid::rowUsesNotes (int row) const
{
    const auto& chans = ctx.project.getChannels();
    if (row < 0 || row >= (int) chans.size()) return false;
    const auto* notes = ctx.project.getNotes (ctx.selectedPattern(), chans[(size_t) row].id);
    return notes != nullptr && ! notes->empty();
}

StepGrid::HitInfo StepGrid::hitTest (juce::Point<int> p) const
{
    HitInfo h;
    h.row = p.y / rowHeight;
    if (h.row < 0 || h.row >= (int) ctx.project.getChannels().size())
        return {};
    const int x = p.x;
    if (x < 8) h.what = Hit::Name;
    else if (x < 30) h.what = Hit::Mute;
    else if (x < 52) h.what = Hit::Solo;
    else if (x < 160) h.what = Hit::Name;
    else if (x < 205) h.what = Hit::Volume;
    else if (x < headerWidth - 4) h.what = Hit::Pan;
    else
    {
        h.what = Hit::Step;
        const auto area = stepArea (h.row);
        h.step = juce::jlimit (0, numSteps() - 1, (int) ((x - area.getX()) * numSteps() / juce::jmax (1, area.getWidth())));
    }
    return h;
}

void StepGrid::timerCallback()
{
    const auto& t = ctx.engine.beat().transport;
    int step = -1;
    if (t.playing.load() && ! t.songMode.load())
        step = (int) std::floor (t.positionBeats.load() / kStepBeats);
    if (step != lastPlayStep)
    {
        lastPlayStep = step;
        repaint();
    }
}

void StepGrid::paint (juce::Graphics& g)
{
    auto& chans = ctx.project.getChannels();
    const int pat = ctx.selectedPattern();
    const int n = numSteps();

    for (int row = 0; row < (int) chans.size(); ++row)
    {
        const auto& ch = chans[(size_t) row];
        auto r = rowBounds (row);
        const bool selected = ch.id == ctx.selectedChannelId;
        g.setColour (selected ? theme::bg3 : (row % 2 ? theme::bg1 : theme::bg1.brighter (0.02f)));
        g.fillRect (r.withWidth (headerWidth));

        g.setColour (ch.colour);
        g.fillRect (r.getX(), r.getY() + 3, 4, rowHeight - 6);

        // mute / solo
        auto box = [&g] (juce::Rectangle<int> b, const juce::String& t, bool on, juce::Colour c)
        {
            g.setColour (on ? c : theme::bg2);
            g.fillRoundedRectangle (b.toFloat(), 3.0f);
            g.setColour (on ? juce::Colours::black : theme::textDim);
            g.setFont (theme::font (10.5f, true));
            g.drawText (t, b, juce::Justification::centred);
        };
        box ({ 9, r.getY() + 6, 19, rowHeight - 12 }, "M", ch.strip->mute.load(), theme::warn);
        box ({ 31, r.getY() + 6, 19, rowHeight - 12 }, "S", ch.strip->solo.load(), theme::good);

        // name
        g.setColour (theme::text);
        g.setFont (theme::font (12.5f, selected));
        juce::String label = ch.name;
        if (row < kNumPads) label = juce::String (row + 1) + "  " + label;
        g.drawFittedText (label, { 56, r.getY(), 102, rowHeight }, juce::Justification::centredLeft, 1, 0.85f);

        // volume bar
        auto vol = juce::Rectangle<int> (162, r.getY() + 11, 40, 8);
        g.setColour (theme::bg0);
        g.fillRoundedRectangle (vol.toFloat(), 3.0f);
        g.setColour (ch.colour.withAlpha (0.85f));
        g.fillRoundedRectangle (vol.withWidth ((int) (vol.getWidth() * ch.strip->volume.load())).toFloat(), 3.0f);

        // pan bar (bipolar)
        auto pan = juce::Rectangle<int> (208, r.getY() + 11, 36, 8);
        g.setColour (theme::bg0);
        g.fillRoundedRectangle (pan.toFloat(), 3.0f);
        const float pv = ch.strip->pan.load();
        const int cx = pan.getCentreX();
        const int px = cx + (int) (pv * pan.getWidth() * 0.5f);
        g.setColour (theme::textDim);
        g.fillRect (juce::jmin (cx, px), pan.getY(), juce::jmax (2, std::abs (px - cx)), pan.getHeight());

        // steps or notes
        const auto area = stepArea (row);
        if (rowUsesNotes (row))
        {
            g.setColour (theme::bg0);
            g.fillRoundedRectangle (area.toFloat(), 3.0f);
            const auto* notes = ctx.project.getNotes (pat, ch.id);
            int lo = 127, hi = 0;
            for (auto& nt : *notes) { lo = juce::jmin (lo, nt.pitch); hi = juce::jmax (hi, nt.pitch); }
            const double len = n * kStepBeats;
            for (auto& nt : *notes)
            {
                const float x0 = area.getX() + (float) (nt.start / len) * area.getWidth();
                const float w = juce::jmax (2.0f, (float) (nt.length / len) * area.getWidth());
                const float y = hi == lo ? area.getCentreY() - 2.0f
                                         : area.getBottom() - 4.0f - (float) (nt.pitch - lo) / (float) (hi - lo) * (area.getHeight() - 8.0f);
                g.setColour (ch.colour);
                g.fillRect (x0, y, w, 3.0f);
            }
            g.setColour (theme::textFaint);
            g.setFont (theme::font (10.0f));
            g.drawText ("piano roll - double-click to edit", area.reduced (6, 0), juce::Justification::centredRight);
        }
        else
        {
            for (int s = 0; s < n; ++s)
            {
                auto cell = stepBounds (row, s).toFloat();
                const int v = ctx.project.getStep (pat, ch.id, s);
                const bool beatGroup = (s / 4) % 2 == 0;
                g.setColour (beatGroup ? theme::bg2 : theme::bg2.brighter (0.06f));
                g.fillRoundedRectangle (cell, 3.0f);
                if (v > 0)
                {
                    const float vel = v / 127.0f;
                    g.setColour (ch.colour.withAlpha (0.35f + 0.65f * vel));
                    g.fillRoundedRectangle (cell, 3.0f);
                    if (v >= 120)
                    {
                        g.setColour (juce::Colours::white.withAlpha (0.8f));
                        g.fillRect (cell.withHeight (2.0f).reduced (3.0f, 0.0f).translated (0.0f, 2.0f));
                    }
                }
                if (s == lastPlayStep % juce::jmax (1, n) && lastPlayStep >= 0)
                {
                    g.setColour (juce::Colours::white.withAlpha (0.18f));
                    g.fillRoundedRectangle (cell, 3.0f);
                }
            }
        }
    }
}

void StepGrid::mouseDown (const juce::MouseEvent& e)
{
    dragHit = hitTest (e.getPosition());
    auto& chans = ctx.project.getChannels();
    if (dragHit.row < 0) return;
    auto& ch = chans[(size_t) dragHit.row];
    const int pat = ctx.selectedPattern();

    switch (dragHit.what)
    {
        case Hit::Name:
            ctx.selectChannel (ch.id);
            if (e.mods.isPopupMenu())
                showRowMenu (dragHit.row);
            else
                ctx.engine.beat().noteOn (dragHit.row, -1, 0.85f);
            break;
        case Hit::Mute:
            ch.strip->mute.store (! ch.strip->mute.load());
            break;
        case Hit::Solo:
            ch.strip->solo.store (! ch.strip->solo.load());
            break;
        case Hit::Volume:
            dragStartValue = ch.strip->volume.load();
            break;
        case Hit::Pan:
            if (e.getNumberOfClicks() > 1) ch.strip->pan.store (0.0f);
            dragStartValue = ch.strip->pan.load();
            break;
        case Hit::Step:
        {
            if (rowUsesNotes (dragHit.row))
            {
                ctx.selectChannel (ch.id);
                return;
            }
            ctx.project.checkpoint();
            const int current = ctx.project.getStep (pat, ch.id, dragHit.step);
            if (e.mods.isPopupMenu()) paintValue = 0;
            else if (e.mods.isShiftDown()) paintValue = current >= 120 ? 0 : 127;
            else paintValue = current > 0 ? 0 : 100;
            ctx.project.setStep (pat, ch.id, dragHit.step, paintValue);
            lastPaintedStep.assign ((size_t) chans.size(), -1);
            lastPaintedStep[(size_t) dragHit.row] = dragHit.step;
            ctx.projectEdited();
            if (paintValue > 0 && ! ctx.isPlaying())
                ctx.engine.beat().noteOn (dragHit.row, -1, paintValue / 127.0f);
            break;
        }
        default:
            break;
    }
    repaint();
}

void StepGrid::mouseDrag (const juce::MouseEvent& e)
{
    auto& chans = ctx.project.getChannels();
    if (dragHit.row < 0 || dragHit.row >= (int) chans.size()) return;
    auto& ch = chans[(size_t) dragHit.row];
    if (dragHit.what == Hit::Volume)
    {
        ch.strip->volume.store (juce::jlimit (0.0f, 1.0f, dragStartValue + e.getDistanceFromDragStartX() / 60.0f
                                                                            - e.getDistanceFromDragStartY() / 120.0f));
        repaint();
    }
    else if (dragHit.what == Hit::Pan)
    {
        ch.strip->pan.store (juce::jlimit (-1.0f, 1.0f, dragStartValue + e.getDistanceFromDragStartX() / 40.0f));
        repaint();
    }
    else if (dragHit.what == Hit::Step && paintValue >= 0)
    {
        auto h = hitTest (e.getPosition());
        if (h.what != Hit::Step || h.row < 0 || rowUsesNotes (h.row)) return;
        if ((int) lastPaintedStep.size() != (int) chans.size()) lastPaintedStep.assign (chans.size(), -1);
        if (lastPaintedStep[(size_t) h.row] == h.step) return;
        lastPaintedStep[(size_t) h.row] = h.step;
        ctx.project.setStep (ctx.selectedPattern(), chans[(size_t) h.row].id, h.step, paintValue);
        ctx.projectEdited();
        repaint();
    }
}

void StepGrid::mouseUp (const juce::MouseEvent&)
{
    if (dragHit.what == Hit::Name && dragHit.row >= 0)
        ctx.engine.beat().noteOff (dragHit.row, -1);
    if (dragHit.what == Hit::Volume || dragHit.what == Hit::Pan)
        ctx.projectEdited (false);
    paintValue = -1;
    dragHit = {};
}

void StepGrid::mouseDoubleClick (const juce::MouseEvent& e)
{
    auto h = hitTest (e.getPosition());
    if (h.row < 0) return;
    auto& ch = ctx.project.getChannels()[(size_t) h.row];
    if ((h.what == Hit::Step && rowUsesNotes (h.row)) || (h.what == Hit::Name && ch.prefersPianoRoll()))
    {
        if (onOpenPianoRoll) onOpenPianoRoll (ch.id);
    }
    else if (h.what == Hit::Name)
    {
        promptRename (ch.id);
    }
}

void StepGrid::promptRename (int id)
{
    auto* ch = ctx.project.findChannel (id);
    if (ch == nullptr) return;
    auto* aw = new juce::AlertWindow ("Rename channel", {}, juce::MessageBoxIconType::NoIcon);
    aw->addTextEditor ("name", ch->name);
    aw->addButton ("OK", 1, juce::KeyPress (juce::KeyPress::returnKey));
    aw->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
    aw->enterModalState (true, juce::ModalCallbackFunction::create ([this, aw, id] (int r)
    {
        if (r == 1)
            if (auto* c = ctx.project.findChannel (id))
            {
                c->name = aw->getTextEditorContents ("name");
                ctx.projectEdited (false);
            }
    }), true);
}

void StepGrid::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    auto h = hitTest (e.getPosition());
    if (h.what != Hit::Step || h.row < 0 || rowUsesNotes (h.row))
    {
        Component::mouseWheelMove (e, w);
        return;
    }
    auto& ch = ctx.project.getChannels()[(size_t) h.row];
    const int pat = ctx.selectedPattern();
    const int v = ctx.project.getStep (pat, ch.id, h.step);
    if (v == 0) return;
    ctx.project.setStep (pat, ch.id, h.step, juce::jlimit (1, 127, v + (w.deltaY > 0 ? 8 : -8)));
    ctx.projectEdited();
    repaint();
}

void StepGrid::showRowMenu (int row)
{
    auto& chans = ctx.project.getChannels();
    const int id = chans[(size_t) row].id;
    juce::PopupMenu m;
    m.addItem (1, "Rename...");
    m.addItem (2, "Open piano roll");
    m.addItem (3, "Fill every 2 steps");
    m.addItem (4, "Fill every 4 steps");
    m.addItem (5, "Clear");
    m.addSeparator();
    m.addItem (6, "Delete channel", row >= kNumPads);
    showMenu (m, this, [this, id] (int r)
    {
        auto* ch = ctx.project.findChannel (id);
        if (ch == nullptr) return;
        const int pat = ctx.selectedPattern();
        if (r == 2 && onOpenPianoRoll) onOpenPianoRoll (id);
        if (r == 3 || r == 4)
        {
            ctx.project.checkpoint();
            const int every = r == 3 ? 2 : 4;
            for (int s = 0; s < numSteps(); ++s)
                ctx.project.setStep (pat, id, s, s % every == 0 ? 100 : 0);
            ctx.projectEdited();
        }
        if (r == 5)
        {
            ctx.project.checkpoint();
            ctx.project.pattern (pat).steps.erase (id);
            ctx.project.pattern (pat).notes.erase (id);
            ctx.projectEdited();
        }
        if (r == 6)
        {
            ctx.project.removeChannel (id);
            if (ctx.selectedChannelId == id && ! ctx.project.getChannels().empty())
                ctx.selectChannel (ctx.project.getChannels().front().id);
            ctx.projectEdited();
        }
        if (r == 1)
            promptRename (id);
        repaint();
    });
}

void StepGrid::dropFileOnRow (int row, const juce::File& f)
{
    auto& chans = ctx.project.getChannels();
    if (row < 0 || row >= (int) chans.size() || ! f.existsAsFile()) return;
    auto& ch = chans[(size_t) row];
    if (ch.kind != ChannelKind::Sampler && ch.kind != ChannelKind::Keys) return;
    if (auto s = SampleCache::get().load (f))
    {
        ctx.project.setChannelSample (ch.id, s, paths::toRef (f), ch.kind == ChannelKind::Keys ? 60 : ch.stepNote);
        ch.name = f.getFileNameWithoutExtension();
        ctx.projectEdited();
    }
}

void StepGrid::itemDropped (const SourceDetails& d)
{
    if (auto f = fileFromDrag (d); f.existsAsFile())
        dropFileOnRow (d.localPosition.y / rowHeight, f);
}

void StepGrid::filesDropped (const juce::StringArray& files, int, int y)
{
    if (! files.isEmpty())
        dropFileOnRow (y / rowHeight, juce::File (files[0]));
}

} // namespace merge
