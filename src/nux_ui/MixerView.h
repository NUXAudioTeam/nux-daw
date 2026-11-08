/* NUX DAW - the Mixer view: channel strips for every track, buses and master. */
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "../nux_core/ProjectDocument.h"
#include "../nux_engine/AudioEngine.h"
#include "Widgets.h"
#include "InsertEditorPanel.h"

namespace nux
{
/** One mixer channel strip. */
class ChannelStripComponent final : public juce::Component
{
public:
    enum class Kind { track, bus, master };

    ChannelStripComponent (ProjectDocument& document, AudioEngine& engine,
                           Kind kind, int index);

    void paint (juce::Graphics&) override;
    void resized() override;

    void refreshLevels();
    void syncFromState();
    void setSelected (bool shouldBeSelected);

    std::function<void()> onEdited;
    std::function<void (int)> onInsertClicked;   // insert slot index
    /** Double-clicking an insert opens the parameter editor for that slot. */
    std::function<void (int, juce::String)> onInsertDoubleClicked;

    /** Name shown in the parameter editor's title. */
    juce::String getStripDisplayName() const;

private:
    void showInsertMenu (int slotIndex);
    juce::String describeInsert (const InsertSlot& slot) const;

    ProjectDocument& document;
    AudioEngine& engine;
    Kind kind;
    int index;

    juce::Label nameLabel;
    juce::Slider fader;
    NuxKnob panKnob { "Pan", -1.0, 1.0, 0.01, 0.0 };
    ChipButton muteButton { "M", theme::warn };
    ChipButton soloButton { "S", theme::accent };
    LevelMeter meter { true };
    std::vector<std::unique_ptr<juce::TextButton>> insertButtons;
    bool selected = false;

    // Send levels for bus strips.
    NuxKnob delaySend { "Delay", 0.0, 1.0, 0.01, 0.0 };
    NuxKnob reverbSend { "Reverb", 0.0, 1.0, 0.01, 0.0 };
};

//==============================================================================
/** The mixer workspace: a horizontally scrolling bank of channel strips. */
class MixerView final : public juce::Component,
                        private juce::Timer
{
public:
    MixerView (ProjectDocument& document, AudioEngine& engine);
    ~MixerView() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    void rebuild();

    std::function<void()> onProjectEdited;

    /** Shows the parameter editor for one insert. Pass slotIndex < 0 to hide it. */
    void showInsertEditor (ChannelStripComponent::Kind kind, int stripIndex, int slotIndex,
                           const juce::String& stripName);
    void hideInsertEditor();

    /** The strip that the editor currently targets, or nullptr. */
    ChannelStripComponent* getEditorSourceStrip() const;

    /** The open parameter editor, or nullptr. */
    InsertEditorPanel* getInsertEditor() const noexcept { return insertEditor.get(); }

private:
    void timerCallback() override;

    ProjectDocument& document;
    AudioEngine& engine;

    juce::Viewport viewport;
    juce::Component stripHolder;
    std::vector<std::unique_ptr<ChannelStripComponent>> strips;

    juce::Label titleLabel;

    /** The insert parameter editor floats over the mixer, bottom right. */
    std::unique_ptr<InsertEditorPanel> insertEditor;
    ChannelStripComponent::Kind editorKind = ChannelStripComponent::Kind::track;
    int editorStripIndex = -1;
    int editorSlotIndex = -1;
};
}
