/* NUX DAW - unit test runner.

   The tests exercise the parts of NUX that must be right regardless of the GUI:
   the project data model, the .nux serializer and the DSP building blocks.
   Run with:  ctest --test-dir build --output-on-failure    */
#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>
#include "UnitTestRunner.h"

int main()
{
    juce::ScopedJuceInitialiser_GUI juceInitialiser;

    juce::UnitTestRunner runner;
    runner.setAssertOnFailure (false);
    runner.runTestsInCategory ("NUX");

    int numFailures = 0;

    for (int i = 0; i < runner.getNumResults(); ++i)
        if (const auto* result = runner.getResult (i))
            numFailures += result->failures;

    if (numFailures == 0)
        std::cout << "\nAll NUX tests passed.\n" << std::endl;
    else
        std::cout << "\n" << numFailures << " NUX test assertion(s) failed.\n" << std::endl;

    return numFailures == 0 ? 0 : 1;
}
