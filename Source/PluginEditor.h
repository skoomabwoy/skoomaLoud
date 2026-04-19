/*
    skoomaLoud - VST3 Loudness Meter
    License: GPL-3.0
*/

#pragma once

#include "PluginProcessor.h"

class SkoomaLoudEditor : public juce::AudioProcessorEditor,
                          private juce::Timer
{
public:
    explicit SkoomaLoudEditor(SkoomaLoudProcessor&);
    ~SkoomaLoudEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent&) override;

private:
    void timerCallback() override;

    SkoomaLoudProcessor& processor;

    juce::Font monoFont;
    std::unique_ptr<juce::Drawable> iconTheme;
    juce::ComponentBoundsConstrainer constrainer;

    static constexpr float kMinLufs = -40.0f;
    static constexpr float kMaxLufs =   0.0f;

    float displayLufs    = kMinLufs;       // raw (incl. -inf clamp for display only)
    float smoothedLufs   = kMinLufs;
    float needleVelocity = 0.0f;
    float displayLra     = 0.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SkoomaLoudEditor)
};
