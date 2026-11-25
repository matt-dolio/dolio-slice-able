#pragma once

// =============================================================================
// SliceLookAndFeel.h - the plug-in's visual style: colours and custom drawing.
//
// In JUCE, a LookAndFeel object decides how standard controls (knobs, buttons,
// menus) are drawn. The editor installs one SliceLookAndFeel on itself, and every
// control inside the window picks it up automatically.
//
// Common changes:
//   - Re-colour the plug-in ..... edit the values in `Colours` below. Every
//                                 part of the UI takes its colours from here
//                                 (except a few keyboard shades in PluginEditor.cpp).
//   - Change how knobs look ..... drawRotarySlider() in SliceLookAndFeel.cpp.
//   - Change fonts / sizes ...... getComboBoxFont(), getTextButtonFont(),
//                                 createSliderTextBox(), and the FontOptions
//                                 calls in PluginEditor.cpp / WaveformView.cpp.
//
// Colour values are written as 0xAARRGGBB: alpha (ff = opaque), then red,
// green and blue in hex - e.g. 0xffff8a3d is an opaque orange.
// =============================================================================

#include <juce_gui_basics/juce_gui_basics.h>

namespace sliceable
{

// The palette used throughout the UI.
namespace Colours
{
    inline const juce::Colour background   { 0xff14161b }; // window background
    inline const juce::Colour panel        { 0xff1c1f26 }; // GRID / PLAYBACK panels, buttons
    inline const juce::Colour panelOutline { 0xff2b3039 }; // panel borders, knob tracks
    inline const juce::Colour text         { 0xffe7e9ec }; // main text
    inline const juce::Colour dimText      { 0xff8b929d }; // labels, secondary text
    inline const juce::Colour accent       { 0xffff8a3d }; // slice / active highlight (orange)
    inline const juce::Colour waveform     { 0xff5cc8f2 }; // the waveform (blue)
    inline const juce::Colour sliceBandA   { 0xff181b21 }; // even slices' background
    inline const juce::Colour sliceBandB   { 0xff1f232a }; // odd slices' background
    inline const juce::Colour gridLine     { 0xff59606c }; // lines between slices
    inline const juce::Colour error        { 0xffff5c5c }; // error messages
    inline const juce::Colour success      { 0xff7ddc8a }; // success messages
}

class SliceLookAndFeel final : public juce::LookAndFeel_V4
{
public:
    /** Applies the palette above to JUCE's built-in controls. */
    SliceLookAndFeel();

    /** Draws a knob: grey track arc, orange value arc, round cap with a pointer line. */
    void drawRotarySlider (juce::Graphics&, int x, int y, int width, int height, float sliderPos,
                           float rotaryStartAngle, float rotaryEndAngle, juce::Slider&) override;

    /** Draws push buttons (Load / Export) as rounded rectangles with an orange hover outline. */
    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour& backgroundColour,
                               bool isHighlighted, bool isDown) override;

    juce::Font getComboBoxFont (juce::ComboBox&) override;
    juce::Font getTextButtonFont (juce::TextButton&, int buttonHeight) override;

    /** Styles the value box under each knob (font, no border, orange border while typing). */
    juce::Label* createSliderTextBox (juce::Slider&) override;
};

} // namespace sliceable
