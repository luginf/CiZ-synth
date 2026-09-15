#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>

#include "AudioJitterBuffer.h"
#include "EmbeddedEmulatorEngine.h"

#include <atomic>
#include <memory>
#include <vector>

enum class EngineState
{
    Stopped,
    MissingRom,
    Booting,
    Ready,
    Failed
};

struct VideoFrame
{
    int width = 0;
    int height = 0;
    uint64_t generation = 0;
    uint64_t engineGeneration = 0;
    std::vector<std::uint32_t> pixels;
};

// Hosts one embedded MAME instance running the Casio CZ-101 driver.
class CZ101Processor final : public juce::AudioProcessor,
                             private juce::Timer
{
public:
    static constexpr const char* driverName = "cz101";

    CZ101Processor();
    ~CZ101Processor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    //==============================================================================
    // Editor-facing API (message thread)
    EngineState getEngineState() const { return static_cast<EngineState> (state.load()); }
    juce::String getStatusText() const;
    juce::String getAudioDiagnosticsText() const;

    juce::File getRomsDirectory() const;
    void setRomsDirectory (const juce::File&);
    juce::File getNvramDirectory() const;
    void restartEngine();

    bool copyLatestVideoFrame (VideoFrame&);
    uint64_t getEngineGeneration() const { return engineGeneration.load(); }
    void requestVideoCaptureWidth (int width);
    void enqueueMouseEvent (ves::EmbeddedMouseEventType, int x, int y);
    void requestMouseRelease();

    uint32_t getLastMidiInMs() const { return lastMidiInMs.load(); }
    uint64_t getMidiMessagesReceived() const { return midiMessagesReceived.load(); }
    float takePeak (int channel);

private:
    void timerCallback() override;
    void startEngineIfNeeded();
    void stopEngine();
    void updateBootState();
    bool hasRomSet (const juce::File& directory) const;
    juce::File getSettingsFile() const;
    void saveSettings() const;

    std::shared_ptr<ves::EmbeddedEmulatorEngine> engine;
    juce::CriticalSection engineLock;

    std::atomic<int> state { static_cast<int> (EngineState::Stopped) };
    std::atomic<uint64_t> engineGeneration { 0 };
    std::atomic<int> requestedCaptureWidth { 1024 };
    juce::uint32 bootStartMs = 0;
    juce::String lastError;

    juce::String romsPath;
    double currentSampleRate = 0.0;
    std::atomic<int> currentBlockSize { 0 };

    AudioJitterBuffer jitterBuffer;
    uint64_t jitterBufferGeneration = 0;
    std::vector<float> monoScratch;

    // Clock comparison MAME vs audio device (logged every 5 s)
    std::atomic<uint64_t> deviceFramesRendered { 0 };
    double rateLogLastTime = 0.0;
    uint64_t rateLogLastMame = 0;
    uint64_t rateLogLastDevice = 0;

    std::atomic<uint32_t> lastMidiInMs { 0 };
    std::atomic<uint64_t> midiMessagesReceived { 0 };
    std::atomic<uint64_t> midiDroppedBeforeReady { 0 };
    std::atomic<float> peak[2] { 0.0f, 0.0f };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CZ101Processor)
};
