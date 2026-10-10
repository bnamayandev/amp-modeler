#include <catch2/catch_session.hpp>

#include <juce_events/juce_events.h>

int main (int argc, char* argv[])
{
    // The model manager reports status through ChangeBroadcaster, which needs a message manager.
    juce::ScopedJuceInitialiser_GUI juce;
    return Catch::Session().run (argc, argv);
}
