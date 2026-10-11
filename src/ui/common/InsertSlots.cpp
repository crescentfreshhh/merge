#include "InsertSlots.h"
#include "PluginWindow.h"
#include "engine/PluginHost.h"

namespace merge
{

InsertSlots::InsertSlots (AppContext& c, int id, bool comp) : ctx (c), channelId (id), compact (comp)
{
    for (int i = 0; i < kNumInserts; ++i)
    {
        auto& s = slots[(size_t) i];
        s.name = std::make_unique<juce::TextButton>();
        s.power = std::make_unique<PillButton> ("", theme::good);
        s.edit = std::make_unique<juce::TextButton> ("...");
        s.name->onClick = [this, i] { choose (i); };
        s.power->setTooltip ("Bypass");
        s.power->onClick = [this, i]
        {
            if (auto fx = ctx.project.getInsert (channelId, i))
                fx->bypassed.store (! slots[(size_t) i].power->getToggleState());
        };
        s.edit->setTooltip ("Edit effect");
        s.edit->onClick = [this, i]
        {
            if (auto fx = ctx.project.getInsert (channelId, i))
                openEditor (*fx, slots[(size_t) i].edit.get());
        };
        addAndMakeVisible (*s.name);
        addAndMakeVisible (*s.power);
        addAndMakeVisible (*s.edit);
    }
    refresh();
}

void InsertSlots::setChannel (int id)
{
    channelId = id;
    refresh();
}

void InsertSlots::refresh()
{
    for (int i = 0; i < kNumInserts; ++i)
    {
        auto& s = slots[(size_t) i];
        auto fx = ctx.project.getInsert (channelId, i);
        s.name->setButtonText (fx != nullptr ? fx->getName() : juce::String ("+ FX ") + juce::String (i + 1));
        s.name->setColour (juce::TextButton::textColourOffId, fx != nullptr ? theme::text : theme::textFaint);
        s.power->setVisible (fx != nullptr);
        s.edit->setVisible (fx != nullptr);
        if (fx != nullptr)
            s.power->setToggleState (! fx->bypassed.load(), juce::dontSendNotification);
    }
}

void InsertSlots::resized()
{
    auto r = getLocalBounds();
    const int h = juce::jmin (compact ? 22 : 26, r.getHeight() / kNumInserts - 3);
    for (auto& s : slots)
    {
        auto row = r.removeFromTop (h);
        r.removeFromTop (3);
        s.power->setBounds (row.removeFromLeft (h).reduced (3));
        s.edit->setBounds (row.removeFromRight (compact ? 22 : 28));
        s.name->setBounds (row.reduced (2, 0));
    }
}

void InsertSlots::openEditor (Effect& fx, juce::Component* near)
{
    if (auto* pe = dynamic_cast<PluginEffect*> (&fx))
    {
        if (auto* p = pe->getPlugin())
            PluginWindow::show (*p);
        return;
    }
    auto panel = std::make_unique<ParamPanel> (fx.params, theme::make, 60);
    const int w = juce::jmax (200, panel->getIdealWidth (6));
    panel->setSize (w, panel->getIdealHeight (w));
    juce::CallOutBox::launchAsynchronously (std::move (panel), near->getScreenBounds(), nullptr);
}

void InsertSlots::choose (int slot)
{
    juce::PopupMenu m;
    m.addItem (1, "None");
    m.addSeparator();
    const auto types = builtInEffectTypes();
    for (int i = 0; i < types.size(); ++i)
        m.addItem (100 + i, builtInEffectName (types[i]));
    const auto plugs = ctx.plugins.getEffects();
    if (! plugs.empty())
    {
        juce::PopupMenu pm;
        for (size_t i = 0; i < plugs.size(); ++i)
            pm.addItem (1000 + (int) i, plugs[i].name + " (" + plugs[i].manufacturerName + ")");
        m.addSubMenu ("Plugins", pm);
    }
    else
    {
        m.addItem (2, "Scan for VST3 plugins...");
    }
    showMenu (m, slots[(size_t) slot].name.get(), [this, slot, types, plugs] (int r)
    {
        Effect::Ptr fx;
        if (r == 2)
        {
            ctx.plugins.startScan();
            ctx.setStatus ("Scanning VST3 plugins in the background...");
            return;
        }
        if (r >= 100 && r < 1000)
            fx = createBuiltInEffect (types[r - 100]);
        else if (r >= 1000)
        {
            juce::String err;
            fx = ctx.plugins.wrapEffect (ctx.plugins.createInstance (plugs[(size_t) (r - 1000)], ctx.engine.getSampleRate(),
                                                                       ctx.engine.getBlockSize(), err));
            if (fx == nullptr)
            {
                ctx.setStatus ("Couldn't load plugin: " + err);
                return;
            }
        }
        if (auto old = ctx.project.getInsert (channelId, slot))
            if (auto* pe = dynamic_cast<PluginEffect*> (old.get()))
                if (auto* p = pe->getPlugin())
                    PluginWindow::closeAllFor (*p);
        ctx.project.setInsert (channelId, slot, fx);
        ctx.projectEdited();
        refresh();
        if (fx != nullptr)
            openEditor (*fx, slots[(size_t) slot].edit.get());
    });
}

} // namespace merge
