#pragma once

#include <cmath>
#include <vector>

// Simple stereo chorus: the centre (L+R)/2 signal goes through two modulated
// delay lines whose LFOs run in opposite phase, and the result is added to
// each channel.  A software stand-in for the CZ-1's hardware stereo chorus,
// which MAME does not emulate.
class StereoChorus
{
public:
    void prepare (double sampleRate)
    {
        rate = sampleRate;
        const auto size = static_cast<size_t> (sampleRate * 0.05) + 8; // 50 ms
        buffer.assign (size, 0.0f);
        writePos = 0;
        phase = 0.0;
    }

    void process (float* left, float* right, int numSamples)
    {
        if (buffer.empty())
            return;

        const auto size = static_cast<int> (buffer.size());
        const double phaseStep = lfoHz / rate;
        const double base = baseMs * 0.001 * rate;
        const double depth = depthMs * 0.001 * rate;

        for (int i = 0; i < numSamples; ++i)
        {
            buffer[static_cast<size_t> (writePos)] = 0.5f * (left[i] + right[i]);

            const auto wetL = tap (writePos - (base + depth * std::sin (twoPi * phase)), size);
            const auto wetR = tap (writePos - (base + depth * std::sin (twoPi * phase + pi)), size);

            left[i]  += mix * wetL;
            right[i] += mix * wetR;

            writePos = (writePos + 1) % size;
            phase += phaseStep;
            if (phase >= 1.0)
                phase -= 1.0;
        }
    }

private:
    static constexpr double pi = 3.14159265358979323846;
    static constexpr double twoPi = 2.0 * pi;

    float tap (double position, int size) const
    {
        while (position < 0.0)
            position += size;
        const auto index = static_cast<int> (position);
        const auto frac = static_cast<float> (position - index);
        const auto a = buffer[static_cast<size_t> (index % size)];
        const auto b = buffer[static_cast<size_t> ((index + 1) % size)];
        return a + (b - a) * frac;
    }

    std::vector<float> buffer;
    int writePos = 0;
    double phase = 0.0;
    double rate = 44100.0;

    double lfoHz = 0.7;
    double baseMs = 12.0;
    double depthMs = 4.0;
    float mix = 0.5f;
};
