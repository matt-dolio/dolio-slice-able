// =============================================================================
// SliceLookAndFeel.cpp - colours and custom control drawing. See the header for
// where to change colours and fonts.
// =============================================================================

#include "SliceLookAndFeel.h"

namespace sliceable
{

SliceLookAndFeel::SliceLookAndFeel()
{
    // Start from JUCE's dark scheme and swap in our palette. The colour scheme
    // covers most controls in one go (menus, text, highlights)...
    auto scheme = getDarkColourScheme();
    scheme.setUIColour (ColourScheme::windowBackground, Colours::background);
    scheme.setUIColour (ColourScheme::widgetBackground, Colours::panel);
    scheme.setUIColour (ColourScheme::menuBackground, Colours::panel);
    scheme.setUIColour (ColourScheme::outline, Colours::panelOutline);
    scheme.setUIColour (ColourScheme::defaultText, Colours::text);
    scheme.setUIColour (ColourScheme::defaultFill, Colours::accent);
    scheme.setUIColour (ColourScheme::highlightedText, Colours::background);
    scheme.setUIColour (ColourScheme::highlightedFill, Colours::accent);
    scheme.setUIColour (ColourScheme::menuText, Colours::text);
    setColourScheme (scheme);

    // ...and these override individual colour IDs the scheme doesn't set the way we want.
    // (Must come AFTER setColourScheme, which resets them.)
    setColour (juce::ComboBox::backgroundColourId, Colours::background);
    setColour (juce::ComboBox::outlineColourId, Colours::panelOutline);
    setColour (juce::ComboBox::arrowColourId, Colours::dimText);
    setColour (juce::TextButton::buttonColourId, Colours::panel);
    setColour (juce::TextButton::textColourOffId, Colours::text);
    setColour (juce::Label::textColourId, Colours::text);
    setColour (juce::Slider::textBoxTextColourId, Colours::text);
    setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
    setColour (juce::Slider::rotarySliderFillColourId, Colours::accent);
    setColour (juce::Slider::rotarySliderOutlineColourId, Colours::panelOutline);
    setColour (juce::ToggleButton::tickColourId, Colours::accent);
    setColour (juce::ToggleButton::tickDisabledColourId, Colours::dimText);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, Colours::accent);
    setColour (juce::PopupMenu::highlightedTextColourId, Colours::background);
}

void SliceLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height, float sliderPos,
                                         float rotaryStartAngle, float rotaryEndAngle, juce::Slider& slider)
{
    // sliderPos is the knob's position from 0 (minimum) to 1 (maximum), already
    // taking the parameter's skew into account. Angles are in radians, measured
    // clockwise from 12 o'clock; JUCE's defaults sweep from about 7 to 5 o'clock.
    const auto bounds = juce::Rectangle<int> (x, y, width, height).toFloat().reduced (4.0f);
    const auto radius = juce::jmin (bounds.getWidth(), bounds.getHeight()) / 2.0f;
    const auto centre = bounds.getCentre();
    const auto lineWidth = juce::jmax (2.0f, radius * 0.16f); // arc thickness scales with knob size
    const auto arcRadius = radius - lineWidth * 0.5f;
    const auto angle = rotaryStartAngle + sliderPos * (rotaryEndAngle - rotaryStartAngle);
    const auto enabled = slider.isEnabled();

    // 1. Background track: the full sweep, in a dim colour.
    juce::Path track;
    track.addCentredArc (centre.x, centre.y, arcRadius, arcRadius, 0.0f, rotaryStartAngle, rotaryEndAngle, true);
    g.setColour (Colours::panelOutline);
    g.strokePath (track, juce::PathStrokeType (lineWidth, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    // 2. Value arc, in the accent colour.
    // Bipolar parameters (pitch) fill from the centre.
    const auto bipolar = slider.getMinimum() < 0.0 && slider.getMaximum() > 0.0;
    const auto fromAngle = bipolar ? (rotaryStartAngle + rotaryEndAngle) * 0.5f : rotaryStartAngle;

    juce::Path value;
    value.addCentredArc (centre.x, centre.y, arcRadius, arcRadius, 0.0f,
                         juce::jmin (fromAngle, angle), juce::jmax (fromAngle, angle), true);
    g.setColour (enabled ? Colours::accent : Colours::dimText.withAlpha (0.5f)); // greyed when disabled
    g.strokePath (value, juce::PathStrokeType (lineWidth, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    // 3. The knob cap: a filled circle inside the arcs.
    const auto knobRadius = arcRadius - lineWidth * 1.4f;
    g.setColour (Colours::panel.brighter (0.15f));
    g.fillEllipse (juce::Rectangle<float> (knobRadius * 2.0f, knobRadius * 2.0f).withCentre (centre));

    // 4. Pointer line on the cap, showing the current angle.
    const auto tip = centre.getPointOnCircumference (knobRadius * 0.85f, angle);
    g.setColour (enabled ? Colours::text : Colours::dimText);
    g.drawLine ({ centre.getPointOnCircumference (knobRadius * 0.3f, angle), tip }, 2.0f);
}

void SliceLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& button, const juce::Colour& backgroundColour,
                                             bool isHighlighted, bool isDown)
{
    auto bounds = button.getLocalBounds().toFloat().reduced (0.5f); // half-pixel inset keeps the outline crisp
    auto colour = backgroundColour;

    // Slightly lighter while hovered, lighter still while pressed.
    if (isDown)
        colour = colour.brighter (0.2f);
    else if (isHighlighted)
        colour = colour.brighter (0.1f);

    g.setColour (colour);
    g.fillRoundedRectangle (bounds, 4.0f);
    g.setColour (isHighlighted ? Colours::accent.withAlpha (0.7f) : Colours::panelOutline);
    g.drawRoundedRectangle (bounds, 4.0f, 1.0f);
}

juce::Font SliceLookAndFeel::getComboBoxFont (juce::ComboBox&)
{
    return juce::FontOptions (13.0f);
}

juce::Font SliceLookAndFeel::getTextButtonFont (juce::TextButton&, int)
{
    return juce::FontOptions (13.0f, juce::Font::bold);
}

juce::Label* SliceLookAndFeel::createSliderTextBox (juce::Slider& slider)
{
    // Start from JUCE's default value box, then restyle it.
    auto* label = LookAndFeel_V4::createSliderTextBox (slider);
    label->setFont (juce::FontOptions (12.0f));
    label->setColour (juce::Label::outlineColourId, juce::Colours::transparentBlack);   // no border normally
    label->setColour (juce::Label::outlineWhenEditingColourId, Colours::accent);         // orange border while typing
    label->setColour (juce::TextEditor::backgroundColourId, Colours::background);
    return label;
}

} // namespace sliceable
