#pragma once

#include "CzTone.h"
#include "PluginProcessor.h"

// Sound editor for the emulated CZ-1.  Reads and writes the synth's internal tone
// memory (emulated RAM) directly; .syx import/export is available for backups.
class ToneEditor final : public juce::Component,
                         private juce::Timer,
                         private juce::TextEditor::Listener
{
public:
    explicit ToneEditor (CZ101Processor&);
    ~ToneEditor() override;

    void resized() override;

private:
    void timerCallback() override;
    void textEditorTextChanged (juce::TextEditor&) override;

    void refreshSlotList();
    void loadSlotIntoControls();
    void controlsChanged();
    void scheduleSend();
    void writeCurrent();
    void loadEnvelopeIntoControls();
    void envelopeControlsChanged();
    void paramChanged (size_t index);
    cz::EnvKind currentEnvKind() const;
    int currentEnvLine() const;
    void openSyx();
    void saveSyx();
    juce::String slotLabel (int slot) const;

    CZ101Processor& processor;
    cz::Dump dump;
    int slot = 0;
    int lastGeneration = 0;
    bool updatingControls = false;
    bool sendPending = false;
    bool memoryWasAvailable = false;
    int pollTicks = 0;
    juce::uint32 lastEditMs = 0;

    juce::Label help;
    juce::ComboBox slotBox, octaveBox, lineBox;
    juce::Label slotLabelText { {}, "Tone" }, nameLabel { {}, "Name" }, octaveLabel { {}, "Octave" }, lineLabel { {}, "Line select" };
    juce::TextEditor nameEditor;
    juce::Label envLabel { {}, "Envelope" }, endLabel { {}, "Last step" }, susLabel { {}, "Sustain" };
    juce::ComboBox envBox, endBox, susBox;
    std::array<juce::Label, 8> stepLabels;
    std::array<juce::Slider, 8> rateSliders, levelSliders;
    std::array<juce::Slider, 16> paramSliders;
    std::array<juce::Label, 16> paramLabels;
    juce::TextEditor rawEditor;
    juce::Label rawLabel { {}, "Raw bytes 0-127 (hex, advanced; unknown fields are preserved)" };
    juce::Label rateHeader { {}, "Rate" }, levelHeader { {}, "Level" };
    juce::TextButton openButton { "Import .syx..." }, saveButton { "Export .syx..." }, sendButton { "Apply" };
    juce::Label status;
    std::unique_ptr<juce::FileChooser> chooser;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ToneEditor)
};

class ToneEditorWindow final : public juce::DocumentWindow
{
public:
    explicit ToneEditorWindow (CZ101Processor&);
    void closeButtonPressed() override { setVisible (false); }
};
