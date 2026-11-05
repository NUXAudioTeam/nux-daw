#include "Widgets.h"

namespace nux
{
//==============================================================================
NuxKnob::NuxKnob (const juce::String& labelText, double minValue, double maxValue,
                  double interval, double defaultValue)
    : label (labelText)
{
    slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    slider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    slider.setRange (minValue, maxValue, interval);
    slider.setValue (defaultValue, juce::dontSendNotification);
    slider.setRotaryParameters (juce::MathConstants<float>::pi * 1.15f,
                                juce::MathConstants<float>::pi * 2.85f, true);
    slider.setDoubleClickReturnValue (true, defaultValue);

    slider.onValueChange = [this]
    {
        updateReadout();
        if (onValueChange)
            onValueChange (slider.getValue());
    };

    addAndMakeVisible (slider);
    setInterceptsMouseClicks (false, true);
}

void NuxKnob::updateReadout()
{
    repaint();
}

void NuxKnob::setValue (double newValue, juce::NotificationType notification)
{
    slider.setValue (newValue, notification);
    repaint();
}

void NuxKnob::resized()
{
    auto area = getLocalBounds();
    if (showLabel) area.removeFromTop (12);

    slider.setBounds (area.withSizeKeepingCentre (knobSize, knobSize));
}

void NuxKnob::paint (juce::Graphics& g)
{
    auto area = getLocalBounds();

    if (showLabel)
    {
        g.setFont (theme::font::ui (11.0f));
        g.setColour (theme::textDim);
        g.drawFittedText (label.toUpperCase(), area.removeFromTop (13),
                          juce::Justification::centred, 1, 0.8f);
    }

    // Readout under the knob (skipped for compact controls in a toolbar).
    if (! showReadout)
        return;

    auto readout = area.removeFromBottom (13);
    g.setFont (theme::font::mono (11.0f));
    g.setColour (theme::text.withAlpha (0.9f));

    const auto value = slider.getValue();
    const auto text = std::abs (value) >= 1000.0 ? juce::String (value, 0)
                                                 : juce::String (value, (value == std::floor (value) ? 0 : 1));

    g.drawFittedText (text + suffix, readout, juce::Justification::centred, 1, 0.85f);
}

//==============================================================================
LevelMeter::LevelMeter (bool isStereo) : stereo (isStereo)
{
    setInterceptsMouseClicks (false, false);
}

void LevelMeter::setLevel (float leftLinear, float rightLinear)
{
    auto decay = [this] (float current, float next)
    {
        next = juce::jlimit (0.0f, 1.2f, next);
        return next >= current ? next : current * 0.88f;
    };

    const auto newL = decay (levelL, leftLinear);
    const auto newR = decay (levelR, rightLinear);

    const auto holdsL = decay (holdL, levelL);
    const auto holdsR = decay (holdR, rightLinear);

    const auto changed = std::abs (newL - levelL) > 0.002f || std::abs (newR - levelR) > 0.002f
                      || std::abs (holdsL - holdL) > 0.004f || std::abs (holdsR - holdR) > 0.004f;

    levelL = newL;
    levelR = newR;
    holdL = holdsL;
    holdR = holdsR;

    if (changed)
        repaint();
}

void LevelMeter::paintBar (juce::Graphics& g, juce::Rectangle<float> bounds, float linear)
{
    g.setColour (theme::panel);
    g.fillRoundedRectangle (bounds, 2.0f);

    if (linear <= 0.0001f)
        return;

    // Map amplitude to a dB scale between -60 and +6 dB.
    const auto db = 20.0f * std::log10 (juce::jmax (0.0001f, linear));
    const auto proportion = juce::jlimit (0.0f, 1.0f, (db + 60.0f) / 66.0f);

    juce::ColourGradient gradient (theme::accentDeep, bounds.getBottomLeft(),
                                   theme::recordRed, bounds.getTopLeft(), false);
    gradient.addColour (0.72, theme::accent);

    auto filled = horizontal
                    ? bounds.withWidth (bounds.getWidth() * proportion)
                    : bounds.withTop (bounds.getBottom() - bounds.getHeight() * proportion);

    g.setGradientFill (gradient);
    g.fillRoundedRectangle (filled, 2.0f);
}

void LevelMeter::paint (juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();

    if (horizontal)
    {
        const auto gap = 2.0f;
        const auto height = stereo ? (bounds.getHeight() - gap) * 0.5f : bounds.getHeight();

        paintBar (g, bounds.removeFromTop (height), levelL);

        if (stereo)
        {
            bounds.removeFromTop (gap);
            paintBar (g, bounds.removeFromTop (height), levelR);
        }
    }
    else
    {
        const auto gap = 2.0f;
        const auto width = stereo ? (bounds.getWidth() - gap) * 0.5f : bounds.getWidth();

        paintBar (g, bounds.removeFromLeft (width), levelL);

        if (stereo)
        {
            bounds.removeFromLeft (gap);
            paintBar (g, bounds.removeFromLeft (width), levelR);
        }
    }
}

//==============================================================================
ScopeView::ScopeView()
{
    setInterceptsMouseClicks (false, false);
    buffer.fill (0.0f);
}

void ScopeView::setSamples (const float* samples, int count)
{
    if (samples == nullptr || count <= 0)
        return;

    numSamples = juce::jmin (count, (int) buffer.size());
    std::copy (samples, samples + numSamples, buffer.begin());
    repaint();
}

void ScopeView::paint (juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();

    g.setColour (theme::windowBg.withAlpha (0.55f));
    g.fillRoundedRectangle (bounds, 4.0f);

    // Centre line.
    g.setColour (theme::gridLineStrong);
    g.drawHorizontalLine ((int) bounds.getCentreY(), bounds.getX(), bounds.getRight());

    if (numSamples < 2)
        return;

    juce::Path path;
    const auto width = bounds.getWidth();
    const auto centreY = bounds.getCentreY();
    const auto amplitude = bounds.getHeight() * 0.45f;

    for (int i = 0; i < numSamples; ++i)
    {
        const auto x = bounds.getX() + width * ((float) i / (float) (numSamples - 1));
        const auto y = centreY - juce::jlimit (-1.0f, 1.0f, buffer[(size_t) i] * 1.4f) * amplitude;

        if (i == 0) path.startNewSubPath (x, y);
        else        path.lineTo (x, y);
    }

    g.setColour (accentColour.withAlpha (0.9f));
    g.strokePath (path, juce::PathStrokeType (1.4f));
}

//==============================================================================
IconButton::IconButton (Icon which, const juce::String& tooltipText) : juce::Button (tooltipText), icon (which)
{
    setTooltip (tooltipText);
    setSize (34, 30);
}

void IconButton::paintButton (juce::Graphics& g, bool isHighlighted, bool isDown)
{
    auto bounds = getLocalBounds().toFloat().reduced (2.0f);
    const auto radius = bounds.getHeight() * 0.32f;

    auto background = theme::surface.withAlpha (0.85f);

    if (getToggleState())
        background = theme::accentDeep.withAlpha (0.85f);
    if (isDown)
        background = background.brighter (0.2f);
    else if (isHighlighted)
        background = background.brighter (0.1f);

    g.setColour (background);
    g.fillRoundedRectangle (bounds, radius);

    auto colour = getToggleState() ? theme::windowBg : iconColour;
    g.setColour (colour);

    auto centre = bounds.getCentre();
    const auto size = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.44f;

    switch (icon)
    {
        case Icon::play:
        {
            juce::Path triangle;
            triangle.addTriangle (centre.x - size * 0.5f, centre.y - size * 0.75f,
                                  centre.x - size * 0.5f, centre.y + size * 0.75f,
                                  centre.x + size * 0.85f, centre.y);
            if (filled) g.fillPath (triangle);
            else        g.strokePath (triangle, juce::PathStrokeType (1.6f));
            break;
        }

        case Icon::stop:
            g.fillRoundedRectangle (centre.x - size * 0.6f, centre.y - size * 0.6f,
                                    size * 1.2f, size * 1.2f, 2.0f);
            break;

        case Icon::record:
            g.setColour (getToggleState() ? theme::windowBg : theme::recordRed);
            g.fillEllipse (centre.x - size * 0.65f, centre.y - size * 0.65f, size * 1.3f, size * 1.3f);
            break;

        case Icon::loop:
        {
            juce::Path loop;
            const auto r = size * 0.75f;
            loop.addCentredArc (centre.x, centre.y, r, r, 0.0f,
                                juce::MathConstants<float>::pi * 0.25f,
                                juce::MathConstants<float>::pi * 1.75f, true);
            g.strokePath (loop, juce::PathStrokeType (1.8f));
            g.fillEllipse (centre.x + r * 0.55f, centre.y + r * 0.82f, 4.0f, 4.0f);
            break;
        }

        case Icon::metronome:
        {
            juce::Path met;
            met.startNewSubPath (centre.x - size * 0.5f, centre.y + size * 0.7f);
            met.lineTo (centre.x, centre.y - size * 0.85f);
            met.lineTo (centre.x + size * 0.5f, centre.y + size * 0.7f);
            met.closeSubPath();
            g.strokePath (met, juce::PathStrokeType (1.5f));
            break;
        }

        case Icon::back:
        {
            juce::Path p;
            p.addTriangle (centre.x - size * 0.5f, centre.y,
                           centre.x + size * 0.3f, centre.y - size * 0.7f,
                           centre.x + size * 0.3f, centre.y + size * 0.7f);
            g.fillPath (p);
            break;
        }

        case Icon::forward:
        {
            juce::Path p;
            p.addTriangle (centre.x + size * 0.5f, centre.y,
                           centre.x - size * 0.3f, centre.y - size * 0.7f,
                           centre.x - size * 0.3f, centre.y + size * 0.7f);
            g.fillPath (p);
            break;
        }

        case Icon::plus:
        case Icon::minus:
        {
            const auto thickness = 1.8f;
            g.fillRoundedRectangle (centre.x - size * 0.6f, centre.y - thickness * 0.5f,
                                    size * 1.2f, thickness, 1.0f);
            if (icon == Icon::plus)
                g.fillRoundedRectangle (centre.x - thickness * 0.5f, centre.y - size * 0.6f,
                                        thickness, size * 1.2f, 1.0f);
            break;
        }

        case Icon::save:
        {
            juce::Path p;
            auto r = juce::Rectangle<float> (centre.x - size * 0.65f, centre.y - size * 0.65f,
                                             size * 1.3f, size * 1.3f);
            p.addRectangle (r);
            g.strokePath (p, juce::PathStrokeType (1.5f));
            g.fillRect (r.removeFromBottom (size * 0.5f).reduced (size * 0.2f));
            break;
        }
    }
}

//==============================================================================
ChipButton::ChipButton (const juce::String& text, juce::Colour colour)
    : juce::Button (text), onColour (colour)
{
    setButtonText (text);
    setClickingTogglesState (true);
}

void ChipButton::paintButton (juce::Graphics& g, bool isHighlighted, bool isDown)
{
    auto bounds = getLocalBounds().toFloat().reduced (1.0f);
    const auto on = getToggleState();

    auto background = on ? onColour : theme::surface;
    if (isDown)            background = background.brighter (0.18f);
    else if (isHighlighted) background = background.brighter (0.08f);

    g.setColour (background);
    g.fillRoundedRectangle (bounds, 3.0f);

    if (! on)
    {
        g.setColour (theme::outline);
        g.drawRoundedRectangle (bounds.reduced (0.5f), 3.0f, 1.0f);
    }

    g.setFont (theme::font::bold (juce::jlimit (9.0f, 11.0f, bounds.getHeight() * 0.62f)));
    g.setColour (on ? theme::windowBg : theme::textDim);
    g.drawFittedText (getButtonText(), getLocalBounds(), juce::Justification::centred, 1, 0.8f);
}

//==============================================================================
TabSelector::TabSelector()
{
    setTabs ({ "MAIN", "MIXER", "PIANO ROLL", "PLUGINS", "MASTER" });
}

void TabSelector::setTabs (const juce::StringArray& labels)
{
    tabLabels = labels;
    repaint();
}

void TabSelector::setSelectedIndex (int index, bool notify)
{
    if (! juce::isPositiveAndBelow (index, tabLabels.size()))
        return;

    if (selectedIndex == index)
        return;

    selectedIndex = index;
    repaint();

    if (notify && onChange)
        onChange (selectedIndex);
}

void TabSelector::resized()
{
    if (tabLabels.isEmpty())
        return;

    tabWidth = juce::jlimit (70, 160, getWidth() / juce::jmax (1, tabLabels.size()));
}

int TabSelector::indexAt (juce::Point<int> position) const
{
    if (tabWidth <= 0)
        return -1;

    const auto index = position.x / tabWidth;
    return juce::isPositiveAndBelow (index, tabLabels.size()) ? index : -1;
}

void TabSelector::mouseDown (const juce::MouseEvent& e)
{
    const auto index = indexAt (e.getPosition());
    if (index >= 0)
        setSelectedIndex (index);
}

void TabSelector::mouseMove (const juce::MouseEvent& e)
{
    const auto index = indexAt (e.getPosition());
    if (index != hoverIndex)
    {
        hoverIndex = index;
        repaint();
    }
}

void TabSelector::mouseExit (const juce::MouseEvent&)
{
    hoverIndex = -1;
    repaint();
}

void TabSelector::paint (juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();

    g.setColour (theme::panel);
    g.fillRect (bounds);

    for (int i = 0; i < tabLabels.size(); ++i)
    {
        auto tab = juce::Rectangle<float> ((float) (i * tabWidth), 0.0f, (float) tabWidth, bounds.getHeight());

        if (i == selectedIndex)
        {
            g.setColour (theme::panelRaised);
            g.fillRect (tab);

            g.setColour (theme::accent);
            g.fillRect (tab.removeFromBottom (2.5f).reduced (8.0f, 0.0f));
        }
        else if (i == hoverIndex)
        {
            g.setColour (theme::surface.withAlpha (0.35f));
            g.fillRect (tab);
        }

        g.setFont (theme::font::bold (11.5f));
        g.setColour (i == selectedIndex ? theme::text : theme::textFaint);
        g.drawFittedText (tabLabels[i], juce::Rectangle<float> ((float) (i * tabWidth), 0.0f,
                                                               (float) tabWidth, bounds.getHeight()).toNearestInt(),
                          juce::Justification::centred, 1, 0.8f);
    }

    g.setColour (theme::outline);
    g.drawHorizontalLine ((int) bounds.getBottom() - 1, 0.0f, bounds.getRight());
}

//==============================================================================
void drawNuxWordmark (juce::Graphics& g, juce::Rectangle<float> bounds, juce::Colour accentColour)
{
    // A compact mark: a rounded tile holding three rising bars, then the wordmark.
    const auto tileSize = juce::jmin (bounds.getHeight(), 34.0f);
    auto tile = juce::Rectangle<float> (bounds.getX(), bounds.getCentreY() - tileSize * 0.5f,
                                        tileSize, tileSize);

    g.setColour (accentColour.withAlpha (0.14f));
    g.fillRoundedRectangle (tile, 7.0f);
    g.setColour (accentColour.withAlpha (0.55f));
    g.drawRoundedRectangle (tile.reduced (0.5f), 7.0f, 1.0f);

    const auto barWidth = tile.getWidth() * 0.13f;
    const auto gap = barWidth * 0.75f;
    const auto totalWidth = barWidth * 3.0f + gap * 2.0f;
    auto x = tile.getCentreX() - totalWidth * 0.5f;

    const float heights[3] = { 0.28f, 0.50f, 0.72f };

    for (int i = 0; i < 3; ++i)
    {
        const auto h = tile.getHeight() * heights[i];
        g.setColour (accentColour.withAlpha (i == 2 ? 1.0f : 0.6f));
        g.fillRoundedRectangle (x, tile.getCentreY() - h * 0.5f, barWidth, h, barWidth * 0.4f);
        x += barWidth + gap;
    }

    auto textArea = bounds.withTrimmedLeft (tile.getWidth() + 7.0f);

    if (textArea.getWidth() < 24.0f)
        return;

    g.setFont (theme::font::bold (juce::jlimit (12.0f, 17.0f, tileSize * 0.46f)));
    g.setColour (theme::text);
    g.drawFittedText ("NUX", textArea.removeFromTop (textArea.getHeight() * 0.58f).toNearestInt(),
                      juce::Justification::centredLeft, 1, 0.8f);

    g.setFont (theme::font::ui (juce::jlimit (8.0f, 10.0f, tileSize * 0.27f)));
    g.setColour (theme::textFaint);
    g.drawFittedText ("DAW", textArea.toNearestInt(), juce::Justification::centredLeft, 1, 0.8f);
}
}
