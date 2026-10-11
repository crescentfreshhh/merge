#pragma once

#include "ui/common/Widgets.h"
#include "app/AppContext.h"

namespace merge
{

class MixView : public juce::Component
{
public:
    explicit MixView (AppContext& c) : ctx (c) {}
    void paint (juce::Graphics& g) override
    {
        g.fillAll (theme::bg0);
        g.setColour (theme::textDim);
        g.drawText ("Mix mode", getLocalBounds(), juce::Justification::centred);
    }
private:
    AppContext& ctx;
};

} // namespace merge
