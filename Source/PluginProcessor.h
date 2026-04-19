/*
    skoomaLoud - VST3 Loudness Meter
    License: GPL-3.0
*/

#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "dsp/LoudnessMeter.h"
#include <atomic>

class SkoomaLoudProcessor : public juce::AudioProcessor
{
public:
    SkoomaLoudProcessor();
    ~SkoomaLoudProcessor() override = default;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported(const BusesLayout&) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }

    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    std::atomic<float> currentLufs{-std::numeric_limits<float>::infinity()};
    std::atomic<float> currentLra{0.0f};
    std::atomic<bool>  darkMode{true};

private:
    skloud::ShortTermLoudnessMeter meter;
    bool wasPlaying{false};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SkoomaLoudProcessor)
};
