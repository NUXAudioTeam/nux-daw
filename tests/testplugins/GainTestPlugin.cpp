#include "GainTestPlugin.h"

/* The plugin factory must live in the global namespace: JUCE's VST3 wrapper looks
   for ::createPluginFilter(), not a namespaced one. */
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new nuxtest::GainTestPlugin();
}
