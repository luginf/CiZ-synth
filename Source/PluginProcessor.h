#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>

#include "AudioJitterBuffer.h"
#include "StereoChorus.h"
#include "CzTone.h"
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
    // MAME driver in use: "cz101" or "cz1".  Persisted in settings.xml.
    juce::String getMachine() const { return machine; }
    juce::String getMachineLabel() const { return machine == "cz1" ? "CZ-1" : "CZ-101"; }
    void setMachine (const juce::String& driver);

    // SysEx for the synth, queued from the message thread and sent to MAME from the
    // audio thread (the only producer of the engine's MIDI queue).  Keep messages
    // small: the engine queue holds 8192 bytes.
    void sendSysex (const std::vector<std::uint8_t>& message);

    // Direct access to the CZ-1's internal tone memory (emulated RAM, no MIDI involved).
    // Bodies: subram[128 * slot]; names: mainram[2304 + 16 * slot]; slot 0-63 = A-1..H-8.
    bool isToneMemoryAvailable() const;
    bool readInternalTone (int slot, cz::Tone& tone) const;
    bool writeInternalTone (int slot, const cz::Tone& tone);
    // Program change on MIDI channel 1: makes the firmware reload that internal tone.
    void recallInternalTone (int slot);

    // Latest tone dump the synth sent on MIDI OUT (panel "MIDI SAVE").
    bool getLatestDump (cz::Dump& out) const;
    int getDumpGeneration() const { return dumpGeneration.load(); }

    // Software stereo chorus, applied to the CZ-1 only (MAME does not emulate it).
    bool isChorusEnabled() const { return chorusEnabled.load(); }
    void setChorusEnabled (bool enabled) { chorusEnabled.store (enabled); }

    CZ101Processor();
    ~CZ101Processor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    // O hospedeiro avisa aqui que vai renderizar fora do tempo real (bounce,
    // exportação, congelamento de pista). Sem tratar isso o plugin devolvia
    // silêncio: o MAME roda preso ao relógio de parede e a DAW pede blocos bem
    // mais rápido que isso, então a fila nunca enchia.
    void setNonRealtime (bool offline) noexcept override;

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

    // Notes played from the computer keyboard (editor) are merged into the MIDI input.
    juce::MidiKeyboardState keyboardState;

private:
    void timerCallback() override;
    void captureMidiOutput();
    void startEngineIfNeeded();
    void stopEngine();
    void updateBootState();
    bool hasRomSet (const juce::File& directory) const;
    juce::File buildRomStage (const juce::File& romsDirectory) const;
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
    juce::String machine { "cz101" };
    std::atomic<bool> machineIsCz1 { false };
    std::atomic<bool> chorusEnabled { true };
    StereoChorus chorus;
    double currentSampleRate = 0.0;
    std::atomic<int> currentBlockSize { 0 };

    AudioJitterBuffer jitterBuffer;

    // Renderização fora do tempo real: espera o áudio em vez de desistir dele.
    void renderizarOffline (ves::EmbeddedEmulatorEngine&, float* esquerda, float* direita, int numSamples);
    std::atomic<bool> offline { false };
    std::vector<ves::StereoFrame> baldeOffline;

    uint64_t jitterBufferGeneration = 0;
    std::vector<float> monoScratch;

    // Clock comparison MAME vs audio device (logged every 5 s)
    std::atomic<uint64_t> deviceFramesRendered { 0 };
    double rateLogLastTime = 0.0;
    uint64_t rateLogLastMame = 0;
    uint64_t rateLogLastDevice = 0;

    // Bytes the synth sent on MIDI OUT, collected on the message thread (single consumer).
    juce::AbstractFifo sysexFifo { 16384 };
    std::vector<std::uint8_t> sysexStorage = std::vector<std::uint8_t> (16384);
    mutable juce::CriticalSection dumpLock;
    cz::Dump latestDump;
    std::atomic<int> dumpGeneration { 0 };

    std::vector<std::uint8_t> midiOutCapture;
    juce::uint32 midiOutLastByteMs = 0;

    std::atomic<uint32_t> lastMidiInMs { 0 };
    std::atomic<uint64_t> midiMessagesReceived { 0 };
    std::atomic<uint64_t> midiDroppedBeforeReady { 0 };
    std::atomic<float> peak[2] { 0.0f, 0.0f };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CZ101Processor)
};
