/* NUX DAW - reusable widgets shared by every view. */
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>
#include "Theme.h"

namespace nux
{
//==============================================================================
/** A labelled rotary control with a value readout underneath. */
class NuxKnob final : public juce::Component
{
public:
    NuxKnob (const juce::String& labelText, double minValue, double maxValue,
             double interval, double defaultValue);

    void resized() override;
    void paint (juce::Graphics&) override;

    void setValue (double newValue, juce::NotificationType notification = juce::dontSendNotification);
    double getValue() const { return slider.getValue(); }

    void setSuffix (const juce::String& s) { suffix = s; repaint(); }
    void setAccentColour (juce::Colour c) { accentColour = c; slider.repaint(); }
    void setKnobSize (int pixels) { knobSize = pixels; resized(); }
    void setShowLabel (bool shouldShow) { showLabel = shouldShow; resized(); }
    void setShowReadout (bool shouldShow) { showReadout = shouldShow; resized(); }

    std::function<void (double)> onValueChange;

    juce::Slider& getSlider() noexcept { return slider; }

private:
    void updateReadout();
    juce::String label;
    juce::String suffix;
    juce::Colour accentColour = theme::accent;
    juce::Slider slider;
    int knobSize = 46;
    bool showLabel = true;
    bool showReadout = true;
};

//==============================================================================
/** Vertical peak meter with a decaying peak-hold marker. */
class LevelMeter final : public juce::Component
{
public:
    explicit LevelMeter (bool isStereo = true);

    void paint (juce::Graphics&) override;
    void setLevel (float leftLinear, float rightLinear);
    void setHorizontal (bool shouldBeHorizontal) { horizontal = shouldBeHorizontal; repaint(); }

private:
    void paintBar (juce::Graphics&, juce::Rectangle<float> bounds, float linear);

    bool stereo = true;
    bool horizontal = false;
    float levelL = 0.0f, levelR = 0.0f;
    float holdL = 0.0f, holdR = 0.0f;
};

//==============================================================================
/** Oscilloscope fed by the engine's scope ring buffer. */
class ScopeView final : public juce::Component
{
public:
    ScopeView();
    void paint (juce::Graphics&) override;

    /** Supplies the most recent samples; call from a timer. */
    void setSamples (const float* samples, int count);
    void setAccentColour (juce::Colour c) { accentColour = c; repaint(); }

private:
    std::array<float, 512> buffer {};
    int numSamples = 0;
    juce::Colour accentColour = theme::accent;
};

//==============================================================================
/** Flat icon button used for transport controls and small toolbar actions. */
class IconButton final : public juce::Button
{
public:
    enum class Icon { play, stop, record, loop, metronome, back, forward, plus, minus, save };

    IconButton (Icon which, const juce::String& tooltipText);

    void paintButton (juce::Graphics&, bool isHighlighted, bool isDown) override;
    void setIconColour (juce::Colour c) { iconColour = c; repaint(); }
    void setFilled (bool shouldFill) { filled = shouldFill; repaint(); }

private:
    Icon icon;
    juce::Colour iconColour = theme::text;
    bool filled = true;
};

//==============================================================================
/** Small toggle chip (Mute / Solo / Rec). */
class ChipButton final : public juce::Button
{
public:
    ChipButton (const juce::String& text, juce::Colour onColour);

    void paintButton (juce::Graphics&, bool isHighlighted, bool isDown) override;

private:
    juce::Colour onColour;
};

//==============================================================================
/** Tab-style selector used for the main workspace views. */
class TabSelector final : public juce::Component
{
public:
    TabSelector();

    void setTabs (const juce::StringArray& labels);
    void setSelectedIndex (int index, bool notify = true);
    int getSelectedIndex() const noexcept { return selectedIndex; }

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;

    std::function<void (int)> onChange;

private:
    int indexAt (juce::Point<int> position) const;
    juce::StringArray tabLabels;
    int selectedIndex = 0;
    int hoverIndex = -1;
    int tabWidth = 120;
};

//==============================================================================
/** Draws a small NUX wordmark used in the header. */
void drawNuxWordmark (juce::Graphics&, juce::Rectangle<float> bounds, juce::Colour accentColour);
}
