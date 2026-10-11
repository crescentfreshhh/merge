#include "PadGrid.h"
#include "core/Paths.h"
#include "engine/Instruments.h"

namespace merge
{

static const char* padKeys = "ZXCVASDFQWER1234"; // pad 1 = Z (bottom-left)

juce::String PadGrid::keyForPad (int pad)
{
    return pad >= 0 && pad < kNumPads ? juce::String::charToString ((juce::juce_wchar) padKeys[pad]) : juce::String();
}

int PadGrid::padForKey (juce::juce_wchar key)
{
    key = juce::CharacterFunctions::toUpperCase (key);
    for (int i = 0; i < kNumPads; ++i)
        if (padKeys[i] == key) return i;
    return -1;
}

PadGrid::PadGrid (AppContext& c, bool comp) : ctx (c), compact (comp)
{
    ctx.selectionChanged.addChangeListener (this);
    ctx.addChangeListener (this);
    startTimerHz (40);
}

PadGrid::~PadGrid()
{
    ctx.selectionChanged.removeChangeListener (this);
    ctx.removeChangeListener (this);
}

juce::Rectangle<int> PadGrid::padBounds (int pad) const
{
    const int gap = compact ? 4 : 6;
    const int size = juce::jmin ((getWidth() - gap * 3) / 4, (getHeight() - gap * 3) / 4);
    const int col = pad % 4, row = 3 - pad / 4;
    const int ox = (getWidth() - (size * 4 + gap * 3)) / 2;
    const int oy = (getHeight() - (size * 4 + gap * 3)) / 2;
    return { ox + col * (size + gap), oy + row * (size + gap), size, size };
}

int PadGrid::padAt (juce::Point<int> p) const
{
    for (int i = 0; i < kNumPads; ++i)
        if (padBounds (i).contains (p)) return i;
    return -1;
}

void PadGrid::timerCallback()
{
    auto& chans = ctx.project.getChannels();
    bool changed = false;
    for (int i = 0; i < kNumPads && i < (int) chans.size(); ++i)
    {
        const auto t = chans[(size_t) i].strip->triggerCount.load();
        if (t != lastTrigger[(size_t) i])
        {
            lastTrigger[(size_t) i] = t;
            flash[(size_t) i] = 1.0f;
            changed = true;
        }
        if (flash[(size_t) i] > 0.0f)
        {
            flash[(size_t) i] = juce::jmax (0.0f, flash[(size_t) i] - 0.08f);
            changed = true;
        }
    }
    if (changed) repaint();
}

void PadGrid::paint (juce::Graphics& g)
{
    auto& chans = ctx.project.getChannels();
    for (int i = 0; i < kNumPads; ++i)
    {
        auto r = padBounds (i).toFloat();
        const bool exists = i < (int) chans.size();
        const auto* ch = exists ? &chans[(size_t) i] : nullptr;
        const bool hasSample = ch != nullptr && ch->strip->instrument != nullptr && ch->strip->instrument->getSample() != nullptr;
        const auto colour = ch != nullptr ? ch->colour : theme::bg3;
        const float f = flash[(size_t) i];

        g.setColour (theme::bg2.interpolatedWith (colour, hasSample ? 0.12f + 0.75f * f : 0.03f));
        g.fillRoundedRectangle (r, 6.0f);
        if (f > 0.0f)
        {
            g.setColour (colour.withAlpha (0.35f * f));
            g.drawRoundedRectangle (r.expanded (1.5f), 7.0f, 3.0f);
        }
        g.setColour (hasSample ? colour.withAlpha (0.9f) : theme::line);
        g.fillRoundedRectangle (r.removeFromBottom (3.0f).reduced (6.0f, 0.0f), 1.5f);
        r = padBounds (i).toFloat();

        if (ch != nullptr && ch->id == ctx.selectedChannelId)
        {
            g.setColour (theme::text);
            g.drawRoundedRectangle (r.reduced (1.0f), 6.0f, 1.5f);
        }

        auto inner = padBounds (i).reduced (compact ? 4 : 7);
        g.setColour (theme::textFaint);
        g.setFont (theme::font (compact ? 9.5f : 11.0f, true));
        g.drawText (juce::String (i + 1), inner, juce::Justification::topLeft);
        g.drawText (keyForPad (i), inner, juce::Justification::topRight);
        if (ch != nullptr)
        {
            g.setColour (hasSample ? theme::text : theme::textFaint);
            g.setFont (theme::font (compact ? 10.5f : 12.5f, true));
            g.drawFittedText (hasSample ? ch->name : juce::String ("empty"), inner.removeFromBottom (compact ? 16 : 22),
                              juce::Justification::bottomLeft, 1, 0.8f);
        }
    }
    if (dragActive)
    {
        g.setColour (theme::make.withAlpha (0.6f));
        g.drawRect (getLocalBounds(), 2);
    }
}

void PadGrid::mouseDown (const juce::MouseEvent& e)
{
    const int pad = padAt (e.getPosition());
    if (pad < 0 || pad >= (int) ctx.project.getChannels().size()) return;
    const auto& ch = ctx.project.getChannels()[(size_t) pad];
    ctx.selectChannel (ch.id);
    if (e.mods.isPopupMenu())
    {
        showPadMenu (pad);
        return;
    }
    // vertical position sets velocity: top of the pad = hardest
    const auto r = padBounds (pad);
    const float vel = juce::jlimit (0.35f, 1.0f, 1.0f - (e.position.y - (float) r.getY()) / (float) r.getHeight() * 0.6f);
    pressedPad = pad;
    ctx.engine.beat().noteOn (pad, -1, vel);
}

void PadGrid::mouseUp (const juce::MouseEvent&)
{
    if (pressedPad >= 0)
        ctx.engine.beat().noteOff (pressedPad, -1);
    pressedPad = -1;
}

void PadGrid::loadFileOntoPad (int pad, const juce::File& file)
{
    auto& chans = ctx.project.getChannels();
    if (pad < 0 || pad >= (int) chans.size() || ! file.existsAsFile()) return;
    auto sample = SampleCache::get().load (file);
    if (sample == nullptr)
    {
        ctx.setStatus ("Couldn't read " + file.getFileName());
        return;
    }
    auto& ch = chans[(size_t) pad];
    ctx.project.setChannelSample (ch.id, sample, paths::toRef (file), ch.stepNote > 0 ? ch.stepNote : 60);
    ch.name = file.getFileNameWithoutExtension();
    if (ch.name.length() > 3 && ch.name.substring (0, 2).containsOnly ("0123456789") && ch.name[2] == ' ')
        ch.name = ch.name.substring (3);
    ctx.selectChannel (ch.id);
    ctx.projectEdited();
    ctx.setStatus ("Loaded " + file.getFileName() + " onto pad " + juce::String (pad + 1));
}

void PadGrid::filesDropped (const juce::StringArray& files, int x, int y)
{
    dragActive = false;
    int pad = padAt ({ x, y });
    for (auto& f : files)
    {
        if (pad < 0 || pad >= kNumPads) break;
        loadFileOntoPad (pad++, juce::File (f));
    }
    repaint();
}

void PadGrid::itemDropped (const SourceDetails& d)
{
    dragActive = false;
    if (auto f = fileFromDrag (d); f.existsAsFile())
        loadFileOntoPad (padAt (d.localPosition), f);
    repaint();
}

void PadGrid::showPadMenu (int pad)
{
    auto& chans = ctx.project.getChannels();
    if (pad >= (int) chans.size()) return;
    auto& ch = chans[(size_t) pad];
    auto* sampler = dynamic_cast<SamplerInstrument*> (ch.strip->instrument.get());

    juce::PopupMenu m;
    m.addSectionHeader ("Pad " + juce::String (pad + 1) + " - " + ch.name);
    m.addItem (1, "Load sample...");
    m.addItem (2, "Clear pad", sampler != nullptr && sampler->getSample() != nullptr);
    juce::PopupMenu choke;
    for (int g = 0; g <= 4; ++g)
        choke.addItem (10 + g, g == 0 ? juce::String ("None") : "Group " + juce::String (g), true, ch.strip->chokeGroup.load() == g);
    m.addSubMenu ("Choke group", choke);
    if (sampler != nullptr)
    {
        m.addItem (20, "Reverse", true, sampler->reverse.getBool());
        m.addItem (21, "Gate (stop on release)", true, sampler->gate.getBool());
    }
    m.addItem (30, "Clear steps in this pattern");

    const int channelId = ch.id;
    showMenu (m, this, [this, pad, channelId] (int r)
    {
        auto* chp = ctx.project.findChannel (channelId);
        if (chp == nullptr) return;
        auto& ch = *chp;
        auto* sampler = dynamic_cast<SamplerInstrument*> (ch.strip->instrument.get());
        if (r == 1)
        {
            auto chooser = std::make_shared<juce::FileChooser> ("Load sample", paths::userSamplesDir(), "*.wav;*.aif;*.aiff;*.flac;*.ogg;*.mp3");
            chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                                  [this, pad, chooser] (const juce::FileChooser& fc)
                                  {
                                      if (fc.getResult().existsAsFile())
                                          loadFileOntoPad (pad, fc.getResult());
                                  });
        }
        else if (r == 2)
        {
            ctx.project.setChannelSample (ch.id, nullptr, {}, 60);
            ch.name = "Pad " + juce::String (pad + 1);
            ctx.projectEdited();
        }
        else if (r >= 10 && r <= 14)
        {
            ch.strip->chokeGroup.store (r - 10);
        }
        else if (r == 20 && sampler != nullptr)
        {
            sampler->reverse.set (sampler->reverse.getBool() ? 0.0f : 1.0f);
        }
        else if (r == 21 && sampler != nullptr)
        {
            sampler->gate.set (sampler->gate.getBool() ? 0.0f : 1.0f);
            ctx.projectEdited();
        }
        else if (r == 30)
        {
            ctx.project.checkpoint();
            auto& p = ctx.project.pattern (ctx.selectedPattern());
            p.steps.erase (ch.id);
            p.notes.erase (ch.id);
            ctx.projectEdited();
        }
        repaint();
    });
}

} // namespace merge
