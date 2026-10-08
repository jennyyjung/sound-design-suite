// soundsuite-render: run the plugin offline over an audio file.
//
//   soundsuite-render --in loop.wav --out out.wav [--gate_chop 0.5] [--width_pan 0.3]
//                     [--space 0] [--width_manual 100] [--sweep width_pan] [--block 512]
//
// Any parameter ID can be passed as --<id> <value> (macros 0..1, advanced
// parameters in their own units). --sweep <id> automates that parameter 0 -> 1
// across the file. Prints metrics as JSON on stdout, for tuning sessions and
// golden-render comparisons.

#include "PluginProcessor.h"
#include "analysis/Metrics.h"

#include <juce_audio_formats/juce_audio_formats.h>

#include <iostream>
#include <map>

namespace
{
int fail (const juce::String& message)
{
    std::cerr << message << std::endl;
    return 1;
}

juce::var metricsFor (const silo::metrics::Signal& l, const silo::metrics::Signal& r, double sr)
{
    using namespace silo::metrics;
    auto* obj = new juce::DynamicObject();
    obj->setProperty ("loudness_db",      loudnessDb (l, r));
    obj->setProperty ("mono_loudness_db", monoLoudnessDb (l, r));
    obj->setProperty ("correlation",      correlation (l, r));
    obj->setProperty ("low_correlation",  correlation (lowpass (l, 80.0, sr), lowpass (r, 80.0, sr)));
    obj->setProperty ("max_step",         maxStep (l) > maxStep (r) ? maxStep (l) : maxStep (r));
    return obj;
}
}

int main (int argc, char* argv[])
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    // "--name value" pairs
    std::map<juce::String, juce::String> args;
    for (int i = 1; i + 1 < argc; i += 2)
        args[juce::String (argv[i]).trimCharactersAtStart ("-")] = argv[i + 1];

    auto option = [&args] (const juce::String& name) { auto it = args.find (name); return it != args.end() ? it->second : juce::String(); };

    const auto cwd     = juce::File::getCurrentWorkingDirectory();
    const auto inFile  = cwd.getChildFile (option ("in"));
    const auto outPath = option ("out");
    if (option ("in").isEmpty() || outPath.isEmpty())
        return fail ("usage: soundsuite-render --in file.wav --out out.wav [--<param-id> value ...] [--sweep <param-id>]");

    juce::AudioFormatManager formats;
    formats.registerBasicFormats();

    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (inFile));
    if (reader == nullptr)
        return fail ("can't read " + inFile.getFullPathName());

    const double sr = reader->sampleRate;
    const auto   numSamples = (int) reader->lengthInSamples;
    const int    blockSize  = option ("block").isNotEmpty() ? option ("block").getIntValue() : 512;

    juce::AudioBuffer<float> audio (2, numSamples);
    reader->read (&audio, 0, numSamples, 0, true, true);   // mono files are copied to both sides

    SoundSuiteProcessor proc;
    proc.setPlayConfigDetails (2, 2, sr, blockSize);
    proc.prepareToPlay (sr, blockSize);

    auto& state = proc.getState();
    for (auto* p : proc.getParameters())
    {
        auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (p);
        const auto value = option (ranged->getParameterID());
        if (value.isNotEmpty())
            ranged->setValueNotifyingHost (ranged->convertTo0to1 (value.getFloatValue()));
    }

    juce::RangedAudioParameter* sweep = nullptr;
    if (option ("sweep").isNotEmpty())
        if ((sweep = state.getParameter (option ("sweep"))) == nullptr)
            return fail ("unknown parameter for --sweep");

    silo::metrics::Signal inL (audio.getReadPointer (0), audio.getReadPointer (0) + numSamples);
    silo::metrics::Signal inR (audio.getReadPointer (1), audio.getReadPointer (1) + numSamples);

    juce::MidiBuffer midi;
    for (int pos = 0; pos < numSamples; pos += blockSize)
    {
        const int n = juce::jmin (blockSize, numSamples - pos);
        if (sweep != nullptr)
            sweep->setValueNotifyingHost ((float) pos / (float) numSamples);

        juce::AudioBuffer<float> view (audio.getArrayOfWritePointers(), 2, pos, n);
        proc.processBlock (view, midi);
    }

    juce::File outFile = cwd.getChildFile (outPath);
    outFile.deleteFile();
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::OutputStream> stream = std::make_unique<juce::FileOutputStream> (outFile);
    auto writer = wav.createWriterFor (stream, juce::AudioFormatWriterOptions{}.withSampleRate (sr)
                                                                              .withNumChannels (2)
                                                                              .withBitsPerSample (24));
    if (writer == nullptr)
        return fail ("can't write " + outFile.getFullPathName());
    writer->writeFromAudioSampleBuffer (audio, 0, numSamples);

    silo::metrics::Signal outL (audio.getReadPointer (0), audio.getReadPointer (0) + numSamples);
    silo::metrics::Signal outR (audio.getReadPointer (1), audio.getReadPointer (1) + numSamples);

    auto* report = new juce::DynamicObject();
    report->setProperty ("input",  metricsFor (inL, inR, sr));
    report->setProperty ("output", metricsFor (outL, outR, sr));
    std::cout << juce::JSON::toString (juce::var (report)) << std::endl;
    return 0;
}
