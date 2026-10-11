#pragma once

#include "DeckWidgets.h"

namespace merge
{

class AutomixPanel : public juce::Component
{
public:
    explicit AutomixPanel (AppContext& c) : ctx (c) {}
    void paint (juce::Graphics& g) override
    {
        g.setColour (theme::bg1);
        g.fillRoundedRectangle (getLocalBounds().toFloat(), 6.0f);
    }

private:
    AppContext& ctx;
};

} // namespace merge
