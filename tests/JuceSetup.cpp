// Starts JUCE (message manager etc.) for the whole test run and shuts it down
// before static destructors run. A global ScopedJuceInitialiser_GUI would be
// destroyed after JUCE's own statics and crash at exit.

#include <catch2/reporters/catch_reporter_event_listener.hpp>
#include <catch2/reporters/catch_reporter_registrars.hpp>

#include <juce_events/juce_events.h>

class JuceSetup : public Catch::EventListenerBase
{
public:
    using Catch::EventListenerBase::EventListenerBase;

    void testRunStarting (const Catch::TestRunInfo&) override { juce::initialiseJuce_GUI(); }
    void testRunEnded (const Catch::TestRunStats&) override   { juce::shutdownJuce_GUI(); }
};

CATCH_REGISTER_LISTENER (JuceSetup)
