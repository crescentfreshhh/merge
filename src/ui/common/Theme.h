#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace merge::theme
{

inline const juce::Colour bg0       { 0xff0d0e12 }; // window
inline const juce::Colour bg1       { 0xff15171d }; // panels
inline const juce::Colour bg2       { 0xff1d2028 }; // controls
inline const juce::Colour bg3       { 0xff272b35 }; // raised / hover
inline const juce::Colour line      { 0xff2e3340 };
inline const juce::Colour text      { 0xffe8eaf0 };
inline const juce::Colour textDim   { 0xff8a90a0 };
inline const juce::Colour textFaint { 0xff5a6070 };
inline const juce::Colour make      { 0xffff7a1a }; // beatmaker accent
inline const juce::Colour mix       { 0xff19d3c5 }; // DJ accent
inline const juce::Colour rec       { 0xffff3b4e };
inline const juce::Colour good      { 0xff3fd28a };
inline const juce::Colour warn      { 0xfff2c94c };

inline const juce::Colour deckColours[] { juce::Colour (0xff3fa9f5), juce::Colour (0xffff5a5f),
                                          juce::Colour (0xff6bd66b), juce::Colour (0xffb37bff) };

inline juce::Colour deck (int i) { return deckColours[(size_t) (i & 3)]; }

inline juce::FontOptions font (float size, bool bold = false)
{
    return juce::FontOptions (size, bold ? juce::Font::bold : juce::Font::plain);
}

} // namespace merge::theme
