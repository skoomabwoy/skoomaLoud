/*
    skoomaLoud - VST3 Loudness Meter
    License: GPL-3.0
*/

#include "LoudnessMeter.h"
#include <algorithm>
#include <cmath>
#include <limits>

namespace skloud {

namespace {
    constexpr double kPi = 3.14159265358979323846;

    // BS.1770-4 K-weighting parameters
    constexpr double kShelfF0 = 1681.974450955533;
    constexpr double kShelfQ  = 0.7071752369554196;
    constexpr double kShelfG  = 3.999843853973347;   // dB

    constexpr double kHpfF0 = 38.13547087602444;
    constexpr double kHpfQ  = 0.5003270373238773;
}

void KWeightingFilter::prepare(double sampleRate)
{
    // High-shelf (RBJ cookbook)
    {
        const double A    = std::pow(10.0, kShelfG / 40.0);
        const double w0   = 2.0 * kPi * kShelfF0 / sampleRate;
        const double cs   = std::cos(w0);
        const double sn   = std::sin(w0);
        const double alpha= sn / (2.0 * kShelfQ);
        const double sqA2 = 2.0 * std::sqrt(A) * alpha;

        const double b0 =      A * ((A + 1.0) + (A - 1.0) * cs + sqA2);
        const double b1 = -2.0*A * ((A - 1.0) + (A + 1.0) * cs);
        const double b2 =      A * ((A + 1.0) + (A - 1.0) * cs - sqA2);
        const double a0 =          (A + 1.0) - (A - 1.0) * cs + sqA2;
        const double a1 =  2.0  *  ((A - 1.0) - (A + 1.0) * cs);
        const double a2 =          (A + 1.0) - (A - 1.0) * cs - sqA2;

        shelf.b0 = b0 / a0;
        shelf.b1 = b1 / a0;
        shelf.b2 = b2 / a0;
        shelf.a1 = a1 / a0;
        shelf.a2 = a2 / a0;
    }
    // High-pass (RBJ cookbook)
    {
        const double w0    = 2.0 * kPi * kHpfF0 / sampleRate;
        const double cs    = std::cos(w0);
        const double sn    = std::sin(w0);
        const double alpha = sn / (2.0 * kHpfQ);

        const double b0 =  (1.0 + cs) * 0.5;
        const double b1 = -(1.0 + cs);
        const double b2 =  (1.0 + cs) * 0.5;
        const double a0 =   1.0 + alpha;
        const double a1 =  -2.0 * cs;
        const double a2 =   1.0 - alpha;

        hpf.b0 = b0 / a0;
        hpf.b1 = b1 / a0;
        hpf.b2 = b2 / a0;
        hpf.a1 = a1 / a0;
        hpf.a2 = a2 / a0;
    }
    reset();
}

void KWeightingFilter::reset()
{
    shelf.reset();
    hpf.reset();
}

float KWeightingFilter::process(float x) noexcept
{
    return static_cast<float>(hpf.process(shelf.process(static_cast<double>(x))));
}

// ---------------------------------------------------------------------------

void ShortTermLoudnessMeter::prepare(double sampleRate, int numChannels)
{
    sr        = sampleRate;
    channels  = std::max(1, numChannels);
    blockSize = std::max(1, static_cast<int>(std::round(sampleRate * (kBlockMs / 1000.0))));

    filters.assign(static_cast<size_t>(channels), KWeightingFilter{});
    for (auto& f : filters) f.prepare(sampleRate);

    history.reserve(static_cast<size_t>(kHistoryCap));

    reset();
}

void ShortTermLoudnessMeter::reset()
{
    for (auto& f : filters) f.reset();
    accumSq      = 0.0;
    accumCount   = 0;
    blockMs.fill(0.0);
    blockIdx     = 0;
    blocksFilled = 0;
    resetHistory();
}

void ShortTermLoudnessMeter::resetHistory() noexcept
{
    history.clear();
    historyCount = 0;
    historyWrite = 0;
}

void ShortTermLoudnessMeter::process(const float* const* channelData,
                                      int numChannels,
                                      int numSamples) noexcept
{
    const int ch = std::min(numChannels, channels);
    if (ch <= 0 || numSamples <= 0) return;

    for (int n = 0; n < numSamples; ++n)
    {
        double sumSq = 0.0;
        for (int c = 0; c < ch; ++c)
        {
            const float kw = filters[static_cast<size_t>(c)].process(channelData[c][n]);
            sumSq += static_cast<double>(kw) * static_cast<double>(kw);
        }
        accumSq += sumSq;
        if (++accumCount >= blockSize)
        {
            blockMs[static_cast<size_t>(blockIdx)] = accumSq / static_cast<double>(blockSize);
            blockIdx = (blockIdx + 1) % kWindowBlocks;
            if (blocksFilled < kWindowBlocks) ++blocksFilled;
            accumSq    = 0.0;
            accumCount = 0;

            // Append the latest short-term loudness to the LRA history. Only
            // store finite values (the >−70 LUFS gate is in getShortTermLufs).
            const float st = getShortTermLufs();
            if (std::isfinite(st))
            {
                if (historyCount < kHistoryCap)
                {
                    history.push_back(st);   // no realloc — reserved at prepare()
                    ++historyCount;
                }
                else
                {
                    history[static_cast<size_t>(historyWrite)] = st;
                    historyWrite = (historyWrite + 1) % kHistoryCap;
                }
            }
        }
    }
}

float ShortTermLoudnessMeter::getShortTermLufs() const noexcept
{
    if (blocksFilled <= 0)
        return -std::numeric_limits<float>::infinity();

    double sum = 0.0;
    for (int i = 0; i < blocksFilled; ++i)
        sum += blockMs[static_cast<size_t>(i)];
    const double mean = sum / static_cast<double>(blocksFilled);

    if (mean <= 1.0e-30)
        return -std::numeric_limits<float>::infinity();

    const double lufs = -0.691 + 10.0 * std::log10(mean);

    // BS.1770 absolute silence gate at -70 LUFS: below this we report
    // "no signal" so the display doesn't descend into the -100s on pause.
    if (lufs < -70.0)
        return -std::numeric_limits<float>::infinity();

    return static_cast<float>(lufs);
}

float ShortTermLoudnessMeter::getLoudnessRange() const noexcept
{
    // Need ≈ 3 s of signal before the percentiles mean anything. Below that,
    // tell the caller "no useful number yet".
    if (historyCount < 30) return 0.0f;

    // Copy out, apply BS.1770-4 gating, then percentile.
    std::vector<float> work;
    work.reserve(static_cast<size_t>(historyCount));
    for (int i = 0; i < historyCount; ++i)
    {
        const float v = history[static_cast<size_t>(i)];
        if (v >= -70.0f)         // absolute gate
            work.push_back(v);
    }
    if (work.size() < 30) return 0.0f;

    // Relative gate: drop blocks below (ungated mean − 20 LU). Mean is in
    // the loudness domain (LU/LUFS), per the standard.
    double sum = 0.0;
    for (float v : work) sum += v;
    const float relGate = static_cast<float>(sum / static_cast<double>(work.size())) - 20.0f;

    work.erase(std::remove_if(work.begin(), work.end(),
                              [relGate](float v) { return v < relGate; }),
               work.end());
    if (work.size() < 30) return 0.0f;

    std::sort(work.begin(), work.end());
    auto pct = [&work](double p) {
        const double idx = p * static_cast<double>(work.size() - 1);
        const size_t lo  = static_cast<size_t>(std::floor(idx));
        const size_t hi  = std::min(lo + 1, work.size() - 1);
        const double f   = idx - static_cast<double>(lo);
        return static_cast<float>(work[lo] * (1.0 - f) + work[hi] * f);
    };

    return pct(0.95) - pct(0.10);
}

} // namespace skloud
