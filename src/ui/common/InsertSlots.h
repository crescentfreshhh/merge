#pragma once

#include "Widgets.h"
#include "app/AppContext.h"

namespace merge
{

/** Insert effect slots for a channel (or master when channelId < 0): pick, bypass, edit. */
class InsertSlots : public juce::Component
{
public:
    InsertSlots (AppContext& ctx, int channelId, bool compact = false);
    void setChannel (int id);
    void refresh();
    void resized() override;

    static void openEditor (Effect& fx, juce::Component* near);

private:
    void choose (int slot);
    AppContext& ctx;
    int channelId;
    bool compact;
    struct Slot
    {
        std::unique_ptr<juce::TextButton> name, power, edit;
    };
    std::array<Slot, kNumInserts> slots;
};

} // namespace merge
