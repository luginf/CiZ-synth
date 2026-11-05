#include "ToneEditor.h"

ToneEditor::ToneEditor (CZ101Processor& p) : processor (p)
{
    help.setText ("Reads and writes the CZ-1's internal tone memory directly (no MIDI dump needed). "
                  "Select the CZ-1 in Options > Machine. Edits are written to the slot and the synth is told to reload it.",
                  juce::dontSendNotification);
    help.setFont (juce::FontOptions (12.5f));
    help.setJustificationType (juce::Justification::topLeft);
    help.setMinimumHorizontalScale (1.0f);
    addAndMakeVisible (help);

    for (auto* label : { &slotLabelText, &nameLabel, &octaveLabel, &lineLabel, &rawLabel })
        addAndMakeVisible (label);

    slotBox.onChange = [this] { slot = juce::jmax (0, slotBox.getSelectedId() - 1); loadSlotIntoControls(); };
    addAndMakeVisible (slotBox);

    nameEditor.setInputRestrictions (cz::nameSize);
    nameEditor.addListener (this);
    addAndMakeVisible (nameEditor);

    octaveBox.addItem ("Normal", 1);
    octaveBox.addItem ("+1", 2);
    octaveBox.addItem ("-1", 3);
    octaveBox.onChange = [this] { controlsChanged(); };
    addAndMakeVisible (octaveBox);

    lineBox.addItem ("1", 1);
    lineBox.addItem ("2", 2);
    lineBox.addItem ("1+1'", 3);
    lineBox.addItem ("1+2'", 4);
    lineBox.onChange = [this] { controlsChanged(); };
    addAndMakeVisible (lineBox);

    for (auto* label : { &envLabel, &endLabel, &susLabel, &rateHeader, &levelHeader })
        addAndMakeVisible (label);

    const char* envNames[] = { "Line 1 - DCO (pitch)", "Line 1 - DCW (wave)", "Line 1 - DCA (amp)",
                               "Line 2 - DCO (pitch)", "Line 2 - DCW (wave)", "Line 2 - DCA (amp)" };
    for (int i = 0; i < 6; ++i)
        envBox.addItem (envNames[i], i + 1);
    envBox.setSelectedId (3, juce::dontSendNotification);
    envBox.onChange = [this] { loadEnvelopeIntoControls(); };
    addAndMakeVisible (envBox);

    for (int i = 1; i <= 8; ++i)
        endBox.addItem (juce::String (i), i);
    endBox.onChange = [this] { envelopeControlsChanged(); };
    addAndMakeVisible (endBox);

    susBox.addItem ("None", 1);
    for (int i = 1; i <= 8; ++i)
        susBox.addItem ("Step " + juce::String (i), i + 1);
    susBox.onChange = [this] { envelopeControlsChanged(); };
    addAndMakeVisible (susBox);

    for (size_t i = 0; i < 8; ++i)
    {
        stepLabels[i].setText ("Step " + juce::String (static_cast<int> (i) + 1), juce::dontSendNotification);
        addAndMakeVisible (stepLabels[i]);
        for (auto* slider : { &rateSliders[i], &levelSliders[i] })
        {
            slider->setSliderStyle (juce::Slider::LinearHorizontal);
            slider->setRange (0.0, 99.0, 1.0);
            slider->setTextBoxStyle (juce::Slider::TextBoxRight, false, 44, 22);
            slider->onValueChange = [this] { envelopeControlsChanged(); };
            addAndMakeVisible (slider);
        }
    }

    {
        const auto& params = cz::simpleParams();
        for (size_t i = 0; i < params.size(); ++i)
        {
            paramLabels[i].setText (params[i].name, juce::dontSendNotification);
            addAndMakeVisible (paramLabels[i]);
            auto& slider = paramSliders[i];
            slider.setSliderStyle (juce::Slider::LinearHorizontal);
            slider.setRange (params[i].min, params[i].max, 1.0);
            slider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 40, 22);
            slider.onValueChange = [this, i] { paramChanged (i); };
            addAndMakeVisible (slider);
        }
    }

    rawEditor.setMultiLine (true);
    rawEditor.setFont (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), 13.0f, juce::Font::plain));
    rawEditor.addListener (this);
    addAndMakeVisible (rawEditor);

    openButton.onClick = [this] { openSyx(); };
    saveButton.onClick = [this] { saveSyx(); };
    sendButton.onClick = [this] { sendPending = false; writeCurrent(); };
    addAndMakeVisible (openButton);
    addAndMakeVisible (saveButton);
    addAndMakeVisible (sendButton);

    status.setFont (juce::FontOptions (12.5f));
    addAndMakeVisible (status);

    refreshSlotList();
    setSize (640, 1090);
    startTimerHz (10);
}

ToneEditor::~ToneEditor() { stopTimer(); }

void ToneEditor::resized()
{
    auto area = getLocalBounds().reduced (12);
    help.setBounds (area.removeFromTop (54));
    area.removeFromTop (6);

    auto row = [&] (juce::Label& label, juce::Component& control)
    {
        auto r = area.removeFromTop (28);
        label.setBounds (r.removeFromLeft (90));
        control.setBounds (r);
        area.removeFromTop (6);
    };
    row (slotLabelText, slotBox);
    row (nameLabel, nameEditor);
    row (octaveLabel, octaveBox);
    row (lineLabel, lineBox);

    row (envLabel, envBox);
    {
        auto r = area.removeFromTop (28);
        endLabel.setBounds (r.removeFromLeft (90));
        endBox.setBounds (r.removeFromLeft (110));
        r.removeFromLeft (16);
        susLabel.setBounds (r.removeFromLeft (70));
        susBox.setBounds (r.removeFromLeft (110));
        area.removeFromTop (4);
    }
    {
        auto r = area.removeFromTop (18);
        r.removeFromLeft (90);
        const auto half = r.getWidth() / 2;
        rateHeader.setBounds (r.removeFromLeft (half));
        levelHeader.setBounds (r);
    }
    for (size_t i = 0; i < 8; ++i)
    {
        auto r = area.removeFromTop (26);
        stepLabels[i].setBounds (r.removeFromLeft (90));
        const auto half = r.getWidth() / 2;
        rateSliders[i].setBounds (r.removeFromLeft (half).reduced (2, 0));
        levelSliders[i].setBounds (r.reduced (2, 0));
    }
    area.removeFromTop (6);

    for (size_t i = 0; i < paramSliders.size(); i += 2)
    {
        auto r = area.removeFromTop (26);
        auto left = r.removeFromLeft (r.getWidth() / 2);
        paramLabels[i].setBounds (left.removeFromLeft (110));
        paramSliders[i].setBounds (left.reduced (2, 0));
        paramLabels[i + 1].setBounds (r.removeFromLeft (110));
        paramSliders[i + 1].setBounds (r.reduced (2, 0));
    }
    area.removeFromTop (6);

    rawLabel.setBounds (area.removeFromTop (22));
    auto buttons = area.removeFromBottom (30);
    status.setBounds (area.removeFromBottom (24));
    area.removeFromBottom (4);
    openButton.setBounds (buttons.removeFromLeft (120));
    buttons.removeFromLeft (8);
    saveButton.setBounds (buttons.removeFromLeft (120));
    buttons.removeFromLeft (8);
    sendButton.setBounds (buttons.removeFromLeft (100));
    rawEditor.setBounds (area);
}

juce::String ToneEditor::slotLabel (int s) const
{
    // The CZ-1 names internal tones A-1..H-8.
    juce::String label;
    label << juce::String::charToString (static_cast<juce::juce_wchar> ('A' + s / 8)) << "-" << (s % 8 + 1);
    if (dump.present[static_cast<size_t> (s)])
        label << "  " << dump.tones[static_cast<size_t> (s)].name();
    else
        label << "  (waiting)";
    return label;
}

void ToneEditor::refreshSlotList()
{
    const auto keep = slot;
    updatingControls = true;
    slotBox.clear (juce::dontSendNotification);
    for (int i = 0; i < 64; ++i)
        slotBox.addItem (slotLabel (i), i + 1);
    slotBox.setSelectedId (keep + 1, juce::dontSendNotification);
    updatingControls = false;
    loadSlotIntoControls();
}

void ToneEditor::loadSlotIntoControls()
{
    updatingControls = true;
    const auto& tone = dump.tones[static_cast<size_t> (slot)];
    const bool present = dump.present[static_cast<size_t> (slot)];
    nameEditor.setText (present ? juce::String (tone.name()) : juce::String(), false);
    octaveBox.setSelectedId (tone.octave() == 1 ? 2 : tone.octave() == 2 ? 3 : 1, juce::dontSendNotification);
    lineBox.setSelectedId (tone.lineSelect() + 1, juce::dontSendNotification);

    juce::String hex;
    for (int i = 0; i < cz::bodySize; ++i)
    {
        hex << juce::String::toHexString (tone.data[static_cast<size_t> (i)]).paddedLeft ('0', 2).toUpperCase();
        hex << ((i % 16 == 15) ? "\n" : " ");
    }
    rawEditor.setText (hex, false);
    for (size_t i = 0; i < paramSliders.size(); ++i)
    {
        paramSliders[i].setValue (cz::simpleParams()[i].get (tone), juce::dontSendNotification);
        paramSliders[i].setEnabled (present);
    }
    updatingControls = false;
    loadEnvelopeIntoControls();
    updatingControls = true;

    for (auto* c : std::initializer_list<juce::Component*> { &nameEditor, &octaveBox, &lineBox, &rawEditor, &sendButton, &saveButton })
        c->setEnabled (present);
    status.setText (present ? "Live from the synth memory." : "Waiting for the CZ-1 (Options > Machine > CZ-1).",
                    juce::dontSendNotification);
    updatingControls = false;
}

cz::EnvKind ToneEditor::currentEnvKind() const
{
    switch ((envBox.getSelectedId() - 1) % 3)
    {
        case 0:  return cz::EnvKind::dco;
        case 1:  return cz::EnvKind::dcw;
        default: return cz::EnvKind::dca;
    }
}

int ToneEditor::currentEnvLine() const { return (envBox.getSelectedId() - 1) / 3; }

void ToneEditor::loadEnvelopeIntoControls()
{
    const bool wasUpdating = updatingControls;
    updatingControls = true;
    const bool present = dump.present[static_cast<size_t> (slot)];
    const auto env = cz::getEnvelope (dump.tones[static_cast<size_t> (slot)], currentEnvKind(), currentEnvLine());
    endBox.setSelectedId (env.end + 1, juce::dontSendNotification);
    susBox.setSelectedId (env.sustain < 0 ? 1 : env.sustain + 2, juce::dontSendNotification);
    for (int i = 0; i < 8; ++i)
    {
        const bool active = present && i <= env.end;
        rateSliders[static_cast<size_t> (i)].setValue (env.steps[static_cast<size_t> (i)].rate, juce::dontSendNotification);
        levelSliders[static_cast<size_t> (i)].setValue (env.steps[static_cast<size_t> (i)].level, juce::dontSendNotification);
        rateSliders[static_cast<size_t> (i)].setEnabled (active);
        levelSliders[static_cast<size_t> (i)].setEnabled (active);
    }
    for (auto* c : std::initializer_list<juce::Component*> { &endBox, &susBox, &envBox })
        c->setEnabled (present);
    updatingControls = wasUpdating;
}

void ToneEditor::envelopeControlsChanged()
{
    if (updatingControls || ! dump.present[static_cast<size_t> (slot)])
        return;

    cz::Envelope env;
    env.end = juce::jmax (0, endBox.getSelectedId() - 1);
    env.sustain = susBox.getSelectedId() <= 1 ? -1 : susBox.getSelectedId() - 2;
    for (size_t i = 0; i < 8; ++i)
    {
        env.steps[i].rate = juce::roundToInt (rateSliders[i].getValue());
        env.steps[i].level = juce::roundToInt (levelSliders[i].getValue());
    }
    if (env.sustain > env.end)
        env.sustain = -1;

    cz::setEnvelope (dump.tones[static_cast<size_t> (slot)], currentEnvKind(), currentEnvLine(), env);
    loadSlotIntoControls();   // refreshes raw bytes and re-reads the envelope from the encoded data
    scheduleSend();
}

void ToneEditor::paramChanged (size_t index)
{
    if (updatingControls || ! dump.present[static_cast<size_t> (slot)])
        return;

    cz::simpleParams()[index].set (dump.tones[static_cast<size_t> (slot)], juce::roundToInt (paramSliders[index].getValue()));
    loadSlotIntoControls();
    scheduleSend();
}

void ToneEditor::controlsChanged()
{
    if (updatingControls || ! dump.present[static_cast<size_t> (slot)])
        return;

    auto& tone = dump.tones[static_cast<size_t> (slot)];
    const auto octaveId = octaveBox.getSelectedId();
    tone.setOctave (octaveId == 2 ? 1 : octaveId == 3 ? 2 : 0);
    tone.setLineSelect (juce::jmax (0, lineBox.getSelectedId() - 1));
    loadSlotIntoControls();
    scheduleSend();
}

void ToneEditor::textEditorTextChanged (juce::TextEditor& editor)
{
    if (updatingControls || ! dump.present[static_cast<size_t> (slot)])
        return;

    auto& tone = dump.tones[static_cast<size_t> (slot)];
    if (&editor == &nameEditor)
    {
        tone.setName (nameEditor.getText().toStdString());
        scheduleSend();
    }
    else if (&editor == &rawEditor)
    {
        const auto tokens = juce::StringArray::fromTokens (rawEditor.getText(), " \n\r\t", "");
        int index = 0;
        bool valid = true;
        std::array<std::uint8_t, cz::bodySize> bytes {};
        for (const auto& token : tokens)
        {
            if (token.isEmpty())
                continue;
            if (index >= cz::bodySize || token.length() > 2 || ! token.containsOnly ("0123456789abcdefABCDEF"))
            {
                valid = false;
                break;
            }
            bytes[static_cast<size_t> (index++)] = static_cast<std::uint8_t> (token.getHexValue32());
        }
        if (valid && index == cz::bodySize)
        {
            std::copy (bytes.begin(), bytes.end(), tone.data.begin());
            scheduleSend();
        }
    }
}

void ToneEditor::scheduleSend()
{
    sendPending = true;
    lastEditMs = juce::Time::getMillisecondCounter();
    status.setText ("Editing...", juce::dontSendNotification);
}

void ToneEditor::writeCurrent()
{
    if (processor.writeInternalTone (slot, dump.tones[static_cast<size_t> (slot)]))
    {
        processor.recallInternalTone (slot);
        status.setText ("Written to " + slotLabel (slot).upToFirstOccurrenceOf ("  ", false, false) + ".", juce::dontSendNotification);
        slotBox.changeItemText (slot + 1, slotLabel (slot));
    }
    else
        status.setText ("CZ-1 not running: select CZ-1 in Options > Machine and wait for Ready.", juce::dontSendNotification);
}

void ToneEditor::timerCallback()
{
    const bool available = processor.isToneMemoryAvailable();
    if (! available)
    {
        if (memoryWasAvailable)
            status.setText ("CZ-1 not running: select CZ-1 in Options > Machine and wait for Ready.", juce::dontSendNotification);
        memoryWasAvailable = false;
        return;
    }

    // Debounced live write of the edited tone.
    if (sendPending && juce::Time::getMillisecondCounter() - lastEditMs > 200)
    {
        sendPending = false;
        writeCurrent();
        return;
    }

    // Pull the synth's memory into the list (about twice a second) unless an edit is in flight.
    if (sendPending || ++pollTicks % 5 != 0)
        return;

    bool changedList = ! memoryWasAvailable;
    bool changedCurrent = false;
    for (int i = 0; i < 64; ++i)
    {
        cz::Tone tone;
        if (! processor.readInternalTone (i, tone))
            return;
        auto& cached = dump.tones[static_cast<size_t> (i)];
        if (! dump.present[static_cast<size_t> (i)] || cached.data != tone.data)
        {
            const bool nameChanged = cached.name() != tone.name() || ! dump.present[static_cast<size_t> (i)];
            cached = tone;
            dump.present[static_cast<size_t> (i)] = true;
            if (nameChanged)
                changedList = true;
            if (i == slot)
                changedCurrent = true;
        }
    }
    memoryWasAvailable = true;
    if (changedList)
        refreshSlotList();
    else if (changedCurrent && ! nameEditor.hasKeyboardFocus (true) && ! rawEditor.hasKeyboardFocus (true))
        loadSlotIntoControls();
}

void ToneEditor::openSyx()
{
    chooser = std::make_unique<juce::FileChooser> ("Import a CZ-1 dump into the synth memory", juce::File(), "*.syx;*.bin");
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [this] (const juce::FileChooser& fc)
                          {
                              const auto file = fc.getResult();
                              if (! file.existsAsFile())
                                  return;
                              juce::MemoryBlock block;
                              file.loadFileAsData (block);
                              const auto* data = static_cast<const std::uint8_t*> (block.getData());
                              const auto loaded = cz::parseDump ({ data, data + block.getSize() });
                              int written = 0;
                              for (int i = 0; i < 64; ++i)
                                  if (loaded.present[static_cast<size_t> (i)] && processor.writeInternalTone (i, loaded.tones[static_cast<size_t> (i)]))
                                      ++written;
                              pollTicks = 4;
                              status.setText ("Imported " + juce::String (written) + " tones from " + file.getFileName(), juce::dontSendNotification);
                          });
}

void ToneEditor::saveSyx()
{
    chooser = std::make_unique<juce::FileChooser> ("Save the tones as .syx", juce::File(), "*.syx");
    chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles
                              | juce::FileBrowserComponent::warnAboutOverwriting,
                          [this] (const juce::FileChooser& fc)
                          {
                              const auto file = fc.getResult();
                              if (file == juce::File())
                                  return;
                              std::vector<std::uint8_t> out;
                              for (int i = 0; i < 64; ++i)
                                  if (dump.present[static_cast<size_t> (i)])
                                  {
                                      const auto message = cz::encodeTone (dump.tones[static_cast<size_t> (i)], i);
                                      out.insert (out.end(), message.begin(), message.end());
                                  }
                              file.replaceWithData (out.data(), out.size());
                              status.setText ("Saved " + file.getFileName(), juce::dontSendNotification);
                          });
}

ToneEditorWindow::ToneEditorWindow (CZ101Processor& p)
    : juce::DocumentWindow ("CZ-1 tone editor", juce::Colour::fromRGB (38, 38, 42), juce::DocumentWindow::closeButton)
{
    setUsingNativeTitleBar (true);
    setContentOwned (new ToneEditor (p), true);
    setResizable (true, false);
    centreWithSize (getWidth(), getHeight());
}
