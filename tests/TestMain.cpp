/* NUX DAW - unit test runner.

   The tests exercise the parts of NUX that must be right regardless of the GUI:
   the project data model, the .nux serializer, the DSP building blocks, and the
   measurement maths behind the Virtual Audio Lab.

   Run everything:        ctest --test-dir build --output-on-failure
   List the test groups:  NUXTests --list
   Run one group:         NUXTests --only="measurements"                          */
#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>
#include <iostream>
#include "UnitTestRunner.h"

namespace
{
struct Options
{
    juce::String only;
    juce::String category = "NUX";
    bool list = false;
};

Options parseOptions (int argc, char** argv)
{
    Options options;

    for (int i = 1; i < argc; ++i)
    {
        const juce::String arg (argv[i]);

        if (arg == "--list")
            options.list = true;
        else if (arg.startsWith ("--only="))
            options.only = arg.fromFirstOccurrenceOf ("=", false, false).unquoted();
        else if (arg.startsWith ("--category="))
            options.category = arg.fromFirstOccurrenceOf ("=", false, false).unquoted();
    }

    return options;
}
}

int main (int argc, char** argv)
{
    // Unbuffered output. When a test crashes, the last line printed names the test that
    // was running, which is the only way to bisect a segfault on a CI runner you cannot
    // attach a debugger to. Without this, Windows CI reported nothing but "SegFault",
    // because the block-buffered stdout died with the process.
    std::cout << std::unitbuf;
    std::cerr << std::unitbuf;

    const auto options = parseOptions (argc, argv);

    juce::ScopedJuceInitialiser_GUI juceInitialiser;

    auto tests = juce::UnitTest::getTestsInCategory (options.category);

    if (options.list)
    {
        for (const auto* test : tests)
            std::cout << test->getName() << "\n";

        return 0;
    }

    if (options.only.isNotEmpty())
    {
        juce::Array<juce::UnitTest*> filtered;

        for (auto* test : tests)
            if (test->getName().containsIgnoreCase (options.only))
                filtered.add (test);

        if (filtered.isEmpty())
        {
            std::cerr << "No test group in category '" << options.category
                      << "' matches '" << options.only << "'. Try --list.\n";
            return 2;
        }

        tests = filtered;
    }

    std::cout << "Running " << tests.size() << " test group(s) in category '"
              << options.category << "'\n";

    juce::UnitTestRunner runner;
    runner.setAssertOnFailure (false);
    runner.runTests (tests);

    int numFailures = 0;

    for (int i = 0; i < runner.getNumResults(); ++i)
        if (const auto* result = runner.getResult (i))
            numFailures += result->failures;

    std::cout << "\nRan " << tests.size() << " group(s).\n";

    if (numFailures == 0)
        std::cout << "All NUX tests passed.\n";
    else
        std::cout << numFailures << " NUX test assertion(s) failed.\n";

    std::cout.flush();

    return numFailures == 0 ? 0 : 1;
}
