/*
    skoomaLoud - VST3 Loudness Meter
    License: GPL-3.0
*/

#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <limits>

SkoomaLoudProcessor::SkoomaLoudProcessor()
    : AudioProcessor(BusesProperties()
                     .withInput("Input",  juce::AudioChannelSet::stereo(), true)
                     .withOutput("Output", juce::AudioChannelSet::stereo(), true))
{
}

bool SkoomaLoudProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    const auto& in  = layouts.getMainInputChannelSet();
    const auto& out = layouts.getMainOutputChannelSet();
    if (in != out) return false;
    return in == juce::AudioChannelSet::mono()
        || in == juce::AudioChannelSet::stereo();
}

void SkoomaLoudProcessor::prepareToPlay(double sampleRate, int /*samplesPerBlock*/)
{
    const int ch = juce::jmax(1, getTotalNumInputChannels());
    meter.prepare(sampleRate, ch);
    currentLufs.store(-std::numeric_limits<float>::infinity(), std::memory_order_release);
    currentLra.store(0.0f, std::memory_order_release);
    wasPlaying = false;
}

void SkoomaLoudProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    // Reset the LRA history at transport start (rising edge) so each
    // playthrough reports the dynamic range of just that take.
    if (auto* ph = getPlayHead())
    {
        if (auto pos = ph->getPosition())
        {
            const bool playing = pos->getIsPlaying();
            if (playing && !wasPlaying)
                meter.resetHistory();
            wasPlaying = playing;
        }
    }

    const int numCh = buffer.getNumChannels();
    const int numSamples = buffer.getNumSamples();
    if (numCh > 0 && numSamples > 0)
    {
        meter.process(buffer.getArrayOfReadPointers(), numCh, numSamples);
        currentLufs.store(meter.getShortTermLufs(), std::memory_order_release);
        currentLra.store(meter.getLoudnessRange(), std::memory_order_release);
    }
}

juce::AudioProcessorEditor* SkoomaLoudProcessor::createEditor()
{
    return new SkoomaLoudEditor(*this);
}

void SkoomaLoudProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    uint8_t dark = darkMode.load() ? 1 : 0;
    destData.append(&dark, sizeof(uint8_t));
}

void SkoomaLoudProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    if (sizeInBytes >= 1)
        darkMode.store(static_cast<const char*>(data)[0] != 0);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new SkoomaLoudProcessor();
}
