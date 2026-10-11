#pragma once

#include <juce_core/juce_core.h>
#include <atomic>
#include <functional>
#include <memory>
#include <vector>

namespace merge
{

/** A realtime-safe parameter: the UI writes it, the audio thread reads it. */
struct Param
{
    juce::String id, name;
    float minValue = 0.0f, maxValue = 1.0f, defaultValue = 0.0f;
    juce::String unit;
    juce::StringArray choices; // non-empty => discrete choice parameter
    float skewCentre = 0.0f;   // non-zero => log-ish slider centred here
    std::atomic<float> value { 0.0f };

    float get() const noexcept { return value.load (std::memory_order_relaxed); }
    int getChoice() const noexcept { return (int) std::lround (get()); }
    bool getBool() const noexcept { return get() >= 0.5f; }
    void set (float v) noexcept { value.store (juce::jlimit (minValue, maxValue, v), std::memory_order_relaxed); }

    juce::String toText() const
    {
        if (! choices.isEmpty())
            return choices[juce::jlimit (0, choices.size() - 1, getChoice())];
        const auto v = get();
        return juce::String (v, std::abs (v) >= 100.0f ? 0 : (std::abs (v) >= 10.0f ? 1 : 2)) + unit;
    }
};

/** Owns a list of Params and (de)serialises them. Used by instruments and effects. */
class ParamSet
{
public:
    Param& add (const juce::String& id, const juce::String& name, float minV, float maxV, float def,
                const juce::String& unit = {}, float skewCentre = 0.0f)
    {
        auto p = std::make_unique<Param>();
        p->id = id;
        p->name = name;
        p->minValue = minV;
        p->maxValue = maxV;
        p->defaultValue = def;
        p->unit = unit;
        p->skewCentre = skewCentre;
        p->value.store (def);
        params.push_back (std::move (p));
        return *params.back();
    }

    Param& addChoice (const juce::String& id, const juce::String& name, const juce::StringArray& choices, int def)
    {
        auto& p = add (id, name, 0.0f, (float) juce::jmax (1, choices.size() - 1), (float) def);
        p.choices = choices;
        return p;
    }

    Param& addToggle (const juce::String& id, const juce::String& name, bool def)
    {
        return addChoice (id, name, { "Off", "On" }, def ? 1 : 0);
    }

    Param* find (const juce::String& id) const
    {
        for (auto& p : params)
            if (p->id == id)
                return p.get();
        return nullptr;
    }

    const std::vector<std::unique_ptr<Param>>& all() const noexcept { return params; }

    juce::var toVar() const
    {
        auto* obj = new juce::DynamicObject();
        for (auto& p : params)
            obj->setProperty (p->id, p->get());
        return juce::var (obj);
    }

    void fromVar (const juce::var& v)
    {
        if (auto* obj = v.getDynamicObject())
            for (auto& p : params)
                if (obj->hasProperty (p->id))
                    p->set ((float) obj->getProperty (p->id));
    }

    void copyFrom (const ParamSet& other)
    {
        for (auto& p : params)
            if (auto* o = other.find (p->id))
                p->set (o->get());
    }

private:
    std::vector<std::unique_ptr<Param>> params;
};

} // namespace merge
