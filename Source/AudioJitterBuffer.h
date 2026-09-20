#pragma once

#include "EmbeddedEmulatorEngine.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <limits>
#include <vector>

// Sits between MAME's audio queue and the audio device callback.
//
// MAME's sound manager delivers audio in bursts (STREAMS_UPDATE_FREQUENCY = 50 Hz,
// i.e. 882 frames every 20 ms at 44.1 kHz), paced by its own throttle, while the
// device pulls fixed blocks on its own clock.  Reading the queue directly, a
// 512-frame callback often finds fewer frames than it needs and the gap is played
// as silence: that is the crackle heard with large buffers.
//
// Measured on the CZ-101 at 44.1 kHz / 512: long-term both clocks agree, but a
// MAME burst sometimes arrives several ms late.  So this buffer:
//  - starts playing once one MAME burst + one device block + a safety margin is queued;
//  - grows the margin after every underrun, so it settles at what MAME actually needs;
//  - after an underrun leaves one block silent and re-primes instead of chopping;
//  - watches the leanest queue level over 4 s windows and only nudges the playback
//    ratio (+-500 ppm, 0.9 cent) when clearly off target.  Otherwise samples are
//    copied 1:1, bit exact.
class AudioJitterBuffer
{
public:
    void prepare (double sampleRate, int maxBlockSize)
    {
        rate = sampleRate > 0.0 ? sampleRate : 44100.0;
        local.assign (static_cast<size_t> (std::max (16384, maxBlockSize * 4 + 16)), {});
        floorMargin = baseMargin (maxBlockSize);
        margin = floorMargin;
        reset();
    }

    // Audio thread only.
    void reset()
    {
        count = 0;
        pos = 0.0;
        primed = false;
        ratio = targetRatio = 1.0;
        windowFrames = 0.0;
        windowMinSurplus = std::numeric_limits<double>::max();
    }

    // Renders numSamples frames; returns false when the block was left silent.
    bool render (ves::EmbeddedEmulatorEngine& engine, float* left, float* right, int numSamples)
    {
        if (numSamples <= 0)
            return false;

        const auto burst = std::max (static_cast<double> (engine.diagnostics().mame_audio_callback_block_size.load()), rate / 50.0);
        const auto primeLevel = burst + numSamples + margin;
        lastPrimeLevel = primeLevel;

        auto queued = static_cast<double> (engine.queuedAudioFrames());
        auto available = queued + static_cast<double> (count) - pos;

        if (! primed)
        {
            publish (available);
            if (available < primeLevel)
                return false;

            // Start at the prime level, not at whatever happened to be queued.
            // MAME runs free and may have produced well past the prime level by
            // the time the first block is asked for; keeping that head start
            // made the delay a lottery -- measured queue depths of 20, 25, 26
            // and 41 ms across runs of the same build, with nothing else
            // changed.  The excess here has never been played, so dropping it
            // costs nothing and makes the latency the same every time.
            if (available > primeLevel && queued > 0.0)
            {
                const auto excess = std::min (queued, available - primeLevel);
                engine.discardOldestAudioFrames (static_cast<size_t> (excess));
                queued = static_cast<double> (engine.queuedAudioFrames());
                available = queued + static_cast<double> (count) - pos;
            }

            primed = true;
            windowFrames = 0.0;
            windowMinSurplus = std::numeric_limits<double>::max();
        }

        // Host stalled (window dragged, device paused...): drop back to the prime
        // level.  The old trigger was a quarter of a second, which is five times
        // the whole latency budget: anything that big had long since been felt as
        // delay on every note.  Four bursts is still far outside normal jitter.
        if (available > primeLevel + 4.0 * burst)
        {
            const auto excess = std::min (queued, available - primeLevel);
            engine.discardOldestAudioFrames (static_cast<size_t> (excess));
            discards.fetch_add (1);
            queued = static_cast<double> (engine.queuedAudioFrames());
            available = queued + static_cast<double> (count) - pos;
        }

        // Frames needed locally for the last interpolation point of this block.
        const auto lastPos = pos + (numSamples - 1) * std::max (ratio, 1.0) + 1.0;
        const auto required = static_cast<size_t> (lastPos) + 3;
        if (required > local.size())
            local.resize (required * 2);

        if (required > count)
        {
            const auto need = required - count;
            if (static_cast<double> (need) > queued)
                return underrun (numSamples, available);

            count += engine.readAudioFrames (local.data() + count, need);
            if (count < required)
                return underrun (numSamples, available);
        }

        windowMinSurplus = std::min (windowMinSurplus, available - numSamples * ratio);

        for (int i = 0; i < numSamples; ++i)
        {
            const auto index = static_cast<size_t> (pos);
            const auto t = static_cast<float> (pos - static_cast<double> (index));
            const auto& y1 = local[index];

            if (t == 0.0f)
            {
                left[i] = y1.left;
                right[i] = y1.right;
            }
            else
            {
                const auto& y0 = local[index > 0 ? index - 1 : 0];
                const auto& y2 = local[index + 1];
                const auto& y3 = local[index + 2];
                left[i] = hermite (y0.left, y1.left, y2.left, y3.left, t);
                right[i] = hermite (y0.right, y1.right, y2.right, y3.right, t);
            }

            pos += ratio;

            // Returning to 1:1: wait until the fractional phase is back on a sample.
            if (ratio != 1.0 && targetRatio == 1.0)
            {
                const auto frac = pos - std::floor (pos);
                const auto eps = std::abs (ratio - 1.0);
                if (frac < eps || 1.0 - frac < eps)
                {
                    pos = std::round (pos);
                    ratio = 1.0;
                }
            }
        }

        // Keep one frame of history before the read position.
        const auto drop = static_cast<size_t> (pos) > 1 ? static_cast<size_t> (pos) - 1 : 0;
        if (drop > 0)
        {
            const auto remaining = count > drop ? count - drop : 0;
            if (remaining > 0)
                std::memmove (local.data(), local.data() + drop, remaining * sizeof (ves::StereoFrame));
            count = remaining;
            pos -= static_cast<double> (drop);
        }

        updateController (numSamples, burst);
        publish (static_cast<double> (engine.queuedAudioFrames()) + static_cast<double> (count) - pos);
        return true;
    }

    // Any thread.
    //
    // What the host has to be told: the steady-state depth this buffer keeps
    // between MAME's queue and the device callback.  It is not a constant -- the
    // margin moves with how lumpy the host turns out to be -- so it is published
    // and read, not computed twice.
    double getPrimeFrames() const { return primeFrames.load(); }
    double getLatencyMs() const { return latencyFrames.load() * 1000.0 / rate; }
    double getMarginMs() const { return marginFrames.load() * 1000.0 / rate; }
    int getRatioPpm() const { return ratioPpm.load(); }
    uint64_t getUnderruns() const { return underruns.load(); }
    uint64_t getDiscards() const { return discards.load(); }

private:
    double baseMargin (int blockSize) const
    {
        return std::max (static_cast<double> (blockSize) * 0.5, rate * 0.005);
    }

    bool underrun (int numSamples, double available)
    {
        primed = false;
        underruns.fetch_add (1);
        // Late MAME bursts are bigger than the margin: widen it (capped at 80 ms).
        margin = std::min (margin * 1.5 + numSamples * 0.5, rate * 0.08);
        publish (available);
        return false;
    }

    void publish (double availableFrames)
    {
        latencyFrames.store (static_cast<float> (availableFrames));
        primeFrames.store (static_cast<float> (lastPrimeLevel));
        marginFrames.store (static_cast<float> (margin));
        ratioPpm.store (static_cast<int> (std::lround ((ratio - 1.0) * 1.0e6)));
    }

    static float hermite (float y0, float y1, float y2, float y3, float t)
    {
        const auto c1 = 0.5f * (y2 - y0);
        const auto c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
        const auto c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
        return ((c3 * t + c2) * t + c1) * t + y1;
    }

    void updateController (int numSamples, double burst)
    {
        windowFrames += numSamples;
        if (windowFrames < rate * 4.0)
            return;

        // windowMinSurplus is what was left in the queue at the leanest moment of
        // the window, i.e. just before a late MAME burst arrived.  In steady state
        // it sits near the margin; only react when it is clearly off.
        const auto error = windowMinSurplus - margin;
        if (error > burst + margin)
            targetRatio = 1.0005;  // a whole burst of extra latency: drain it slowly
        else if (error < -0.5 * margin)
            targetRatio = 0.9995;  // running lean: refill slowly
        else
            targetRatio = 1.0;

        // The margin grows on every underrun and used to stay there: one rough
        // patch at start-up -- which is exactly when a DAW is busiest -- left the
        // buffer holding up to 80 ms for the rest of the session, and that delay
        // is felt on every note.  Give it back slowly when the queue has had room
        // to spare for a whole window, so a genuinely lumpy host still keeps what
        // it needs while a one-off hiccup costs seconds, not the session.
        if (windowMinSurplus > margin * 1.5)
            margin = std::max (floorMargin, margin * 0.8);

        if (targetRatio != 1.0)
            ratio = targetRatio;

        windowFrames = 0.0;
        windowMinSurplus = std::numeric_limits<double>::max();
    }

    double rate = 44100.0;
    std::vector<ves::StereoFrame> local;
    size_t count = 0;
    double pos = 0.0;
    bool primed = false;
    double margin = 256.0;
    double floorMargin = 256.0;
    double ratio = 1.0;
    double targetRatio = 1.0;
    double windowFrames = 0.0;
    double windowMinSurplus = std::numeric_limits<double>::max();

    double lastPrimeLevel = 0.0;

    std::atomic<float> primeFrames { 0.0f };
    std::atomic<float> latencyFrames { 0.0f };
    std::atomic<float> marginFrames { 0.0f };
    std::atomic<int> ratioPpm { 0 };
    std::atomic<uint64_t> underruns { 0 };
    std::atomic<uint64_t> discards { 0 };
};
