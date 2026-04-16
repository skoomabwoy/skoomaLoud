/*
    skoomaLoud - VST3 Loudness Meter
    License: GPL-3.0
*/

#include "PluginEditor.h"
#include "BinaryData.h"
#include <cmath>
#include <limits>

namespace {

struct Theme {
    juce::Colour background;
    juce::Colour trackDim, trackBright, accent;
    juce::Colour meterLow, meterMid, meterHigh, meterOff;
    juce::Colour labelText, valueText;
    juce::Colour toggleBg, toggleBorder, toggleIcon;
};

const Theme darkTheme = {
    juce::Colour(0xff1a1a2e),
    juce::Colour(0xff555555),
    juce::Colour(0xffcccccc),
    juce::Colour(0xff00ff88),
    juce::Colour(0xff00ff88),
    juce::Colour(0xffffaa00),
    juce::Colour(0xffff4444),
    juce::Colour(0xff444444),
    juce::Colour(0xff888888),
    juce::Colour(0xffaaaaaa),
    juce::Colour(0xff2a2a3e),
    juce::Colour(0xff444455),
    juce::Colour(0xff999999),
};

const Theme lightTheme = {
    juce::Colour(0xfff2f2f7),
    juce::Colour(0xffcccccc),
    juce::Colour(0xff555555),
    juce::Colour(0xff00aa55),
    juce::Colour(0xff00aa55),
    juce::Colour(0xffdd8800),
    juce::Colour(0xffdd2222),
    juce::Colour(0xffcccccc),
    juce::Colour(0xff888888),
    juce::Colour(0xff666666),
    juce::Colour(0xffe0e0ea),
    juce::Colour(0xffbbbbcc),
    juce::Colour(0xff777777),
};

void drawIcon(juce::Graphics& g, const juce::Drawable* src,
              juce::Rectangle<float> rect, juce::Colour colour)
{
    if (src == nullptr) return;
    auto d = src->createCopy();
    d->replaceColour(juce::Colours::black, colour);
    d->drawWithin(g, rect, juce::RectanglePlacement::centred, 1.0f);
}

} // anonymous namespace

SkoomaLoudEditor::SkoomaLoudEditor(SkoomaLoudProcessor& p)
    : AudioProcessorEditor(&p), processor(p)
{
    auto typeface = juce::Typeface::createSystemTypefaceFor(
        BinaryData::JetBrainsMonoBoldsubset_ttf,
        BinaryData::JetBrainsMonoBoldsubset_ttfSize);
    monoFont = juce::Font(juce::FontOptions(typeface));

    iconTheme = juce::Drawable::createFromImageData(BinaryData::theme_svg, BinaryData::theme_svgSize);

    constrainer.setFixedAspectRatio(1.0);
    constrainer.setMinimumSize(200, 200);
    constrainer.setMaximumSize(800, 800);
    setConstrainer(&constrainer);
    setResizable(true, true);
    setSize(300, 300);

    startTimerHz(30);
}

SkoomaLoudEditor::~SkoomaLoudEditor()
{
    stopTimer();
}

void SkoomaLoudEditor::timerCallback()
{
    float lufs = processor.currentLufs.load(std::memory_order_acquire);

    // For display, clamp -inf to bottom of scale
    float target = std::isfinite(lufs)
        ? std::clamp(lufs, kMinLufs, kMaxLufs)
        : kMinLufs;
    displayLufs = lufs;

    // Spring-damper needle physics (slightly underdamped) — same feel as Tuner
    constexpr float dt = 1.0f / 30.0f;
    constexpr float springK = 150.0f;
    constexpr float damping = 20.0f;
    float force = -springK * (smoothedLufs - target) - damping * needleVelocity;
    needleVelocity += force * dt;
    smoothedLufs   += needleVelocity * dt;

    repaint();
}

void SkoomaLoudEditor::paint(juce::Graphics& g)
{
    const bool dark = processor.darkMode.load();
    const auto& t = dark ? darkTheme : lightTheme;

    float w = static_cast<float>(getWidth());
    float scale = w / 300.0f;

    g.fillAll(t.background);

    float cx = w * 0.5f;
    float cy = w * 0.48f;
    float radius = w * 0.38f;

    // Loudness color zones (per user: quiet < -14, average -14..-8, hot > -8)
    juce::Colour valColour;
    if (!std::isfinite(displayLufs))
        valColour = t.meterOff;
    else if (displayLufs < -14.0f)
        valColour = t.meterLow;
    else if (displayLufs < -8.0f)
        valColour = t.meterMid;
    else
        valColour = t.meterHigh;

    // --- Gauge ---
    float arcStart = juce::MathConstants<float>::pi * 0.75f;
    float arcSpan  = juce::MathConstants<float>::pi * 1.5f;

    // Ticks every 1 dB minor, every 5 dB major
    for (int dB = static_cast<int>(kMinLufs); dB <= static_cast<int>(kMaxLufs); ++dB)
    {
        float frac = (static_cast<float>(dB) - kMinLufs) / (kMaxLufs - kMinLufs);
        float angle = arcStart + frac * arcSpan;
        float cosA = std::cos(angle);
        float sinA = std::sin(angle);

        bool isMajor = (dB % 5 == 0);
        float innerR = radius * (isMajor ? 0.78f : 0.85f);

        float x1 = cx + innerR * cosA;
        float y1 = cy + innerR * sinA;
        float x2 = cx + radius * cosA;
        float y2 = cy + radius * sinA;

        if (isMajor)
        {
            g.setColour(t.trackBright);
            g.drawLine(x1, y1, x2, y2, 1.5f * scale);
        }
        else
        {
            g.setColour(t.trackDim);
            g.drawLine(x1, y1, x2, y2, 1.0f * scale);
        }
    }

    // Endpoint labels
    g.setFont(monoFont.withHeight(11.0f * scale));
    g.setColour(t.labelText);
    for (int val : { static_cast<int>(kMinLufs), static_cast<int>(kMaxLufs) })
    {
        float frac = (static_cast<float>(val) - kMinLufs) / (kMaxLufs - kMinLufs);
        float angle = arcStart + frac * arcSpan;
        float labelR = radius * 0.65f;
        float lx = cx + labelR * std::cos(angle);
        float ly = cy + labelR * std::sin(angle);

        juce::String labelText = juce::String(val);
        float lw = 36.0f * scale;
        float lh = 14.0f * scale;
        g.drawText(labelText, juce::Rectangle<float>(lx - lw * 0.5f, ly - lh * 0.5f, lw, lh),
                   juce::Justification::centred, false);
    }

    // --- Needle ---
    float needleVal = std::clamp(smoothedLufs, kMinLufs, kMaxLufs);
    float needleFrac = (needleVal - kMinLufs) / (kMaxLufs - kMinLufs);
    float needleAngle = arcStart + needleFrac * arcSpan;

    float nx = cx + radius * 0.92f * std::cos(needleAngle);
    float ny = cy + radius * 0.92f * std::sin(needleAngle);

    g.setColour(valColour);
    g.drawLine(cx, cy, nx, ny, 2.0f * scale);

    float dotR = 5.0f * scale;
    g.fillEllipse(cx - dotR, cy - dotR, dotR * 2, dotR * 2);

    // --- Toggle: theme (top-right, hover-only background) ---
    float iconSize = 33.0f * scale;
    float iconPad  = 8.0f * scale;
    juce::Rectangle<float> themeRect(w - iconSize - iconPad, iconPad, iconSize, iconSize);

    if (isMouseOver(false)) {
        auto mp = getMouseXYRelative().toFloat();
        if (themeRect.contains(mp)) {
            g.setColour(t.toggleIcon.withAlpha(0.15f));
            g.fillRoundedRectangle(themeRect, 3.0f * scale);
        }
    }
    drawIcon(g, iconTheme.get(), themeRect.reduced(iconSize * 0.2f), t.toggleIcon);

    // --- Numeric readout (only when signal present) ---
    if (std::isfinite(displayLufs))
    {
        float valH = 34.0f * scale;
        float valY = cy + 28.0f * scale;

        g.setColour(valColour);
        g.setFont(monoFont.withHeight(valH));
        g.drawText(juce::String(displayLufs, 1),
                   juce::Rectangle<float>(0, valY, w, valH * 1.1f),
                   juce::Justification::centred, false);

        float labY = valY + valH * 1.15f;
        float labH = 14.0f * scale;
        g.setColour(t.valueText);
        g.setFont(monoFont.withHeight(labH));
        g.drawText("LUFS",
                   juce::Rectangle<float>(0, labY, w, labH * 1.3f),
                   juce::Justification::centred, false);
    }
}

void SkoomaLoudEditor::resized()
{
}

void SkoomaLoudEditor::mouseDown(const juce::MouseEvent& e)
{
    float w = static_cast<float>(getWidth());
    float scale = w / 300.0f;
    float iconSize = 33.0f * scale;
    float iconPad  = 8.0f * scale;

    juce::Rectangle<float> themeRect(w - iconSize - iconPad, iconPad, iconSize, iconSize);
    if (themeRect.contains(e.position))
    {
        processor.darkMode.store(!processor.darkMode.load());
        repaint();
    }
}
