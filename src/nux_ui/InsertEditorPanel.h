/* NUX DAW - the insert parameter editor.

   Opens when an insert button on a mixer strip is double-clicked, and shows the
   three parameters that slot actually has, with the units and ranges that the DSP
   layer uses. The values are written back into the project model on every change
   and pushed into the running engine, so you hear the result immediately.

   A hosted plugin slot does not appear here: plugins draw their own editor, and
   the panel says so rather than showing meaningless knobs. */
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>
#include <memory>
#include <vector>

#include "../nux_core/InsertParameters.h"
#include "../nux_core/ProjectDocument.h"
#include "../nux_engine/AudioEngine.h"
#include "Theme.h"
#include "Widgets.h"

namespace nux
{
class InsertEditorPanel final : public juce::Component
{
public:
    /** Which insert of which strip is being edited. */
    struct Target
    {
        AudioEngine::StripKind kind = AudioEngine::StripKind::track;
        int stripIndex = 0;
        int slotIndex = 0;
        juce::String stripName;
    };

    InsertEditorPanel (ProjectDocument& document, AudioEngine& engine, Target target);
    ~InsertEditorPanel() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    /** Re-reads the slot from the project (e.g. after an undo). */
    void refreshFromState();

    /** Sets one parameter exactly as dragging its knob would, including the engine
        update. `value` is in descriptor units (0..100 for percent parameters). */
    void setParameterFromHost (int parameterIndex, double value);

    /** Reads the slot's current parameter values, in descriptor units. */
    std::vector<double> getParameterValues() const;

    /** Called after any edit so the host can refresh views and mark the project dirty. */
    std::function<void()> onEdited;

    /** True when the slot holds a built-in effect that can be edited here. */
    bool hasEditableParameters() const;

    void setCompact (bool shouldBeCompact);
    bool isCompact() const noexcept { return compact; }

private:
    InsertSlot* findSlot();
    void buildControlsForCurrentType();
    void applyValueToSlot (int parameterIndex, double newValue);
    juce::String getSlotTitle() const;

    ProjectDocument& document;
    AudioEngine& engine;
    Target target;

    InsertType currentType = InsertType::none;
    std::vector<InsertParameterDescriptor> descriptors;

    juce::Label titleLabel;
    juce::Label subtitleLabel;
    std::vector<std::unique_ptr<NuxKnob>> knobs;

    juce::TextButton bypassButton { "Bypass" };
    juce::TextButton resetButton { "Reset" };
    juce::TextButton pluginButton { "Open plugin..." };
    juce::Label emptyLabel;

    bool compact = false;
    bool updating = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (InsertEditorPanel)
};
}
