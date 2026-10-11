#include <juce_events/juce_events.h>
#include <juce_core/juce_core.h>
#include "core/Paths.h"

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;

    // Keep tests away from the real user folders.
    auto root = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("merge-tests");
    root.deleteRecursively();
    merge::paths::setOverrideRoot (root);

    juce::UnitTestRunner runner;
    runner.setAssertOnFailure (false);
    juce::String category = argc > 1 ? juce::String (argv[1]) : juce::String();
    if (category.isNotEmpty())
        runner.runTestsInCategory (category);
    else
        runner.runAllTests();

    int failures = 0;
    for (int i = 0; i < runner.getNumResults(); ++i)
        failures += runner.getResult (i)->failures;

    std::printf ("\n%s: %d failure(s)\n", failures == 0 ? "ALL TESTS PASSED" : "TESTS FAILED", failures);
    root.deleteRecursively();
    return failures == 0 ? 0 : 1;
}
