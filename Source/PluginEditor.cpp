#include "PluginEditor.h"

#include <juce_audio_plugin_client/Standalone/juce_StandaloneFilterWindow.h>

#include <cmath>

namespace
{
const auto backgroundColour = juce::Colour::fromRGB (24, 24, 26);
const auto headerColour     = juce::Colour::fromRGB (38, 38, 42);
const auto czBlue           = juce::Colour::fromRGB (74, 97, 209);

juce::StandalonePluginHolder* standaloneHolder()
{
    return juce::JUCEApplicationBase::isStandaloneApp() ? juce::StandalonePluginHolder::getInstance() : nullptr;
}
}

//==============================================================================
MameDisplay::MameDisplay (CZ101Processor& p) : processor (p)
{
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
}

bool MameDisplay::updateFrame()
{
    const auto activeGeneration = processor.getEngineGeneration();
    if (activeGeneration != displayedEngineGeneration)
    {
        image = {};
        sourceBounds = {};
        displayedGeneration = 0;
        displayedEngineGeneration = activeGeneration;
        repaint();
    }

    VideoFrame frame;
    if (! processor.copyLatestVideoFrame (frame)
        || frame.engineGeneration != activeGeneration
        || frame.generation == displayedGeneration
        || frame.width <= 0 || frame.height <= 0
        || frame.pixels.size() != static_cast<size_t> (frame.width) * static_cast<size_t> (frame.height))
        return false;

    if (! image.isValid() || image.getWidth() != frame.width || image.getHeight() != frame.height)
        image = juce::Image (juce::Image::ARGB, frame.width, frame.height, true);

    {
        juce::Image::BitmapData data (image, juce::Image::BitmapData::writeOnly);
        for (int y = 0; y < frame.height; ++y)
        {
            const auto* row = frame.pixels.data() + static_cast<size_t> (y) * static_cast<size_t> (frame.width);
            for (int x = 0; x < frame.width; ++x)
            {
                const auto argb = row[x];
                reinterpret_cast<juce::PixelARGB*> (data.getPixelPointer (x, y))
                    ->setARGB (0xff, (argb >> 16) & 0xff, (argb >> 8) & 0xff, argb & 0xff);
            }
        }
    }

    displayedGeneration = frame.generation;
    updateSourceBounds();
    repaint();
    return true;
}

double MameDisplay::getPanelAspect() const
{
    // The generated artwork is 1600 x 500; use the live frame once one exists.
    const auto source = getSourceBounds();
    return image.isValid() && source.getHeight() > 0 ? source.getWidth() / static_cast<double> (source.getHeight())
                                                     : 3.2;
}

void MameDisplay::resized()
{
    // Ask MAME to rasterise at roughly the physical size we display.
    const auto* primary = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay();
    const auto scale = primary != nullptr ? primary->scale : 1.0;
    const auto width = juce::jlimit (1024, 4096, ((juce::roundToInt (getWidth() * scale) + 63) / 64) * 64);
    if (width != publishedCaptureWidth)
    {
        publishedCaptureWidth = width;
        processor.requestVideoCaptureWidth (width);
    }
}

void MameDisplay::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colours::black);

    if (! image.isValid())
    {
        g.setColour (juce::Colours::white.withAlpha (0.7f));
        g.setFont (juce::FontOptions (18.0f));
        g.drawFittedText (processor.getStatusText(), getLocalBounds().reduced (20), juce::Justification::centred, 3);
        return;
    }

    const auto target = getImageDestination();
    const auto source = getSourceBounds();
    g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
    g.drawImage (image,
                 juce::roundToInt (target.getX()), juce::roundToInt (target.getY()),
                 juce::roundToInt (target.getWidth()), juce::roundToInt (target.getHeight()),
                 source.getX(), source.getY(), source.getWidth(), source.getHeight());
}

juce::Rectangle<float> MameDisplay::getImageDestination() const
{
    if (! image.isValid())
        return {};

    const auto area = getLocalBounds().toFloat();
    const auto source = getSourceBounds().toFloat();
    const auto scale = juce::jmin (area.getWidth() / source.getWidth(), area.getHeight() / source.getHeight());
    if (! std::isfinite (scale) || scale <= 0.0f)
        return {};

    return juce::Rectangle<float> (source.getWidth() * scale, source.getHeight() * scale).withCentre (area.getCentre());
}

juce::Rectangle<int> MameDisplay::getSourceBounds() const
{
    return sourceBounds.isEmpty() ? image.getBounds() : sourceBounds;
}

void MameDisplay::updateSourceBounds()
{
    // Crop the black border MAME leaves around the layout.
    const juce::Image::BitmapData pixels (image, juce::Image::BitmapData::readOnly);
    int minX = image.getWidth(), minY = image.getHeight(), maxX = -1, maxY = -1;

    for (int y = 0; y < image.getHeight(); ++y)
        for (int x = 0; x < image.getWidth(); ++x)
        {
            const auto* px = reinterpret_cast<const juce::PixelARGB*> (pixels.getPixelPointer (x, y));
            if (static_cast<unsigned> (px->getRed()) + px->getGreen() + px->getBlue() > 24)
            {
                minX = juce::jmin (minX, x);
                minY = juce::jmin (minY, y);
                maxX = juce::jmax (maxX, x);
                maxY = juce::jmax (maxY, y);
            }
        }

    sourceBounds = (maxX >= minX && maxY >= minY) ? juce::Rectangle<int> (minX, minY, maxX - minX + 1, maxY - minY + 1)
                                                   : image.getBounds();
}

bool MameDisplay::toMame (juce::Point<float> position, juce::Point<int>& result) const
{
    const auto target = getImageDestination();
    if (target.isEmpty() || ! target.contains (position))
        return false;

    const auto source = getSourceBounds();
    const auto nx = (position.x - target.getX()) / target.getWidth();
    const auto ny = (position.y - target.getY()) / target.getHeight();
    result = { juce::jlimit (source.getX(), source.getRight() - 1, source.getX() + static_cast<int> (std::floor (nx * source.getWidth()))),
               juce::jlimit (source.getY(), source.getBottom() - 1, source.getY() + static_cast<int> (std::floor (ny * source.getHeight()))) };
    return true;
}

void MameDisplay::mouseMove (const juce::MouseEvent& e)
{
    if (toMame (e.position, lastMame))
        processor.enqueueMouseEvent (ves::EmbeddedMouseEventType::Move, lastMame.x, lastMame.y);
}

void MameDisplay::mouseDown (const juce::MouseEvent& e)
{
    mouseIsDown = e.mods.isLeftButtonDown() && toMame (e.position, lastMame);
    if (mouseIsDown)
        processor.enqueueMouseEvent (ves::EmbeddedMouseEventType::LeftDown, lastMame.x, lastMame.y);
}

void MameDisplay::mouseDrag (const juce::MouseEvent& e)
{
    if (mouseIsDown && toMame (e.position, lastMame))
        processor.enqueueMouseEvent (ves::EmbeddedMouseEventType::Move, lastMame.x, lastMame.y);
}

void MameDisplay::mouseUp (const juce::MouseEvent& e)
{
    if (! mouseIsDown)
        return;

    mouseIsDown = false;
    juce::Point<int> point;
    if (toMame (e.position, point))
        processor.enqueueMouseEvent (ves::EmbeddedMouseEventType::LeftUp, point.x, point.y);
    else if (lastMame.x >= 0)
        processor.enqueueMouseEvent (ves::EmbeddedMouseEventType::LeftUp, lastMame.x, lastMame.y);
    else
        processor.requestMouseRelease();
}

//==============================================================================
void LevelMeter::setLevels (float left, float right)
{
    const float in[2] { left, right };
    for (int ch = 0; ch < 2; ++ch)
    {
        levels[ch] = juce::jmax (in[ch], levels[ch] * 0.8f);        // fast attack, ~decay
        holds[ch] = juce::jmax (in[ch], holds[ch] - 0.01f);
    }
    repaint();
}

void LevelMeter::paint (juce::Graphics& g)
{
    const auto toProportion = [] (float value)
    {
        const auto db = juce::Decibels::gainToDecibels (value, -60.0f);
        return juce::jlimit (0.0f, 1.0f, (db + 60.0f) / 60.0f);
    };

    auto bounds = getLocalBounds().toFloat();
    const auto rowHeight = (bounds.getHeight() - 2.0f) / 2.0f;
    for (int ch = 0; ch < 2; ++ch)
    {
        auto row = bounds.removeFromTop (rowHeight);
        if (ch == 0)
            bounds.removeFromTop (2.0f);

        g.setColour (juce::Colours::black);
        g.fillRect (row);

        const auto lit = row.withWidth (row.getWidth() * toProportion (levels[ch]));
        g.setGradientFill (juce::ColourGradient::horizontal (juce::Colours::limegreen, row.getX(),
                                                             juce::Colours::red, row.getRight()));
        g.fillRect (lit);

        const auto holdX = row.getX() + row.getWidth() * toProportion (holds[ch]);
        g.setColour (holds[ch] >= 0.999f ? juce::Colours::red : juce::Colours::white);
        g.fillRect (holdX - 1.0f, row.getY(), 2.0f, row.getHeight());
    }
}

void MidiLamp::setActive (bool shouldBeActive)
{
    if (active != shouldBeActive)
    {
        active = shouldBeActive;
        repaint();
    }
}

void MidiLamp::paint (juce::Graphics& g)
{
    auto area = getLocalBounds().toFloat();
    const auto lamp = area.removeFromLeft (area.getHeight()).reduced (3.0f);
    g.setColour (active ? juce::Colours::orange : juce::Colour::fromRGB (70, 40, 20));
    g.fillEllipse (lamp);
    g.setColour (juce::Colours::black);
    g.drawEllipse (lamp, 1.0f);

    g.setColour (juce::Colours::white.withAlpha (0.8f));
    g.setFont (juce::FontOptions (12.0f));
    g.drawText ("MIDI", getLocalBounds().withTrimmedLeft (getHeight() + 2), juce::Justification::centredLeft);
}

//==============================================================================
void PanelConstrainer::checkBounds (juce::Rectangle<int>& bounds, const juce::Rectangle<int>& previous,
                                    const juce::Rectangle<int>& limits, bool isStretchingTop, bool isStretchingLeft,
                                    bool isStretchingBottom, bool isStretchingRight)
{
    const auto ratio = aspect != nullptr ? aspect() : 3.2;
    const auto verticalOnly = (isStretchingTop || isStretchingBottom) && ! (isStretchingLeft || isStretchingRight);

    if (verticalOnly)
        bounds.setWidth (juce::roundToInt ((bounds.getHeight() - chromeHeight) * ratio) + chromeWidth);
    else
        bounds.setHeight (heightForWidth (bounds.getWidth(), ratio));

    ComponentBoundsConstrainer::checkBounds (bounds, previous, limits, isStretchingTop, isStretchingLeft,
                                             isStretchingBottom, isStretchingRight);
}

//==============================================================================
CZ101Editor::CZ101Editor (CZ101Processor& p)
    : AudioProcessorEditor (&p),
      processor (p),
      display (p)
{
    addAndMakeVisible (display);
    addAndMakeVisible (meter);
    addAndMakeVisible (midiLamp);

    statusLabel.setColour (juce::Label::textColourId, juce::Colours::white.withAlpha (0.75f));
    statusLabel.setFont (juce::FontOptions (13.0f));
    addAndMakeVisible (statusLabel);

    romsButton.onClick = [this] { chooseRomsFolder(); };
    romsButton.setTooltip (processor.getRomsDirectory().getFullPathName());
    addAndMakeVisible (romsButton);

    restartButton.onClick = [this] { processor.restartEngine(); };
    addAndMakeVisible (restartButton);

    constrainer.aspect = [this] { return display.getPanelAspect(); };
    constrainer.setSizeLimits (720, 250, 3400, 1300);
    setConstrainer (&constrainer);
    setResizable (true, true);
    setSize (1400, PanelConstrainer::heightForWidth (1400, 3.2));

    enableNewMidiInputs();
    startTimerHz (30);
}

CZ101Editor::~CZ101Editor()
{
    stopTimer();
}

void CZ101Editor::paint (juce::Graphics& g)
{
    g.fillAll (backgroundColour);

    auto header = getLocalBounds().removeFromTop (44);
    g.setColour (headerColour);
    g.fillRect (header);

    g.setColour (juce::Colours::white);
    g.setFont (juce::FontOptions (20.0f, juce::Font::bold));
    g.drawText ("CASIO", header.removeFromLeft (84).withTrimmedLeft (14), juce::Justification::centredLeft);
    g.setColour (czBlue);
    g.drawText ("CZ-101", header.removeFromLeft (90), juce::Justification::centredLeft);
}

void CZ101Editor::resized()
{
    auto bounds = getLocalBounds();
    auto header = bounds.removeFromTop (44).reduced (8, 8);
    header.removeFromLeft (170);

    restartButton.setBounds (header.removeFromRight (84));
    header.removeFromRight (6);
    romsButton.setBounds (header.removeFromRight (74));
    header.removeFromRight (12);

    meter.setBounds (header.removeFromRight (160).reduced (0, 4));
    header.removeFromRight (12);
    midiLamp.setBounds (header.removeFromRight (56));
    header.removeFromRight (12);
    statusLabel.setBounds (header);

    display.setBounds (bounds.reduced (6));
}

void CZ101Editor::timerCallback()
{
    display.updateFrame();
    meter.setLevels (processor.takePeak (0), processor.takePeak (1));

    const auto lastMidi = processor.getLastMidiInMs();
    midiLamp.setActive (lastMidi != 0 && juce::Time::getMillisecondCounter() - lastMidi < 120);

    auto status = processor.getStatusText();
    const auto audio = processor.getAudioDiagnosticsText();
    if (audio.isNotEmpty())
        status << juce::String::fromUTF8 ("  —  ") << audio;
    if (statusLabel.getText() != status)
        statusLabel.setText (status, juce::dontSendNotification);

    if (++timerTicks % 60 == 0) // every 2 s
        enableNewMidiInputs();
}

void CZ101Editor::chooseRomsFolder()
{
    chooser = std::make_unique<juce::FileChooser> ("Pasta com cz101.zip", processor.getRomsDirectory());
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
                          [this] (const juce::FileChooser& fc)
                          {
                              const auto dir = fc.getResult();
                              if (dir.isDirectory())
                              {
                                  processor.setRomsDirectory (dir);
                                  romsButton.setTooltip (dir.getFullPathName());
                              }
                          });
}

void CZ101Editor::enableNewMidiInputs()
{
    // Standalone only: any MIDI input that appears (e.g. a keyboard switched on
    // after launch) is enabled once, so MIDI works without visiting Options.
    auto* holder = standaloneHolder();
    if (holder == nullptr)
        return;

    for (const auto& device : juce::MidiInput::getAvailableDevices())
    {
        if (knownMidiInputs.insert (device.identifier).second
            && ! holder->deviceManager.isMidiInputDeviceEnabled (device.identifier))
            holder->deviceManager.setMidiInputDeviceEnabled (device.identifier, true);
    }
}
