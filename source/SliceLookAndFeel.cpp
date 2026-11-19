#include "SliceLookAndFeel.h"

namespace sliceable
{

SliceLookAndFeel::SliceLookAndFeel()
{
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
    const auto bounds = juce::Rectangle<int> (x, y, width, height).toFloat().reduced (4.0f);
    const auto radius = juce::jmin (bounds.getWidth(), bounds.getHeight()) / 2.0f;
    const auto centre = bounds.getCentre();
    const auto lineWidth = juce::jmax (2.0f, radius * 0.16f);
    const auto arcRadius = radius - lineWidth * 0.5f;
    const auto angle = rotaryStartAngle + sliderPos * (rotaryEndAngle - rotaryStartAngle);
    const auto enabled = slider.isEnabled();

    juce::Path track;
    track.addCentredArc (centre.x, centre.y, arcRadius, arcRadius, 0.0f, rotaryStartAngle, rotaryEndAngle, true);
    g.setColour (Colours::panelOutline);
    g.strokePath (track, juce::PathStrokeType (lineWidth, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    // Bipolar parameters (pitch) fill from the centre.
    const auto bipolar = slider.getMinimum() < 0.0 && slider.getMaximum() > 0.0;
    const auto fromAngle = bipolar ? (rotaryStartAngle + rotaryEndAngle) * 0.5f : rotaryStartAngle;

    juce::Path value;
    value.addCentredArc (centre.x, centre.y, arcRadius, arcRadius, 0.0f,
                         juce::jmin (fromAngle, angle), juce::jmax (fromAngle, angle), true);
    g.setColour (enabled ? Colours::accent : Colours::dimText.withAlpha (0.5f));
    g.strokePath (value, juce::PathStrokeType (lineWidth, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    const auto knobRadius = arcRadius - lineWidth * 1.4f;
    g.setColour (Colours::panel.brighter (0.15f));
    g.fillEllipse (juce::Rectangle<float> (knobRadius * 2.0f, knobRadius * 2.0f).withCentre (centre));

    const auto tip = centre.getPointOnCircumference (knobRadius * 0.85f, angle);
    g.setColour (enabled ? Colours::text : Colours::dimText);
    g.drawLine ({ centre.getPointOnCircumference (knobRadius * 0.3f, angle), tip }, 2.0f);
}

void SliceLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& button, const juce::Colour& backgroundColour,
                                             bool isHighlighted, bool isDown)
{
    auto bounds = button.getLocalBounds().toFloat().reduced (0.5f);
    auto colour = backgroundColour;

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
    auto* label = LookAndFeel_V4::createSliderTextBox (slider);
    label->setFont (juce::FontOptions (12.0f));
    label->setColour (juce::Label::outlineColourId, juce::Colours::transparentBlack);
    label->setColour (juce::Label::outlineWhenEditingColourId, Colours::accent);
    label->setColour (juce::TextEditor::backgroundColourId, Colours::background);
    return label;
}

} // namespace sliceable
