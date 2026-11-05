#include "Theme.h"

namespace nux::theme
{
NuxLookAndFeel::NuxLookAndFeel()
{
    setColour (juce::ResizableWindow::backgroundColourId, windowBg);

    setColour (juce::Label::textColourId,                 text);
    setColour (juce::Label::backgroundColourId,           juce::Colours::transparentBlack);
    setColour (juce::Label::outlineWhenEditingColourId,   accent);

    setColour (juce::TextButton::buttonColourId,          surface);
    setColour (juce::TextButton::buttonOnColourId,        accentDeep);
    setColour (juce::TextButton::textColourOffId,         text);
    setColour (juce::TextButton::textColourOnId,          windowBg);

    setColour (juce::TextEditor::backgroundColourId,      panel);
    setColour (juce::TextEditor::textColourId,            text);
    setColour (juce::TextEditor::highlightColourId,       accentGlow);
    setColour (juce::TextEditor::outlineColourId,         outline);
    setColour (juce::TextEditor::focusedOutlineColourId,  accent);

    setColour (juce::ComboBox::backgroundColourId,        surface);
    setColour (juce::ComboBox::textColourId,              text);
    setColour (juce::ComboBox::outlineColourId,           outline);
    setColour (juce::ComboBox::arrowColourId,             accent);
    setColour (juce::ComboBox::buttonColourId,            surface);

    setColour (juce::PopupMenu::backgroundColourId,       panelRaised);
    setColour (juce::PopupMenu::textColourId,             text);
    setColour (juce::PopupMenu::headerTextColourId,       textDim);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, accentDeep);
    setColour (juce::PopupMenu::highlightedTextColourId,  windowBg);

    setColour (juce::Slider::thumbColourId,               accent);
    setColour (juce::Slider::trackColourId,               accentDeep);
    setColour (juce::Slider::backgroundColourId,          surface);
    setColour (juce::Slider::rotarySliderFillColourId,    accent);
    setColour (juce::Slider::rotarySliderOutlineColourId, surface);
    setColour (juce::Slider::textBoxTextColourId,         text);
    setColour (juce::Slider::textBoxBackgroundColourId,   panel);
    setColour (juce::Slider::textBoxOutlineColourId,      juce::Colours::transparentBlack);

    setColour (juce::ScrollBar::thumbColourId,            surfaceHover);
    setColour (juce::ScrollBar::backgroundColourId,       juce::Colours::transparentBlack);

    setColour (juce::TooltipWindow::backgroundColourId,   panelRaised);
    setColour (juce::TooltipWindow::textColourId,         text);
    setColour (juce::TooltipWindow::outlineColourId,      outline);

    setColour (juce::ListBox::backgroundColourId,         panel);
    setColour (juce::ListBox::textColourId,               text);
    setColour (juce::ListBox::outlineColourId,            outline);

    setColour (juce::TableHeaderComponent::backgroundColourId, panelRaised);
    setColour (juce::TableHeaderComponent::textColourId,  textDim);
    setColour (juce::TableHeaderComponent::outlineColourId, outline);

    setColour (juce::DocumentWindow::textColourId,        text);
}

void NuxLookAndFeel::drawPanel (juce::Graphics& g, juce::Rectangle<float> bounds,
                                juce::Colour fill, juce::Colour border, float radius)
{
    g.setColour (fill);
    g.fillRoundedRectangle (bounds, radius);

    if (! border.isTransparent())
    {
        g.setColour (border);
        g.drawRoundedRectangle (bounds.reduced (0.5f), radius, 1.0f);
    }
}

void NuxLookAndFeel::drawGlow (juce::Graphics& g, juce::Rectangle<float> bounds,
                               juce::Colour colour, float radius)
{
    g.setColour (colour.withAlpha (0.10f));
    g.fillRoundedRectangle (bounds.expanded (2.0f), radius + 2.0f);
}

void NuxLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& button,
                                          const juce::Colour& backgroundColour,
                                          bool isHighlighted, bool isDown)
{
    auto bounds = button.getLocalBounds().toFloat().reduced (0.5f);
    const auto radius = juce::jmin (metrics::cornerRadius, bounds.getHeight() * 0.4f);

    auto base = button.getToggleState() ? accentDeep : backgroundColour;

    if (isDown)          base = base.brighter (0.20f);
    else if (isHighlighted) base = base.brighter (0.09f);

    if (button.getToggleState())
        drawGlow (g, bounds, accent, radius);

    g.setColour (base);
    g.fillRoundedRectangle (bounds, radius);

    if (! button.getToggleState())
    {
        g.setColour (outline);
        g.drawRoundedRectangle (bounds, radius, 1.0f);
    }
}

void NuxLookAndFeel::drawButtonText (juce::Graphics& g, juce::TextButton& button,
                                    bool isHighlighted, bool isDown)
{
    g.setFont (font::bold (juce::jlimit (10.0f, 14.0f, button.getHeight() * 0.38f)));
    g.setColour (button.getToggleState() ? windowBg : text.withAlpha (isDown ? 1.0f : (isHighlighted ? 0.95f : 0.82f)));

    g.drawFittedText (button.getButtonText(), button.getLocalBounds(),
                      juce::Justification::centred, 1, 0.9f);
}

void NuxLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height,
                                      float sliderPos, float rotaryStartAngle,
                                      float rotaryEndAngle, juce::Slider& slider)
{
    auto bounds = juce::Rectangle<int> (x, y, width, height).toFloat().reduced (2.0f);
    const auto radius = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f;
    const auto centreX = bounds.getCentreX();
    const auto centreY = bounds.getCentreY();
    const auto angle = rotaryStartAngle + sliderPos * (rotaryEndAngle - rotaryStartAngle);
    const auto lineWidth = juce::jmax (2.0f, radius * 0.16f);
    const auto arcRadius = radius - lineWidth * 0.6f;

    juce::Path backgroundArc, valueArc;
    backgroundArc.addCentredArc (centreX, centreY, arcRadius, arcRadius, 0.0f,
                                 rotaryStartAngle, rotaryEndAngle, true);
    valueArc.addCentredArc (centreX, centreY, arcRadius, arcRadius, 0.0f,
                            rotaryStartAngle, angle, true);

    g.setColour (slider.findColour (juce::Slider::rotarySliderOutlineColourId));
    g.strokePath (backgroundArc, juce::PathStrokeType (lineWidth, juce::PathStrokeType::curved,
                                                      juce::PathStrokeType::rounded));

    g.setColour (slider.isEnabled() ? accent : textFaint);
    g.strokePath (valueArc, juce::PathStrokeType (lineWidth, juce::PathStrokeType::curved,
                                                  juce::PathStrokeType::rounded));

    // Centre cap.
    const auto capSize = radius * 0.72f;
    g.setColour (surface);
    g.fillEllipse (centreX - capSize * 0.5f, centreY - capSize * 0.5f, capSize, capSize);

    // Pointer.
    juce::Path pointer;
    const auto pointerLength = capSize * 0.42f;
    const auto pointerThickness = juce::jmax (1.5f, capSize * 0.12f);
    pointer.addRoundedRectangle (-pointerThickness * 0.5f, -capSize * 0.5f + capSize * 0.12f,
                                 pointerThickness, pointerLength, pointerThickness * 0.5f);
    pointer.applyTransform (juce::AffineTransform::rotation (angle).translated (centreX, centreY));
    g.setColour (accent);
    g.fillPath (pointer);
}

void NuxLookAndFeel::drawLinearSlider (juce::Graphics& g, int x, int y, int width, int height,
                                      float sliderPos, float, float,
                                      juce::Slider::SliderStyle style, juce::Slider& slider)
{
    const auto isHorizontal = style == juce::Slider::LinearHorizontal
                           || style == juce::Slider::LinearBar;

    auto bounds = juce::Rectangle<int> (x, y, width, height).toFloat();

    if (isHorizontal)
    {
        auto track = bounds.withSizeKeepingCentre (bounds.getWidth(), 5.0f);
        g.setColour (surface);
        g.fillRoundedRectangle (track, 2.5f);

        g.setColour (accentDeep);
        g.fillRoundedRectangle (track.withWidth (juce::jmax (0.0f, sliderPos - track.getX())), 2.5f);

        g.setColour (accent);
        g.fillEllipse (sliderPos - 6.0f, track.getCentreY() - 6.0f, 12.0f, 12.0f);
    }
    else
    {
        auto track = bounds.withSizeKeepingCentre (5.0f, bounds.getHeight());
        g.setColour (surface);
        g.fillRoundedRectangle (track, 2.5f);

        const auto filledHeight = juce::jmax (0.0f, track.getBottom() - sliderPos);
        g.setColour (accentDeep);
        g.fillRoundedRectangle (track.withTop (track.getBottom() - filledHeight), 2.5f);

        g.setColour (accent);
        auto thumb = juce::Rectangle<float> (0.0f, 0.0f, track.getWidth() + 12.0f, 9.0f)
                        .withCentre ({ track.getCentreX(), sliderPos });
        g.fillRoundedRectangle (thumb, 3.0f);
    }
}

void NuxLookAndFeel::drawComboBox (juce::Graphics& g, int width, int height, bool,
                                  int, int, int, int, juce::ComboBox& box)
{
    auto bounds = juce::Rectangle<float> (0.0f, 0.0f, (float) width, (float) height).reduced (0.5f);

    g.setColour (surface);
    g.fillRoundedRectangle (bounds, 4.0f);

    g.setColour (box.isMouseOver() ? accent.withAlpha (0.6f) : outline);
    g.drawRoundedRectangle (bounds, 4.0f, 1.0f);

    juce::Path arrow;
    const auto cx = (float) width - 14.0f;
    const auto cy = (float) height * 0.5f;
    arrow.startNewSubPath (cx - 4.0f, cy - 2.0f);
    arrow.lineTo (cx, cy + 3.0f);
    arrow.lineTo (cx + 4.0f, cy - 2.0f);
    g.setColour (accent);
    g.strokePath (arrow, juce::PathStrokeType (1.8f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

juce::Font NuxLookAndFeel::getComboBoxFont (juce::ComboBox& box)
{
    return font::ui (juce::jlimit (11.0f, 15.0f, box.getHeight() * 0.42f));
}

juce::Font NuxLookAndFeel::getLabelFont (juce::Label& label)
{
    return font::ui (juce::jlimit (10.0f, 16.0f, (float) label.getHeight() * 0.55f));
}

juce::Font NuxLookAndFeel::getTextButtonFont (juce::TextButton&, int buttonHeight)
{
    return font::bold (juce::jlimit (10.0f, 14.0f, (float) buttonHeight * 0.38f));
}

juce::Font NuxLookAndFeel::getPopupMenuFont()
{
    return font::ui (13.0f);
}

void NuxLookAndFeel::drawPopupMenuBackground (juce::Graphics& g, int width, int height)
{
    auto bounds = juce::Rectangle<float> (0.0f, 0.0f, (float) width, (float) height);
    g.setColour (panelRaised);
    g.fillRoundedRectangle (bounds, 6.0f);
    g.setColour (outline);
    g.drawRoundedRectangle (bounds.reduced (0.5f), 6.0f, 1.0f);
}

void NuxLookAndFeel::drawScrollbar (juce::Graphics& g, juce::ScrollBar&, int x, int y,
                                   int width, int height, bool isScrollbarVertical,
                                   int thumbStartPosition, int thumbSize,
                                   bool isMouseOver, bool isMouseDown)
{
    juce::Rectangle<int> thumb;

    if (isScrollbarVertical)
        thumb = { x + width / 3, thumbStartPosition, juce::jmax (4, width / 3), thumbSize };
    else
        thumb = { thumbStartPosition, y + height / 3, thumbSize, juce::jmax (4, height / 3) };

    auto colour = surfaceHover;
    if (isMouseDown)      colour = accent.withAlpha (0.8f);
    else if (isMouseOver) colour = surfaceHover.brighter (0.2f);

    g.setColour (colour);
    g.fillRoundedRectangle (thumb.toFloat(), 3.0f);
}

void NuxLookAndFeel::drawTooltip (juce::Graphics& g, const juce::String& text, int width, int height)
{
    auto bounds = juce::Rectangle<float> (0.0f, 0.0f, (float) width, (float) height).reduced (0.5f);

    g.setColour (panelRaised);
    g.fillRoundedRectangle (bounds, 4.0f);
    g.setColour (outline);
    g.drawRoundedRectangle (bounds, 4.0f, 1.0f);

    g.setColour (textDim);
    g.setFont (font::ui (12.0f));
    g.drawFittedText (text, bounds.reduced (6.0f, 2.0f).toNearestInt(), juce::Justification::centredLeft, 2);
}
}
