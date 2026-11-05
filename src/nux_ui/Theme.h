/* NUX DAW - dark studio theme and custom look and feel. */
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace nux::theme
{
//==============================================================================
// Palette: a near-black studio background with teal accents.
inline const juce::Colour windowBg      { 0xff0b0f14 };
inline const juce::Colour panel          { 0xff121820 };
inline const juce::Colour panelRaised    { 0xff1a222c };
inline const juce::Colour surface        { 0xff222c38 };
inline const juce::Colour surfaceHover   { 0xff2b3746 };
inline const juce::Colour outline        { 0xff2d3948 };
inline const juce::Colour text           { 0xffe7eef6 };
inline const juce::Colour textDim        { 0xff8fa0b4 };
inline const juce::Colour textFaint      { 0xff5d6d80 };
inline const juce::Colour accent         { 0xff2ee6c4 };
inline const juce::Colour accentDeep     { 0xff17a58f };
inline const juce::Colour accentGlow     { 0x332ee6c4 };
inline const juce::Colour recordRed      { 0xffff4d5e };
inline const juce::Colour playGreen      { 0xff3ddc97 };
inline const juce::Colour warn           { 0xffffb454 };
inline const juce::Colour trackDrums     { 0xff2ee6c4 };
inline const juce::Colour trackBass      { 0xff7c9cff };
inline const juce::Colour trackAudio     { 0xffffb454 };
inline const juce::Colour gridLine       { 0xff1e2734 };
inline const juce::Colour gridLineStrong { 0xff2b3746 };

//==============================================================================
namespace font
{
inline juce::Font ui (float height)          { return juce::Font (juce::FontOptions { height }); }
inline juce::Font bold (float height)        { return juce::Font (juce::FontOptions { height }.withStyle ("Bold")); }
inline juce::Font mono (float height)        { return juce::Font (juce::FontOptions { juce::Font::getDefaultMonospacedFontName(), height, juce::Font::plain }); }
}

/** Standard sizes used across the application so everything lines up. */
namespace metrics
{
inline constexpr int transportBarHeight = 62;
inline constexpr int tabBarHeight       = 40;
inline constexpr int statusBarHeight    = 26;
inline constexpr int trackHeaderWidth   = 214;
inline constexpr int mixerStripWidth    = 92;
inline constexpr int rulerHeight        = 26;
inline constexpr float cornerRadius     = 6.0f;
}

//==============================================================================
/** The NUX look and feel: flat dark surfaces, teal highlights, rounded corners. */
class NuxLookAndFeel final : public juce::LookAndFeel_V4
{
public:
    NuxLookAndFeel();

    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour& backgroundColour,
                               bool shouldDrawButtonAsHighlighted,
                               bool shouldDrawButtonAsDown) override;

    void drawButtonText (juce::Graphics&, juce::TextButton&,
                         bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown) override;

    void drawRotarySlider (juce::Graphics&, int x, int y, int width, int height,
                           float sliderPosProportional, float rotaryStartAngle,
                           float rotaryEndAngle, juce::Slider&) override;

    void drawLinearSlider (juce::Graphics&, int x, int y, int width, int height,
                           float sliderPos, float minSliderPos, float maxSliderPos,
                           juce::Slider::SliderStyle, juce::Slider&) override;

    void drawComboBox (juce::Graphics&, int width, int height, bool isButtonDown,
                       int buttonX, int buttonY, int buttonW, int buttonH, juce::ComboBox&) override;

    juce::Font getComboBoxFont (juce::ComboBox&) override;
    juce::Font getLabelFont (juce::Label&) override;
    juce::Font getTextButtonFont (juce::TextButton&, int buttonHeight) override;
    juce::Font getPopupMenuFont() override;

    void drawPopupMenuBackground (juce::Graphics&, int width, int height) override;

    void drawScrollbar (juce::Graphics&, juce::ScrollBar&, int x, int y, int width, int height,
                        bool isScrollbarVertical, int thumbStartPosition, int thumbSize,
                        bool isMouseOver, bool isMouseDown) override;

    void drawTooltip (juce::Graphics&, const juce::String& text, int width, int height) override;

    /** Helpers shared by the custom components. */
    static void drawPanel (juce::Graphics&, juce::Rectangle<float> bounds, juce::Colour fill,
                           juce::Colour border, float radius = metrics::cornerRadius);

    static void drawGlow (juce::Graphics&, juce::Rectangle<float> bounds, juce::Colour colour, float radius = metrics::cornerRadius);
};
}
