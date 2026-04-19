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
    juce::Colour trackDim, trackBright;
    juce::Colour meterLow, meterMid, meterHigh, meterOff;
    juce::Colour labelText, valueText;
    juce::Colour toggleIcon;
};

const Theme darkTheme = {
    juce::Colour(0xff1a1a2e),
    juce::Colour(0xff555555),
    juce::Colour(0xffcccccc),
    juce::Colour(0xff00ff88),
    juce::Colour(0xffffaa00),
    juce::Colour(0xffff4444),
    juce::Colour(0xff444444),
    juce::Colour(0xff888888),
    juce::Colour(0xffaaaaaa),
    juce::Colour(0xff999999),
};

const Theme lightTheme = {
    juce::Colour(0xfff2f2f7),
    juce::Colour(0xffcccccc),
    juce::Colour(0xff555555),
    juce::Colour(0xff00aa55),
    juce::Colour(0xffdd8800),
    juce::Colour(0xffdd2222),
    juce::Colour(0xffcccccc),
    juce::Colour(0xff888888),
    juce::Colour(0xff666666),
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
        BinaryData::JetBrainsMonoBold_ttf,
        BinaryData::JetBrainsMonoBold_ttfSize);
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

    // LRA settles slowly; light one-pole smoothing keeps the readout steady
    // without lagging meaningful changes. Meter returns 0 when not enough data.
    const float lra = processor.currentLra.load(std::memory_order_acquire);
    displayLra += 0.2f * (lra - displayLra);

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

    // Loudness color zones, anchored to streaming-platform normalization targets
    // (Spotify/YouTube/Tidal -14 LUFS, Apple -16 LUFS): green is the band where
    // normalization is essentially free; red means the platform will turn you
    // down (squashed dynamics for nothing); yellow means quieter than targets.
    juce::Colour valColour;
    if (!std::isfinite(displayLufs))
        valColour = t.meterOff;
    else if (displayLufs < -18.0f)
        valColour = t.meterMid;     // yellow — below streaming targets
    else if (displayLufs < -12.0f)
        valColour = t.meterLow;     // green  — in the streaming-friendly band
    else
        valColour = t.meterHigh;    // red    — above targets, will be turned down

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
    // Unified text band across Tuner/Loud/Image: main at 0.76w, secondary at 0.89w,
    // placed below the arc endpoints (y ≈ 0.75w) so the needle sweep can't overlap.
    const float mainH  = 34.0f * scale;
    const float mainY  = 0.76f * w;
    const float labelH = 14.0f * scale;
    const float labelY = 0.89f * w;

    if (std::isfinite(displayLufs))
    {
        g.setColour(valColour);
        g.setFont(monoFont.withHeight(mainH));
        g.drawText(juce::String(displayLufs, 1),
                   juce::Rectangle<float>(0, mainY, w, mainH),
                   juce::Justification::centred, false);
    }

    // Loudness Range secondary readout: tells the producer if the mix is
    // over-compressed (red <4 LU), in the typical rock/jazz/funk band
    // (green 4–12 LU), or so dynamic it'll feel quiet on streaming
    // (yellow >12 LU). Hidden until the meter has enough data (returns 0).
    if (displayLra > 0.05f)
    {
        const juce::Colour lraColour =
            displayLra < 4.0f  ? t.meterHigh :
            displayLra > 12.0f ? t.meterMid  :
                                 t.meterLow;

        g.setColour(lraColour);
        g.setFont(monoFont.withHeight(labelH));
        g.drawText(juce::String(displayLra, 1) + " LU",
                   juce::Rectangle<float>(0, labelY, w, labelH),
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
