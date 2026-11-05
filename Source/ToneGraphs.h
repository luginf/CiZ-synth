#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <cmath>

#include "CzTone.h"

// Schematic plots for the tone editor.  They follow the edited parameters but are
// drawings, not a measurement of the chip: the wave shapes approximate the CZ phase
// distortion waves at a fixed DCW amount, and envelope times are proportional to
// |level change| / rate rather than the firmware's exact timing.

// Selected envelope as a level/time polyline, with the sustain step and end marked.
class EnvelopeGraph : public juce::Component
{
public:
    void setEnvelope (const cz::Envelope& e, cz::EnvKind k, bool enabled)
    {
        env = e;
        kind = k;
        active = enabled;
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        auto area = getLocalBounds().toFloat();
        g.setColour (juce::Colour::fromRGB (24, 24, 28));
        g.fillRoundedRectangle (area, 4.0f);
        auto plot = area.reduced (10.0f, 14.0f);
        g.setColour (juce::Colours::grey.withAlpha (0.35f));
        g.drawRect (plot, 1.0f);
        g.setColour (juce::Colours::lightgrey);
        g.setFont (juce::FontOptions (12.0f));
        g.drawText (kind == cz::EnvKind::dca ? "DCA envelope (amplitude)" : kind == cz::EnvKind::dcw ? "DCW envelope (timbre)" : "DCO envelope (pitch)",
                    getLocalBounds().removeFromTop (14), juce::Justification::centred);
        if (! active)
            return;

        // Time of each step: level change divided by a speed derived from the rate.
        std::array<float, 8> duration {};
        float total = 0.0f, previous = 0.0f;
        for (int i = 0; i <= env.end; ++i)
        {
            const auto& s = env.steps[static_cast<size_t> (i)];
            const float speed = 0.2f + static_cast<float> (s.rate) * static_cast<float> (s.rate) / 100.0f;
            duration[static_cast<size_t> (i)] = 0.05f + std::abs (static_cast<float> (s.level) - previous) / speed;
            total += duration[static_cast<size_t> (i)];
            previous = static_cast<float> (s.level);
        }
        if (total <= 0.0f)
            return;

        juce::Path path;
        float t = 0.0f;
        auto pointAt = [&] (float time, float level)
        { return juce::Point<float> (plot.getX() + plot.getWidth() * time / total, plot.getBottom() - plot.getHeight() * level / 99.0f); };
        path.startNewSubPath (pointAt (0.0f, 0.0f));
        for (int i = 0; i <= env.end; ++i)
        {
            t += duration[static_cast<size_t> (i)];
            const auto p = pointAt (t, static_cast<float> (env.steps[static_cast<size_t> (i)].level));
            path.lineTo (p);
            if (i == env.sustain)
            {
                g.setColour (juce::Colours::orange.withAlpha (0.5f));
                g.drawVerticalLine (juce::roundToInt (p.x), plot.getY(), plot.getBottom());
            }
            g.setColour (juce::Colours::white);
            g.fillEllipse (p.x - 3.0f, p.y - 3.0f, 6.0f, 6.0f);
            g.setColour (juce::Colours::grey);
            g.drawText (juce::String (i + 1), juce::Rectangle<float> (p.x - 8.0f, plot.getBottom() + 1.0f, 16.0f, 12.0f), juce::Justification::centred);
        }
        g.setColour (juce::Colour::fromRGB (90, 200, 255));
        g.strokePath (path, juce::PathStrokeType (2.0f));
    }

private:
    cz::Envelope env;
    cz::EnvKind kind = cz::EnvKind::dca;
    bool active = false;
};

// One period of both waves of a line (wave 1 solid, wave 2 dashed).
class WaveGraph : public juce::Component
{
public:
    void setWaves (int w1, int w2, const juce::String& title)
    {
        wave1 = w1;
        wave2 = w2;
        label = title;
        repaint();
    }

    // x in [0,1), returns -1..1.  Approximates the CZ phase distortion waves at DCW ~ 0.8.
    static float shape (int wave, float x)
    {
        constexpr float twoPi = 6.2831853f;
        const float d = 0.8f;
        const float kk = 0.5f * (1.0f - 0.97f * d);
        auto ramp = [] (float v, float from, float to) { return juce::jlimit (0.0f, 1.0f, (v - from) / (to - from)); };
        switch (wave)
        {
            case 1: // sawtooth
            {
                const float k = 0.5f * (1.0f - 0.97f * d);
                const float phi = x < k ? 0.5f * x / k : 0.5f + 0.5f * (x - k) / (1.0f - k);
                return std::cos (twoPi * phi);
            }
            case 2: // square
            {
                const float phi = x < 0.5f ? 0.5f * ramp (x, 0.5f - kk, 0.5f) : 0.5f + 0.5f * ramp (x, 1.0f - kk, 1.0f);
                return std::cos (twoPi * phi);
            }
            case 3: // pulse (narrow high part)
            {
                const float phi = x < 0.5f ? 0.5f * ramp (x, 0.25f - kk * 0.5f, 0.25f) : 0.5f + 0.5f * ramp (x, 1.0f - kk, 1.0f);
                return std::cos (twoPi * phi);
            }
            case 4: // double sine
                return (1.0f - d) * std::cos (twoPi * x) + d * std::cos (2.0f * twoPi * x);
            case 5: // saw + pulse
            {
                const float v = x < 0.5f ? shape (1, 2.0f * x) : -1.0f;
                return v;
            }
            case 6: case 7: case 8: // resonance, decaying burst of the n-th harmonic
                return (1.0f - x) * std::cos (twoPi * static_cast<float> (wave - 3) * x);
            default:
                return 0.0f;
        }
    }

    void paint (juce::Graphics& g) override
    {
        auto area = getLocalBounds().toFloat();
        g.setColour (juce::Colour::fromRGB (24, 24, 28));
        g.fillRoundedRectangle (area, 4.0f);
        g.setColour (juce::Colours::lightgrey);
        g.setFont (juce::FontOptions (12.0f));
        g.drawText (label, getLocalBounds().removeFromTop (14), juce::Justification::centred);
        auto plot = area.reduced (10.0f, 16.0f);
        g.setColour (juce::Colours::grey.withAlpha (0.35f));
        g.drawHorizontalLine (juce::roundToInt (plot.getCentreY()), plot.getX(), plot.getRight());

        auto draw = [&] (int wave, bool dashed)
        {
            if (wave <= 0)
                return;
            juce::Path p;
            constexpr int n = 160;
            for (int i = 0; i <= n; ++i)
            {
                const float x = static_cast<float> (i) / n * 0.9999f;
                const float px = plot.getX() + plot.getWidth() * static_cast<float> (i) / n;
                const float py = plot.getCentreY() - 0.5f * plot.getHeight() * shape (wave, x);
                if (i == 0) p.startNewSubPath (px, py); else p.lineTo (px, py);
            }
            if (dashed)
            {
                juce::Path dashedPath;
                const float pattern[] = { 4.0f, 3.0f };
                juce::PathStrokeType (1.5f).createDashedStroke (dashedPath, p, pattern, 2);
                g.setColour (juce::Colour::fromRGB (255, 170, 80));
                g.fillPath (dashedPath);
            }
            else
            {
                g.setColour (juce::Colour::fromRGB (90, 200, 255));
                g.strokePath (p, juce::PathStrokeType (2.0f));
            }
        };
        draw (wave1, false);
        draw (wave2, true);
    }

private:
    int wave1 = 1, wave2 = 0;
    juce::String label;
};
