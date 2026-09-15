#pragma once

#include "PluginProcessor.h"

#include <functional>
#include <set>

// Shows the frame MAME renders from the cz101 layout (LCD + front panel) and
// forwards mouse clicks back to MAME so the panel buttons work.
class MameDisplay final : public juce::Component
{
public:
    explicit MameDisplay (CZ101Processor&);

    bool updateFrame();
    double getPanelAspect() const;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

private:
    juce::Rectangle<float> getImageDestination() const;
    juce::Rectangle<int> getSourceBounds() const;
    void updateSourceBounds();
    bool toMame (juce::Point<float> position, juce::Point<int>& result) const;

    CZ101Processor& processor;
    juce::Image image;
    juce::Rectangle<int> sourceBounds;
    uint64_t displayedGeneration = 0;
    uint64_t displayedEngineGeneration = 0;
    juce::Point<int> lastMame { -1, -1 };
    bool mouseIsDown = false;
    int publishedCaptureWidth = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MameDisplay)
};

class LevelMeter final : public juce::Component
{
public:
    void setLevels (float left, float right);
    void paint (juce::Graphics&) override;

private:
    float levels[2] { 0.0f, 0.0f };
    float holds[2] { 0.0f, 0.0f };
};

class MidiLamp final : public juce::Component
{
public:
    void setActive (bool);
    void paint (juce::Graphics&) override;

private:
    bool active = false;
};

// Keeps the window at the panel's proportions (plus the header), so the MAME
// panel fills it without black bars at any size.
class PanelConstrainer final : public juce::ComponentBoundsConstrainer
{
public:
    static constexpr int chromeWidth = 12;   // display margins
    static constexpr int chromeHeight = 56;  // 44 px header + display margins

    static int heightForWidth (int width, double aspect)
    {
        return juce::roundToInt ((width - chromeWidth) / aspect) + chromeHeight;
    }

    void checkBounds (juce::Rectangle<int>& bounds, const juce::Rectangle<int>& previous,
                      const juce::Rectangle<int>& limits, bool isStretchingTop, bool isStretchingLeft,
                      bool isStretchingBottom, bool isStretchingRight) override;

    std::function<double()> aspect;
};

class CZ101Editor final : public juce::AudioProcessorEditor,
                          private juce::Timer
{
public:
    explicit CZ101Editor (CZ101Processor&);
    ~CZ101Editor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void chooseRomsFolder();
    void enableNewMidiInputs();

    CZ101Processor& processor;

    PanelConstrainer constrainer;
    MameDisplay display;
    LevelMeter meter;
    MidiLamp midiLamp;
    juce::Label statusLabel;
    juce::TextButton romsButton { "ROMs..." };
    juce::TextButton restartButton { "Reiniciar" };

    std::unique_ptr<juce::FileChooser> chooser;
    std::set<juce::String> knownMidiInputs;
    int timerTicks = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CZ101Editor)
};
