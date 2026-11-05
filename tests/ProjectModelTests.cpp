#include <juce_core/juce_core.h>
#include "../src/nux_core/TrackModel.h"

namespace nux
{
class ProjectModelTests final : public juce::UnitTest
{
public:
    ProjectModelTests() : juce::UnitTest ("NUX project model", "NUX") {}

    void runTest() override
    {
        beginTest ("Default project is playable");
        {
            const auto project = ProjectState::createDefault();

            expectEquals ((int) project.tracks.size(), 3);
            expectWithinAbsoluteError (project.tempo, 120.0, 1.0e-9);
            expectEquals (project.patternLengthSteps, constants::defaultNumSteps);
            expect (! project.tracks.empty());

            // The starter groove must contain enabled kick steps, otherwise the app
            // would launch silently.
            int enabledSteps = 0;

            for (int i = 0; i < project.tracks[0].numSteps; ++i)
                if (project.tracks[0].steps[(size_t) i].enabled)
                    ++enabledSteps;

            expect (enabledSteps >= 4, "The default drum pattern should have at least four hits");
        }

        beginTest ("dB <-> linear gain conversion");
        {
            expectWithinAbsoluteError (dbToGain (0.0f), 1.0f, 0.0001f);
            expectWithinAbsoluteError (dbToGain (-6.0206f), 0.5f, 0.001f);
            expectWithinAbsoluteError (dbToGain (6.0206f), 2.0f, 0.001f);
            expectEquals (dbToGain (-60.0f), 0.0f);
            expectWithinAbsoluteError (gainToDb (1.0f), 0.0f, 0.0001f);
            expectWithinAbsoluteError (gainToDb (0.5f), -6.0206f, 0.01f);

            // Round trip across the useful range.
            for (float db = -48.0f; db <= 6.0f; db += 3.0f)
                expectWithinAbsoluteError (gainToDb (dbToGain (db)), db, 0.01f);
        }

        beginTest ("Track colour round trips through the packed ARGB value");
        {
            TrackState track;
            track.setColour (juce::Colour (0xff2ee6c4));
            expect (track.getColour() == juce::Colour (0xff2ee6c4));

            track.setColour (juce::Colour (0xff000000));
            expect (track.getColour() == juce::Colour (0xff000000));
        }

        beginTest ("Clip length arithmetic");
        {
            Clip clip;
            clip.startStep = 4;
            clip.lengthSteps = 16;
            expectEquals (clip.getEndStep(), 20);
        }

        beginTest ("Insert slot activation rules");
        {
            InsertSlot slot;
            expect (! slot.isActive());

            slot.type = InsertType::reverb;
            expect (slot.isActive());

            slot.bypassed = true;
            expect (! slot.isActive());
        }
    }
};

static ProjectModelTests projectModelTests;
}
