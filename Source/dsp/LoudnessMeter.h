/*
    skoomaLoud - VST3 Loudness Meter
    License: GPL-3.0
*/

#pragma once

#include <array>
#include <vector>

namespace skloud {

// Single-channel ITU-R BS.1770-4 K-weighting filter
// (high-shelf "head" pre-filter + RLB high-pass), implemented as two
// transposed Direct Form II biquads designed via RBJ cookbook formulas.
class KWeightingFilter
{
public:
    void prepare(double sampleRate);
    void reset();
    float process(float x) noexcept;

private:
    struct Biquad {
        double b0{1.0}, b1{0.0}, b2{0.0}, a1{0.0}, a2{0.0};
        double z1{0.0}, z2{0.0};
        double process(double x) noexcept {
            double y = b0 * x + z1;
            z1 = b1 * x - a1 * y + z2;
            z2 = b2 * x - a2 * y;
            return y;
        }
        void reset() { z1 = z2 = 0.0; }
    };

    Biquad shelf;
    Biquad hpf;
};

// Short-term loudness meter: sums K-weighted squared samples across all
// channels into 100 ms blocks, holds the last 30 blocks (3 s window),
// reports loudness in LUFS via getShortTermLufs(). Also accumulates a
// long history of short-term values for Loudness Range (LRA) per BS.1770-4.
class ShortTermLoudnessMeter
{
public:
    void prepare(double sampleRate, int numChannels);
    void reset();
    // Clear LRA history without disturbing the short-term ring (used on
    // transport rising-edge so each playthrough gets a fresh range).
    void resetHistory() noexcept;

    // Process one block of interleaved-by-channel-pointer audio.
    // channelData[c] points to numSamples floats for channel c.
    void process(const float* const* channelData, int numChannels, int numSamples) noexcept;

    // Returns short-term loudness in LUFS, or -INFINITY if no data yet.
    float getShortTermLufs() const noexcept;

    // Returns Loudness Range (LRA) in LU per BS.1770-4: P95 − P10 of
    // short-term values gated by −70 LUFS absolute and −20 LU relative
    // to the ungated mean. Returns 0.0f when there isn't enough data
    // (need at least ~3 s of signal) — caller treats that as "hide".
    float getLoudnessRange() const noexcept;

private:
    static constexpr int kBlockMs       = 100;
    static constexpr int kWindowBlocks  = 30;     // 3 s short-term window
    static constexpr int kHistoryCap    = 36000;  // 1 hour @ 10 samples/s

    double sr{48000.0};
    int channels{2};
    int blockSize{4800};                         // samples per 100 ms block

    std::vector<KWeightingFilter> filters;       // one per channel
    double accumSq{0.0};                         // sum of K^2 across all channels
    int accumCount{0};                           // samples accumulated in current block

    std::array<double, kWindowBlocks> blockMs{}; // ring of mean-square per block
    int blockIdx{0};
    int blocksFilled{0};

    // LRA history: short-term LUFS sampled once per 100 ms block. Reserved
    // to kHistoryCap at prepare(); push_back is realtime-safe until full,
    // then we wrap (ring) so very long sessions reflect the most recent hour.
    std::vector<float> history;
    int historyCount{0};
    int historyWrite{0};
};

} // namespace skloud
