#pragma once

#include <juce_events/juce_events.h>
#include "library/LibraryDb.h"

namespace merge
{

class AppContext;

/** Plays the Automix Queue crate hands-free: beat-matched transitions with a crossfade
    and a bass swap halfway through (djay-style). Uses decks A and B. */
class Automix : public juce::ChangeBroadcaster, private juce::Timer
{
public:
    explicit Automix (AppContext& ctx);
    ~Automix() override;

    void start();
    void stop();
    bool isRunning() const noexcept { return running; }
    void skip(); // start the transition now

    int transitionBeats = 16;
    juce::String getState() const { return state; }
    std::vector<juce::int64> queue() const;

private:
    void timerCallback() override;
    bool loadNext (int deck);
    void beginTransition();
    void finishTransition();

    AppContext& ctx;
    bool running = false;
    int current = 0;          // deck that is playing out
    bool transitioning = false;
    double transitionStartBeat = 0.0;
    bool nextLoaded = false;
    juce::int64 nextTrack = -1;
    juce::String state { "Stopped" };
};

} // namespace merge
