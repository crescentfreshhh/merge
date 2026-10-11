#include "Automix.h"
#include "AppContext.h"
#include "DjController.h"
#include "LibraryService.h"

namespace merge
{

Automix::Automix (AppContext& c) : ctx (c) {}

Automix::~Automix() { stopTimer(); }

std::vector<juce::int64> Automix::queue() const
{
    return ctx.library->db().crateTracks (ctx.library->automixCrate());
}

bool Automix::loadNext (int deck)
{
    auto q = queue();
    // skip tracks already loaded on the other deck
    for (auto id : q)
    {
        if (ctx.decks->state (1 - deck).info.id == id && ctx.decks->state (1 - deck).track != nullptr) continue;
        ctx.decks->loadTrack (deck, id);
        nextTrack = id;
        return true;
    }
    return false;
}

void Automix::start()
{
    if (queue().empty())
    {
        ctx.setStatus ("Automix: add tracks to the Automix Queue first (right-click a track > Add to Automix queue).");
        return;
    }
    running = true;
    transitioning = false;
    ctx.dj.channel (0).xfAssign.store (0);
    ctx.dj.channel (1).xfAssign.store (2);
    // if nothing is playing, start the first queued track on deck A
    if (! ctx.dj.deck (0).playing.load() && ! ctx.dj.deck (1).playing.load())
    {
        current = 0;
        ctx.dj.crossfader.store (-1.0f);
        loadNext (0);
        nextLoaded = false;
        state = "Starting...";
    }
    else
    {
        current = ctx.dj.deck (0).playing.load() ? 0 : 1;
        ctx.dj.crossfader.store (current == 0 ? -1.0f : 1.0f);
        nextLoaded = false;
        state = "Playing";
    }
    startTimerHz (20);
    sendChangeMessage();
}

void Automix::stop()
{
    running = false;
    transitioning = false;
    stopTimer();
    state = "Stopped";
    sendChangeMessage();
}

void Automix::skip()
{
    if (running && ! transitioning && nextLoaded)
        beginTransition();
}

void Automix::beginTransition()
{
    const int next = 1 - current;
    auto& dn = ctx.dj.deck (next);
    dn.sync.store (true);
    ctx.dj.channel (next).eqLow.store (0.0f); // incoming bass off until the swap
    ctx.dj.channel (next).fader.store (1.0f);
    ctx.decks->play (next);
    transitioning = true;
    transitionStartBeat = ctx.dj.deck (current).audibleBeat();
    state = "Mixing into " + ctx.decks->state (next).info.title;
    sendChangeMessage();
}

void Automix::finishTransition()
{
    const int old = current;
    current = 1 - current;
    ctx.decks->pause (old);
    ctx.dj.channel (old).eqLow.store (0.5f);
    ctx.dj.channel (current).eqLow.store (0.5f);
    ctx.dj.crossfader.store (current == 0 ? -1.0f : 1.0f);
    ctx.dj.deck (current).sync.store (false); // the new track runs at its own tempo again
    // played tracks leave the queue
    ctx.library->db().removeFromCrate (ctx.library->automixCrate(), ctx.decks->state (old).info.id);
    ctx.library->refresh();
    transitioning = false;
    nextLoaded = false;
    state = "Playing " + ctx.decks->state (current).info.title;
    sendChangeMessage();
}

void Automix::timerCallback()
{
    if (! running) return;
    auto& cur = ctx.dj.deck (current);
    auto& cs = ctx.decks->state (current);

    // first track: wait for it to load, then play
    if (cs.track == nullptr || cs.loading) return;
    if (! cur.playing.load() && ! transitioning)
    {
        ctx.decks->play (current);
        return;
    }

    const int next = 1 - current;
    if (! nextLoaded && ! ctx.decks->state (next).loading)
    {
        if (ctx.decks->state (next).track == nullptr || ctx.decks->state (next).info.id != nextTrack || ctx.dj.deck (next).playing.load())
        {
            if (! loadNext (next))
            {
                state = "Last track in the queue";
                return;
            }
        }
        nextLoaded = ctx.decks->state (next).track != nullptr && ctx.decks->state (next).info.id == nextTrack;
    }

    const double bpm = juce::jmax (60.0, cur.effectiveBpm.load() > 0 ? cur.effectiveBpm.load() : 120.0);
    const double transitionSec = transitionBeats * 60.0 / bpm;
    const double remaining = cs.track->lengthSeconds() - cur.positionSec.load();

    if (! transitioning)
    {
        if (nextLoaded && remaining < transitionSec + 4.0)
            beginTransition();
        return;
    }

    // transition progress in beats of the outgoing track
    const double progress = juce::jlimit (0.0, 1.0, (cur.audibleBeat() - transitionStartBeat) / transitionBeats);
    const float x = (float) (current == 0 ? -1.0 + 2.0 * progress : 1.0 - 2.0 * progress);
    ctx.dj.crossfader.store (x);
    // bass swap at the halfway point
    const bool swapped = progress >= 0.5;
    ctx.dj.channel (current).eqLow.store (swapped ? 0.0f : 0.5f);
    ctx.dj.channel (next).eqLow.store (swapped ? 0.5f : 0.0f);
    if (progress >= 1.0 || ! cur.playing.load())
        finishTransition();
}

} // namespace merge
