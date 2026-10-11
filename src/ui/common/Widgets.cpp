#include "Widgets.h"

namespace merge
{

//==============================================================================
Knob::Knob (const juce::String& cap, juce::Colour accent) : caption (cap)
{
    slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    slider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    slider.setRotaryParameters (juce::MathConstants<float>::pi * 1.25f, juce::MathConstants<float>::pi * 2.75f, true);
    slider.setColour (juce::Slider::rotarySliderFillColourId, accent);
    slider.setPopupDisplayEnabled (true, true, nullptr);
    slider.setDoubleClickReturnValue (true, 0.0);
    slider.onValueChange = [this]
    {
        if (param != nullptr)
            param->set ((float) slider.getValue());
        if (onChange)
            onChange (slider.getValue());
    };
    slider.textFromValueFunction = [this] (double v)
    {
        if (formatter) return formatter (v);
        if (param != nullptr)
        {
            if (! param->choices.isEmpty())
                return param->choices[juce::jlimit (0, param->choices.size() - 1, (int) std::lround (v))];
            return juce::String (v, std::abs (v) >= 100 ? 0 : (std::abs (v) >= 10 ? 1 : 2)) + param->unit;
        }
        return juce::String (v, 2);
    };
    addAndMakeVisible (slider);
}

void Knob::setRange (double min, double max, double def, double skewCentre)
{
    slider.setRange (min, max, 0.0);
    if (skewCentre > min && skewCentre < max)
        slider.setSkewFactorFromMidPoint (skewCentre);
    slider.setDoubleClickReturnValue (true, def);
    slider.setValue (def, juce::dontSendNotification);
}

void Knob::setAccent (juce::Colour c)
{
    slider.setColour (juce::Slider::rotarySliderFillColourId, c);
    repaint();
}

void Knob::bind (Param* p)
{
    param = p;
    if (p == nullptr) return;
    caption = p->name;
    slider.setRange (p->minValue, p->maxValue, p->choices.isEmpty() ? 0.0 : 1.0);
    if (p->skewCentre > p->minValue && p->skewCentre < p->maxValue)
        slider.setSkewFactorFromMidPoint (p->skewCentre);
    slider.setDoubleClickReturnValue (true, p->defaultValue);
    slider.setValue (p->get(), juce::dontSendNotification);
}

void Knob::refreshFromParam()
{
    if (param != nullptr && ! slider.isMouseButtonDown() && std::abs (slider.getValue() - param->get()) > 1.0e-6)
        slider.setValue (param->get(), juce::dontSendNotification);
}

void Knob::paint (juce::Graphics& g)
{
    if (caption.isEmpty()) return;
    g.setColour (theme::textDim);
    g.setFont (theme::font (11.0f));
    g.drawFittedText (caption, getLocalBounds().removeFromBottom (14), juce::Justification::centred, 1);
}

void Knob::resized()
{
    auto r = getLocalBounds();
    if (caption.isNotEmpty())
        r.removeFromBottom (14);
    slider.setBounds (r);
}

//==============================================================================
ValueBox::ValueBox (const juce::String& cap, double min, double max, double step, int dec)
    : caption (cap), minV (min), maxV (max), stepV (step), decimals (dec)
{
    setMouseCursor (juce::MouseCursor::UpDownResizeCursor);
    value = min;
}

void ValueBox::setValue (double v, bool notify)
{
    v = juce::jlimit (minV, maxV, std::round (v / stepV) * stepV);
    if (std::abs (v - value) < 1.0e-9) return;
    value = v;
    repaint();
    if (notify && onChange) onChange (value);
}

void ValueBox::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setColour (theme::bg2);
    g.fillRoundedRectangle (r, 4.0f);
    g.setColour (theme::line);
    g.drawRoundedRectangle (r.reduced (0.5f), 4.0f, 1.0f);
    auto area = getLocalBounds().reduced (6, 2);
    if (caption.isNotEmpty())
    {
        g.setColour (theme::textDim);
        g.setFont (theme::font (10.0f, true));
        g.drawText (caption.toUpperCase(), area.removeFromTop (12), juce::Justification::centredLeft);
    }
    g.setColour (accent);
    g.setFont (theme::font (juce::jmin (18.0f, area.getHeight() * 0.9f), true));
    g.drawText (juce::String (value, decimals) + suffix, area, juce::Justification::centredLeft);
}

void ValueBox::mouseDown (const juce::MouseEvent&) { dragStart = value; }

void ValueBox::mouseDrag (const juce::MouseEvent& e)
{
    const double sensitivity = e.mods.isShiftDown() ? 0.05 : 0.5;
    setValue (dragStart - e.getDistanceFromDragStartY() * sensitivity * (stepV < 1 ? stepV * 10 : stepV), true);
}

void ValueBox::mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& w)
{
    setValue (value + (w.deltaY > 0 ? stepV : -stepV) * (stepV < 1 ? 10 : 1), true);
}

void ValueBox::mouseDoubleClick (const juce::MouseEvent&)
{
    editor = std::make_unique<juce::TextEditor>();
    editor->setText (juce::String (value, decimals), false);
    editor->setJustification (juce::Justification::centredLeft);
    editor->setFont (theme::font (16.0f, true));
    editor->setBounds (getLocalBounds());
    editor->selectAll();
    auto commit = [this]
    {
        if (editor == nullptr) return;
        const auto t = editor->getText();
        juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<ValueBox> (this), t]
        {
            if (safe == nullptr) return;
            safe->setValue (t.getDoubleValue(), true);
            safe->editor.reset();
        });
    };
    editor->onReturnKey = commit;
    editor->onFocusLost = commit;
    editor->onEscapeKey = [this] { juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<ValueBox> (this)] { if (safe) safe->editor.reset(); }); };
    addAndMakeVisible (*editor);
    editor->grabKeyboardFocus();
}

//==============================================================================
LevelMeter::LevelMeter (std::atomic<float>* l, std::atomic<float>* r, bool v) : srcL (l), srcR (r), vertical (v)
{
    startTimerHz (30);
}

void LevelMeter::setSources (std::atomic<float>* l, std::atomic<float>* r)
{
    srcL = l;
    srcR = r;
}

void LevelMeter::timerCallback()
{
    auto take = [] (std::atomic<float>* a) { return a != nullptr ? a->exchange (0.0f) : 0.0f; };
    const float l = take (srcL), r = srcR != nullptr ? take (srcR) : l;
    levelL = juce::jmax (l, levelL * 0.82f);
    levelR = juce::jmax (r, levelR * 0.82f);
    holdL = juce::jmax (l, holdL * 0.97f);
    holdR = juce::jmax (r, holdR * 0.97f);
    repaint();
}

void LevelMeter::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setColour (theme::bg0);
    g.fillRoundedRectangle (r, 2.0f);
    auto toPos = [] (float level)
    {
        const float db = juce::Decibels::gainToDecibels (level, -60.0f);
        return juce::jlimit (0.0f, 1.0f, (db + 60.0f) / 63.0f);
    };
    auto drawBar = [&] (juce::Rectangle<float> bar, float level, float hold)
    {
        const float p = toPos (level);
        juce::ColourGradient grad = vertical
            ? juce::ColourGradient (theme::good, bar.getBottomLeft(), theme::rec, bar.getTopLeft(), false)
            : juce::ColourGradient (theme::good, bar.getTopLeft(), theme::rec, bar.getTopRight(), false);
        grad.addColour (0.75, theme::warn);
        g.setGradientFill (grad);
        if (vertical) g.fillRect (bar.withTop (bar.getBottom() - bar.getHeight() * p));
        else g.fillRect (bar.withWidth (bar.getWidth() * p));
        const float hp = toPos (hold);
        g.setColour (hold > 0.99f ? theme::rec : theme::text.withAlpha (0.6f));
        if (vertical) g.fillRect (bar.getX(), bar.getBottom() - bar.getHeight() * hp - 1.0f, bar.getWidth(), 1.5f);
        else g.fillRect (bar.getX() + bar.getWidth() * hp - 1.0f, bar.getY(), 1.5f, bar.getHeight());
    };
    auto inner = r.reduced (1.5f);
    if (vertical)
    {
        auto left = inner.removeFromLeft (inner.getWidth() * 0.5f - 0.5f);
        inner.removeFromLeft (1.0f);
        drawBar (left, levelL, holdL);
        drawBar (inner, levelR, holdR);
    }
    else
    {
        auto top = inner.removeFromTop (inner.getHeight() * 0.5f - 0.5f);
        inner.removeFromTop (1.0f);
        drawBar (top, levelL, holdL);
        drawBar (inner, levelR, holdR);
    }
}

//==============================================================================
PillButton::PillButton (const juce::String& text, juce::Colour onColour, bool toggle) : juce::TextButton (text)
{
    setClickingTogglesState (toggle);
    setColour (juce::TextButton::buttonOnColourId, onColour);
    setColour (juce::TextButton::textColourOnId, onColour.getPerceivedBrightness() > 0.55f ? juce::Colours::black : juce::Colours::white);
}

//==============================================================================
void WaveformThumb::setSample (SampleBuffer::Ptr s)
{
    if (s == sample) return;
    sample = std::move (s);
    rebuild();
}

void WaveformThumb::rebuild()
{
    peaks.clear();
    const int w = juce::jmax (1, getWidth());
    if (sample != nullptr && sample->getNumFrames() > 0)
    {
        const auto& a = sample->getAudio();
        const int frames = a.getNumSamples();
        peaks.resize ((size_t) w);
        for (int x = 0; x < w; ++x)
        {
            const int s0 = (int) ((juce::int64) x * frames / w);
            const int s1 = juce::jmax (s0 + 1, (int) ((juce::int64) (x + 1) * frames / w));
            float lo = 0, hi = 0;
            for (int ch = 0; ch < a.getNumChannels(); ++ch)
            {
                auto range = juce::FloatVectorOperations::findMinAndMax (a.getReadPointer (ch, s0), juce::jmin (s1, frames) - s0);
                lo = juce::jmin (lo, range.getStart());
                hi = juce::jmax (hi, range.getEnd());
            }
            peaks[(size_t) x] = { lo, hi };
        }
    }
    repaint();
}

void WaveformThumb::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setColour (theme::bg0);
    g.fillRoundedRectangle (r, 4.0f);
    if (peaks.empty())
    {
        g.setColour (theme::textFaint);
        g.setFont (theme::font (12.0f));
        g.drawText ("Drop a sample here", getLocalBounds(), juce::Justification::centred);
        return;
    }
    const float mid = r.getCentreY(), half = r.getHeight() * 0.45f;
    for (size_t x = 0; x < peaks.size(); ++x)
    {
        const float fx = (float) x / (float) peaks.size();
        const bool inRegion = fx >= regionStart && fx <= regionEnd;
        g.setColour (inRegion ? colour : colour.withAlpha (0.25f));
        g.drawVerticalLine ((int) x, mid - peaks[x].second * half, mid - peaks[x].first * half + 1.0f);
    }
}

//==============================================================================
ParamPanel::ParamPanel (ParamSet& params, juce::Colour accent, int size) : knobSize (size)
{
    for (auto& p : params.all())
    {
        if (! p->choices.isEmpty() && p->choices.size() > 2)
        {
            auto box = std::make_unique<juce::ComboBox>();
            box->addItemList (p->choices, 1);
            box->setSelectedItemIndex (p->getChoice(), juce::dontSendNotification);
            auto* raw = p.get();
            auto* b = box.get();
            box->onChange = [raw, b] { raw->set ((float) b->getSelectedItemIndex()); };
            box->setTooltip (p->name);
            addAndMakeVisible (*box);
            combos.push_back (std::move (box));
            comboParams.push_back (raw);
        }
        else if (p->choices.size() == 2)
        {
            auto t = std::make_unique<PillButton> (p->name, accent);
            t->setToggleState (p->getBool(), juce::dontSendNotification);
            auto* raw = p.get();
            auto* b = t.get();
            t->onClick = [raw, b] { raw->set (b->getToggleState() ? 1.0f : 0.0f); };
            addAndMakeVisible (*t);
            toggles.push_back (std::move (t));
            toggleParams.push_back (raw);
        }
        else
        {
            auto k = std::make_unique<Knob> (p->name, accent);
            k->bind (p.get());
            addAndMakeVisible (*k);
            knobs.push_back (std::move (k));
        }
    }
    startTimerHz (15);
}

int ParamPanel::getIdealWidth (int maxPerRow) const
{
    const int items = (int) (knobs.size() + combos.size() + toggles.size());
    return juce::jmin (items, maxPerRow) * (knobSize + 6);
}

int ParamPanel::getIdealHeight (int width) const
{
    const int perRow = juce::jmax (1, width / (knobSize + 6));
    const int items = (int) (knobs.size() + combos.size() + toggles.size());
    return ((items + perRow - 1) / perRow) * (knobSize + 18);
}

void ParamPanel::resized()
{
    const int perRow = juce::jmax (1, getWidth() / (knobSize + 6));
    int i = 0;
    auto place = [&] (juce::Component& c, bool combo)
    {
        const int row = i / perRow, col = i % perRow;
        auto cell = juce::Rectangle<int> (col * (knobSize + 6), row * (knobSize + 18), knobSize, knobSize + 14);
        c.setBounds (combo ? cell.withSizeKeepingCentre (knobSize, 24) : cell);
        ++i;
    };
    for (auto& c : combos) place (*c, true);
    for (auto& t : toggles) place (*t, true);
    for (auto& k : knobs) place (*k, false);
}

void ParamPanel::timerCallback()
{
    for (auto& k : knobs) k->refreshFromParam();
    for (size_t i = 0; i < toggles.size(); ++i)
        if (toggles[i]->getToggleState() != toggleParams[i]->getBool())
            toggles[i]->setToggleState (toggleParams[i]->getBool(), juce::dontSendNotification);
    for (size_t i = 0; i < combos.size(); ++i)
        if (combos[i]->getSelectedItemIndex() != comboParams[i]->getChoice())
            combos[i]->setSelectedItemIndex (comboParams[i]->getChoice(), juce::dontSendNotification);
}

//==============================================================================
juce::File fileFromDrag (const juce::DragAndDropTarget::SourceDetails& d)
{
    const auto desc = d.description.toString();
    if (desc.startsWith ("file:"))
        return juce::File (desc.substring (5));
    if (auto* tree = dynamic_cast<juce::FileTreeComponent*> (d.sourceComponent.get()))
        return tree->getSelectedFile();
    return {};
}

//==============================================================================
void showMenu (juce::PopupMenu menu, juce::Component* target, std::function<void (int)> callback)
{
    auto opts = juce::PopupMenu::Options();
    if (target != nullptr)
        opts = opts.withTargetComponent (target);
    menu.showMenuAsync (opts, [callback] (int r) { if (r != 0 && callback) callback (r); });
}

} // namespace merge
