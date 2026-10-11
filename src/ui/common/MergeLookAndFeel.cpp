#include "MergeLookAndFeel.h"
#include "MergeResources.h"

namespace merge
{

MergeLookAndFeel::MergeLookAndFeel()
{
    using namespace theme;
    setColourScheme ({ bg0, bg1, bg2, line, text, bg3, juce::Colours::white, make, text });
    setColour (juce::ResizableWindow::backgroundColourId, bg0);
    setColour (juce::DocumentWindow::textColourId, text);
    setColour (juce::TextButton::buttonColourId, bg2);
    setColour (juce::TextButton::buttonOnColourId, make);
    setColour (juce::TextButton::textColourOffId, text);
    setColour (juce::TextButton::textColourOnId, juce::Colours::black);
    setColour (juce::ComboBox::backgroundColourId, bg2);
    setColour (juce::ComboBox::outlineColourId, line);
    setColour (juce::ComboBox::textColourId, text);
    setColour (juce::ComboBox::arrowColourId, textDim);
    setColour (juce::PopupMenu::backgroundColourId, bg1);
    setColour (juce::PopupMenu::textColourId, text);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, bg3);
    setColour (juce::PopupMenu::highlightedTextColourId, juce::Colours::white);
    setColour (juce::Label::textColourId, text);
    setColour (juce::Slider::textBoxTextColourId, text);
    setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour (juce::Slider::textBoxBackgroundColourId, bg2);
    setColour (juce::Slider::rotarySliderFillColourId, make);
    setColour (juce::Slider::trackColourId, make);
    setColour (juce::ListBox::backgroundColourId, bg1);
    setColour (juce::ListBox::outlineColourId, line);
    setColour (juce::TreeView::backgroundColourId, bg1);
    setColour (juce::TreeView::selectedItemBackgroundColourId, bg3);
    setColour (juce::DirectoryContentsDisplayComponent::highlightColourId, bg3);
    setColour (juce::DirectoryContentsDisplayComponent::textColourId, text);
    setColour (juce::TextEditor::backgroundColourId, bg2);
    setColour (juce::TextEditor::textColourId, text);
    setColour (juce::TextEditor::outlineColourId, line);
    setColour (juce::TextEditor::focusedOutlineColourId, make);
    setColour (juce::CaretComponent::caretColourId, make);
    setColour (juce::ScrollBar::thumbColourId, bg3);
    setColour (juce::TabbedButtonBar::tabTextColourId, textDim);
    setColour (juce::TabbedButtonBar::frontTextColourId, text);
    setColour (juce::TabbedComponent::backgroundColourId, bg1);
    setColour (juce::TabbedComponent::outlineColourId, juce::Colours::transparentBlack);
    setColour (juce::AlertWindow::backgroundColourId, bg1);
    setColour (juce::AlertWindow::textColourId, text);
    setColour (juce::TooltipWindow::backgroundColourId, bg3);
    setColour (juce::TooltipWindow::textColourId, text);
    setColour (juce::TableHeaderComponent::backgroundColourId, bg2);
    setColour (juce::TableHeaderComponent::textColourId, textDim);
    setColour (juce::TableHeaderComponent::outlineColourId, line);
    setColour (juce::ToggleButton::textColourId, text);
    setColour (juce::ToggleButton::tickColourId, make);
    regular = juce::Typeface::createSystemTypefaceFor (MergeResources::InterRegular_otf, (size_t) MergeResources::InterRegular_otfSize);
    bold = juce::Typeface::createSystemTypefaceFor (MergeResources::InterSemiBold_otf, (size_t) MergeResources::InterSemiBold_otfSize);
}

juce::Typeface::Ptr MergeLookAndFeel::getTypefaceForFont (const juce::Font& f)
{
    if (f.getTypefaceName() == juce::Font::getDefaultSansSerifFontName() && regular != nullptr)
        return f.isBold() ? bold : regular;
    return juce::LookAndFeel_V4::getTypefaceForFont (f);
}

void MergeLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float start,
                                         float end, juce::Slider& s)
{
    const auto bounds = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h).reduced (2.0f);
    const float radius = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f;
    const auto centre = bounds.getCentre();
    const float lineW = juce::jmax (2.0f, radius * 0.18f);
    const float arcR = radius - lineW * 0.5f;
    const auto accent = s.findColour (juce::Slider::rotarySliderFillColourId);

    juce::Path bgArc;
    bgArc.addCentredArc (centre.x, centre.y, arcR, arcR, 0.0f, start, end, true);
    g.setColour (theme::bg3);
    g.strokePath (bgArc, juce::PathStrokeType (lineW, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    // bipolar knobs (range symmetric around 0) fill from the centre
    const bool bipolar = s.getMinimum() < 0.0 && s.getMaximum() > 0.0;
    const float from = bipolar ? (start + end) * 0.5f : start;
    const float to = start + pos * (end - start);
    juce::Path valueArc;
    valueArc.addCentredArc (centre.x, centre.y, arcR, arcR, 0.0f, juce::jmin (from, to), juce::jmax (from, to), true);
    g.setColour (s.isEnabled() ? accent : theme::textFaint);
    g.strokePath (valueArc, juce::PathStrokeType (lineW, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    const float knobR = arcR - lineW * 1.2f;
    g.setColour (theme::bg2.brighter (0.08f));
    g.fillEllipse (centre.x - knobR, centre.y - knobR, knobR * 2.0f, knobR * 2.0f);
    juce::Path pointer;
    pointer.addRoundedRectangle (-lineW * 0.35f, -knobR, lineW * 0.7f, knobR * 0.55f, lineW * 0.3f);
    pointer.applyTransform (juce::AffineTransform::rotation (to).translated (centre.x, centre.y));
    g.setColour (theme::text);
    g.fillPath (pointer);
}

void MergeLookAndFeel::drawLinearSlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float, float,
                                         juce::Slider::SliderStyle style, juce::Slider& s)
{
    const auto accent = s.findColour (juce::Slider::trackColourId);
    const bool vertical = style == juce::Slider::LinearVertical || style == juce::Slider::LinearBarVertical;
    if (style == juce::Slider::LinearBar || style == juce::Slider::LinearBarVertical)
    {
        g.setColour (theme::bg2);
        g.fillRoundedRectangle ((float) x, (float) y, (float) w, (float) h, 3.0f);
        g.setColour (accent.withAlpha (0.7f));
        if (vertical) g.fillRoundedRectangle ((float) x, pos, (float) w, (float) (y + h) - pos, 3.0f);
        else g.fillRoundedRectangle ((float) x, (float) y, pos - (float) x, (float) h, 3.0f);
        return;
    }
    if (vertical)
    {
        const float cx = x + w * 0.5f;
        g.setColour (theme::bg0);
        g.fillRoundedRectangle (cx - 2.5f, (float) y, 5.0f, (float) h, 2.5f);
        g.setColour (accent.withAlpha (0.8f));
        g.fillRoundedRectangle (cx - 2.5f, pos, 5.0f, (float) (y + h) - pos, 2.5f);
        const float capH = juce::jmin (22.0f, h * 0.18f), capW = juce::jmin ((float) w, 34.0f);
        auto cap = juce::Rectangle<float> (capW, capH).withCentre ({ cx, pos });
        g.setColour (theme::bg3.brighter (0.15f));
        g.fillRoundedRectangle (cap, 3.0f);
        g.setColour (theme::text);
        g.fillRect (cap.withSizeKeepingCentre (capW - 6.0f, 2.0f));
    }
    else
    {
        const float cy = y + h * 0.5f;
        g.setColour (theme::bg0);
        g.fillRoundedRectangle ((float) x, cy - 2.5f, (float) w, 5.0f, 2.5f);
        const bool bipolar = s.getMinimum() < 0.0 && s.getMaximum() > 0.0;
        const float zero = bipolar ? (float) s.getPositionOfValue (0.0) : (float) x;
        g.setColour (accent.withAlpha (0.8f));
        g.fillRoundedRectangle (juce::jmin (zero, pos), cy - 2.5f, std::abs (pos - zero), 5.0f, 2.5f);
        const float capW = juce::jmin (16.0f, w * 0.2f), capH = juce::jmin ((float) h, 26.0f);
        auto cap = juce::Rectangle<float> (capW, capH).withCentre ({ pos, cy });
        g.setColour (theme::bg3.brighter (0.15f));
        g.fillRoundedRectangle (cap, 3.0f);
        g.setColour (theme::text);
        g.fillRect (cap.withSizeKeepingCentre (2.0f, capH - 6.0f));
    }
}

void MergeLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&, bool over, bool down)
{
    auto r = b.getLocalBounds().toFloat().reduced (0.5f);
    const bool on = b.getToggleState();
    auto base = on ? b.findColour (juce::TextButton::buttonOnColourId) : b.findColour (juce::TextButton::buttonColourId);
    if (down) base = base.brighter (0.2f);
    else if (over) base = base.brighter (0.08f);
    g.setColour (base);
    g.fillRoundedRectangle (r, 4.0f);
    if (! on)
    {
        g.setColour (theme::line);
        g.drawRoundedRectangle (r, 4.0f, 1.0f);
    }
}

void MergeLookAndFeel::drawButtonText (juce::Graphics& g, juce::TextButton& b, bool, bool)
{
    g.setFont (getTextButtonFont (b, b.getHeight()));
    g.setColour (b.findColour (b.getToggleState() ? juce::TextButton::textColourOnId : juce::TextButton::textColourOffId)
                     .withMultipliedAlpha (b.isEnabled() ? 1.0f : 0.4f));
    g.drawFittedText (b.getButtonText(), b.getLocalBounds().reduced (4, 2), juce::Justification::centred, 1, 0.8f);
}

void MergeLookAndFeel::drawComboBox (juce::Graphics& g, int w, int h, bool, int, int, int, int, juce::ComboBox& box)
{
    auto r = juce::Rectangle<float> (0, 0, (float) w, (float) h).reduced (0.5f);
    g.setColour (box.findColour (juce::ComboBox::backgroundColourId));
    g.fillRoundedRectangle (r, 4.0f);
    g.setColour (box.findColour (juce::ComboBox::outlineColourId));
    g.drawRoundedRectangle (r, 4.0f, 1.0f);
    juce::Path arrow;
    const float ax = (float) w - 12.0f, ay = h * 0.5f;
    arrow.addTriangle (ax - 4.0f, ay - 2.0f, ax + 4.0f, ay - 2.0f, ax, ay + 3.0f);
    g.setColour (theme::textDim);
    g.fillPath (arrow);
}

void MergeLookAndFeel::drawPopupMenuBackground (juce::Graphics& g, int w, int h)
{
    g.fillAll (theme::bg1);
    g.setColour (theme::line);
    g.drawRect (0, 0, w, h);
}

void MergeLookAndFeel::drawScrollbar (juce::Graphics& g, juce::ScrollBar&, int x, int y, int w, int h, bool vertical,
                                      int thumbStart, int thumbSize, bool over, bool)
{
    juce::Rectangle<int> thumb = vertical ? juce::Rectangle<int> (x + 2, thumbStart, w - 4, thumbSize)
                                          : juce::Rectangle<int> (thumbStart, y + 2, thumbSize, h - 4);
    g.setColour (over ? theme::bg3.brighter (0.2f) : theme::bg3);
    g.fillRoundedRectangle (thumb.toFloat(), 3.0f);
}

void MergeLookAndFeel::drawTabButton (juce::TabBarButton& b, juce::Graphics& g, bool over, bool)
{
    auto r = b.getLocalBounds().toFloat();
    const bool front = b.isFrontTab();
    if (front)
    {
        g.setColour (theme::bg2);
        g.fillRoundedRectangle (r.reduced (2.0f, 3.0f), 4.0f);
        g.setColour (b.findColour (juce::TabbedButtonBar::frontOutlineColourId, true) == juce::Colour()
                         ? theme::make : b.findColour (juce::TabbedButtonBar::frontOutlineColourId, true));
        g.fillRect (r.removeFromBottom (2.0f).reduced (8.0f, 0.0f));
    }
    g.setColour (front ? theme::text : (over ? theme::text.withAlpha (0.8f) : theme::textDim));
    g.setFont (theme::font (13.5f, true));
    g.drawText (b.getButtonText(), b.getLocalBounds(), juce::Justification::centred);
}

int MergeLookAndFeel::getTabButtonBestWidth (juce::TabBarButton& b, int)
{
    return juce::GlyphArrangement::getStringWidthInt (theme::font (13.5f, true), b.getButtonText()) + 36;
}

void MergeLookAndFeel::fillTextEditorBackground (juce::Graphics& g, int w, int h, juce::TextEditor&)
{
    g.setColour (theme::bg2);
    g.fillRoundedRectangle (0, 0, (float) w, (float) h, 4.0f);
}

void MergeLookAndFeel::drawTextEditorOutline (juce::Graphics& g, int w, int h, juce::TextEditor& e)
{
    g.setColour (e.hasKeyboardFocus (true) ? theme::make : theme::line);
    g.drawRoundedRectangle (0.5f, 0.5f, (float) w - 1.0f, (float) h - 1.0f, 4.0f, 1.0f);
}

void MergeLookAndFeel::drawTooltip (juce::Graphics& g, const juce::String& text, int w, int h)
{
    g.fillAll (theme::bg3);
    g.setColour (theme::text);
    g.setFont (theme::font (13.0f));
    g.drawFittedText (text, juce::Rectangle<int> (w, h).reduced (6, 2), juce::Justification::centredLeft, 4);
}

} // namespace merge
