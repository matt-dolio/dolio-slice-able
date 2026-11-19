#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace sliceable
{

namespace Colours
{
    inline const juce::Colour background   { 0xff14161b };
    inline const juce::Colour panel        { 0xff1c1f26 };
    inline const juce::Colour panelOutline { 0xff2b3039 };
    inline const juce::Colour text         { 0xffe7e9ec };
    inline const juce::Colour dimText      { 0xff8b929d };
    inline const juce::Colour accent       { 0xffff8a3d }; // slice / active highlight
    inline const juce::Colour waveform     { 0xff5cc8f2 };
    inline const juce::Colour sliceBandA   { 0xff181b21 };
    inline const juce::Colour sliceBandB   { 0xff1f232a };
    inline const juce::Colour gridLine     { 0xff59606c };
    inline const juce::Colour error        { 0xffff5c5c };
    inline const juce::Colour success      { 0xff7ddc8a };
}

class SliceLookAndFeel final : public juce::LookAndFeel_V4
{
public:
    SliceLookAndFeel();

    void drawRotarySlider (juce::Graphics&, int x, int y, int width, int height, float sliderPos,
                           float rotaryStartAngle, float rotaryEndAngle, juce::Slider&) override;

    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour& backgroundColour,
                               bool isHighlighted, bool isDown) override;

    juce::Font getComboBoxFont (juce::ComboBox&) override;
    juce::Font getTextButtonFont (juce::TextButton&, int buttonHeight) override;
    juce::Label* createSliderTextBox (juce::Slider&) override;
};

} // namespace sliceable
