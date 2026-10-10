#include "PluginEditor.h"

namespace evil
{
namespace
{
    void styleLabel (juce::Label& label, float height, juce::Colour colour, bool bold = false)
    {
        label.setFont (gui::uiFont (height, bold));
        label.setColour (juce::Label::textColourId, colour);
        label.setJustificationType (juce::Justification::centredLeft);
        label.setMinimumHorizontalScale (0.7f);
    }

    juce::ComboBox& withItems (juce::ComboBox& box, const juce::StringArray& items)
    {
        box.addItemList (items, 1); // must happen before the attachment selects an item
        return box;
    }

    juce::String formatEsr (double esr)
    {
        return esr >= 0.0 ? juce::String (esr, esr < 0.01 ? 4 : 3) : juce::String ("n/a");
    }
} // namespace

EvilAmplifierEditor::EvilAmplifierEditor (EvilAmplifierProcessor& p)
    : AudioProcessorEditor (p),
      audioProcessor (p),
      inputKnob (p.getState(), params::input, "Input"),
      gateKnob (p.getState(), params::gateThreshold, "Gate"),
      gateAttachment (p.getState(), params::gateOn, gateButton),
      normaliseAttachment (p.getState(), params::normalise, normaliseButton),
      driveKnob (p.getState(), params::drive, "Drive"),
      toneKnob (p.getState(), params::tone, "Tone"),
      oversamplingAttachment (p.getState(), params::oversampling, withItems (oversamplingBox, { "Off", "2x", "4x" })),
      cabAttachment (p.getState(), params::cabOn, cabButton),
      bassKnob (p.getState(), params::bass, "Bass"),
      midKnob (p.getState(), params::mid, "Mid"),
      trebleKnob (p.getState(), params::treble, "Treble"),
      outputKnob (p.getState(), params::output, "Output")
{
    setLookAndFeel (&lookAndFeel);

    // Header
    canvas.addAndMakeVisible (presetBox);
    presetBox.setTooltip ("Presets: factory first, then yours");
    presetBox.onChange = [this] {
        const int index = presetBox.getSelectedItemIndex();
        if (index >= 0 && index != audioProcessor.getPresets().getCurrentIndex())
            audioProcessor.getPresets().load (index);
    };
    for (auto* b : { &prevPreset, &nextPreset, &savePreset })
        canvas.addAndMakeVisible (b);
    prevPreset.onClick = [this] { stepPreset (-1); };
    nextPreset.onClick = [this] { stepPreset (1); };
    savePreset.onClick = [this] { savePresetDialog(); };
    savePreset.setTooltip ("Save the current settings as a user preset");
    for (auto* l : { &cpuLabel, &latencyLabel })
    {
        styleLabel (*l, 12.0f, gui::colours::textDim);
        l->setJustificationType (juce::Justification::centredRight);
        canvas.addAndMakeVisible (l);
    }
    cpuLabel.setTooltip ("Processing time per block as a share of the block's real-time deadline (average / peak)");
    latencyLabel.setTooltip ("Latency reported to the host");
    refreshPresetList();

    // Input
    for (auto* c : std::initializer_list<juce::Component*> { &inputKnob, &gateKnob, &gateButton, &inputMeter })
        canvas.addAndMakeVisible (c);
    gateButton.setTooltip ("Noise gate before the amp");

    // Amp
    auto& modeParam = *p.getState().getParameter (params::ampMode);
    for (int i = 0; i < 3; ++i)
    {
        auto& b = modeButtons[i];
        b.setClickingTogglesState (false);
        b.onClick = [this, i] { modeAttachment->setValueAsCompleteGesture ((float) i); };
        canvas.addAndMakeVisible (b);
    }
    modeAttachment = std::make_unique<juce::ParameterAttachment> (modeParam, [this] (float value) {
        const int mode = juce::roundToInt (value);
        for (int i = 0; i < 3; ++i)
            modeButtons[i].setToggleState (i == mode, juce::dontSendNotification);
        refreshAmpSections();
    });
    modeAttachment->sendInitialUpdate();

    styleLabel (modelName, 18.0f, gui::colours::text, true);
    styleLabel (modelDetails, 12.0f, gui::colours::textDim);
    for (auto* c : std::initializer_list<juce::Component*> { &modelName, &modelDetails, &loadModelButton, &builtinModelButton,
                                                              &unloadModelButton, &normaliseButton, &driveKnob, &toneKnob,
                                                              &oversamplingLabel, &oversamplingBox })
        canvas.addAndMakeVisible (c);
    loadModelButton.onClick = [this] { chooseModelFile(); };
    builtinModelButton.onClick = [this] { showBuiltinModels(); };
    unloadModelButton.onClick = [this] { audioProcessor.unloadModel(); };
    loadModelButton.setTooltip ("Load a Neural Amp Modeler .nam capture");
    normaliseButton.setTooltip ("Match the model's output to -18 dBFS using its loudness metadata");
    oversamplingLabel.setText ("OVERSAMPLING", juce::dontSendNotification);
    styleLabel (oversamplingLabel, 12.0f, gui::colours::textDim, true);
    oversamplingBox.setTooltip ("Oversampling around the overdrive waveshaper (reduces aliasing)");

    // Cab
    styleLabel (irName, 16.0f, gui::colours::text, true);
    styleLabel (irDetails, 12.0f, gui::colours::textDim);
    for (auto* c : std::initializer_list<juce::Component*> { &irName, &irDetails, &loadIrButton, &builtinIrButton, &cabButton })
        canvas.addAndMakeVisible (c);
    loadIrButton.onClick = [this] { chooseIrFile(); };
    builtinIrButton.onClick = [this] { showBuiltinIrs(); };
    loadIrButton.setTooltip ("Load a cabinet impulse response (WAV, AIFF or FLAC)");

    // EQ + output
    for (auto* c : std::initializer_list<juce::Component*> { &bassKnob, &midKnob, &trebleKnob, &outputKnob, &outputMeter })
        canvas.addAndMakeVisible (c);

    audioProcessor.getModels().addChangeListener (this);
    refreshModelStatus();
    refreshIrStatus();
    refreshStats();

    addAndMakeVisible (canvas);
    setResizable (true, true);
    setResizeLimits (designWidth * 3 / 5, designHeight * 3 / 5, designWidth * 2, designHeight * 2);
    getConstrainer()->setFixedAspectRatio ((double) designWidth / designHeight);
    setSize (designWidth, designHeight);
    startTimerHz (30);
}

EvilAmplifierEditor::~EvilAmplifierEditor()
{
    stopTimer();
    audioProcessor.getModels().removeChangeListener (this);
    setLookAndFeel (nullptr);
}

void EvilAmplifierEditor::paint (juce::Graphics& g)
{
    g.fillAll (gui::colours::background); // behind any letterboxing
}

void EvilAmplifierEditor::resized()
{
    const float scale = juce::jmin ((float) getWidth() / designWidth, (float) getHeight() / designHeight);
    canvas.setBounds (0, 0, designWidth, designHeight);
    canvas.setTransform (juce::AffineTransform::scale (scale).translated ((getWidth() - designWidth * scale) * 0.5f,
                                                                          (getHeight() - designHeight * scale) * 0.5f));
}

void EvilAmplifierEditor::paintCanvas (juce::Graphics& g)
{
    g.fillAll (gui::colours::background);

    // Header
    g.setColour (gui::colours::accent);
    g.setFont (gui::uiFont (22.0f, true));
    g.drawText ("EVIL", 20, 14, 60, 30, juce::Justification::centredLeft);
    g.setColour (gui::colours::text);
    g.drawText ("AMPLIFIER", 72, 14, 140, 30, juce::Justification::centredLeft);
    g.setColour (gui::colours::panelEdge);
    g.fillRect (0, 59, designWidth, 1);

    gui::drawPanel (g, inputPanel, "Input");
    gui::drawPanel (g, ampPanel, "Amp");
    gui::drawPanel (g, cabPanel, "Cabinet");
    gui::drawPanel (g, eqPanel, "EQ");
    gui::drawPanel (g, outputPanel, "Output");

    // Amp sub-sections, the inactive one dimmed.
    const int mode = juce::roundToInt (audioProcessor.getState().getRawParameterValue (params::ampMode)->load());
    g.setFont (gui::uiFont (12.0f, true));
    g.setColour (mode == 2 ? gui::colours::text : gui::colours::textDim);
    g.drawText ("NEURAL MODEL", neuralArea.getX(), neuralArea.getY(), 200, 16, juce::Justification::centredLeft);
    g.setColour (mode == 1 ? gui::colours::text : gui::colours::textDim);
    g.drawText ("GRAY-BOX OVERDRIVE", overdriveArea.getX(), overdriveArea.getY(), 220, 16, juce::Justification::centredLeft);
    g.setColour (gui::colours::panelEdge);
    g.fillRect (overdriveArea.getX() - 14, overdriveArea.getY(), 1, overdriveArea.getHeight());

    // Gate activity LED
    const auto led = juce::Rectangle<float> (12.0f, 12.0f)
                         .withCentre ({ (float) inputPanel.getRight() - 26.0f, (float) inputPanel.getY() + 21.0f });
    const bool gateEnabled = audioProcessor.getState().getRawParameterValue (params::gateOn)->load() >= 0.5f;
    g.setColour (! gateEnabled ? gui::colours::control : audioProcessor.isGateOpen() ? gui::colours::good : gui::colours::accent);
    g.fillEllipse (led);
}

void EvilAmplifierEditor::layoutCanvas()
{
    const juce::Rectangle<int> design (designWidth, designHeight);

    // Header
    auto header = design.withHeight (60).reduced (16, 14);
    header.removeFromLeft (220);
    auto stats = header.removeFromRight (200);
    cpuLabel.setBounds (stats.removeFromTop (16));
    latencyLabel.setBounds (stats.removeFromTop (16));
    prevPreset.setBounds (header.removeFromLeft (32));
    header.removeFromLeft (6);
    presetBox.setBounds (header.removeFromLeft (280));
    header.removeFromLeft (6);
    nextPreset.setBounds (header.removeFromLeft (32));
    header.removeFromLeft (12);
    savePreset.setBounds (header.removeFromLeft (70));

    auto body = design.withTrimmedTop (60).reduced (16, 12);
    auto top = body.removeFromTop (262);
    body.removeFromTop (12);
    auto bottom = body;

    inputPanel = top.removeFromLeft (230);
    top.removeFromLeft (12);
    ampPanel = top;

    cabPanel = bottom.removeFromLeft (340);
    bottom.removeFromLeft (12);
    outputPanel = bottom.removeFromRight (210);
    bottom.removeFromRight (12);
    eqPanel = bottom;

    // Input: two knobs side by side, gate switch, meter on the right.
    {
        auto b = inputPanel.reduced (14).withTrimmedTop (30);
        inputMeter.setBounds (b.removeFromRight (14).withTrimmedBottom (4));
        b.removeFromRight (8);
        gateButton.setBounds (b.removeFromBottom (24).withTrimmedLeft (4));
        auto knobs = b.withTrimmedBottom (6);
        inputKnob.setBounds (knobs.removeFromLeft (knobs.getWidth() / 2));
        gateKnob.setBounds (knobs);
    }

    // Amp: mode selector, then neural (left) and overdrive (right).
    {
        auto b = ampPanel.reduced (14).withTrimmedTop (30);
        auto modes = b.removeFromTop (30).withWidth (330);
        for (auto& m : modeButtons)
        {
            m.setBounds (modes.removeFromLeft (106));
            modes.removeFromLeft (6);
        }
        b.removeFromTop (14);

        overdriveArea = b.removeFromRight (290);
        b.removeFromRight (28);
        neuralArea = b;

        auto n = neuralArea.withTrimmedTop (20);
        modelName.setBounds (n.removeFromTop (28));
        modelDetails.setBounds (n.removeFromTop (40));
        n.removeFromTop (10);
        auto buttons = n.removeFromTop (28);
        loadModelButton.setBounds (buttons.removeFromLeft (120));
        buttons.removeFromLeft (8);
        builtinModelButton.setBounds (buttons.removeFromLeft (84));
        buttons.removeFromLeft (8);
        unloadModelButton.setBounds (buttons.removeFromLeft (76));
        n.removeFromTop (10);
        normaliseButton.setBounds (n.removeFromTop (24).withWidth (200));

        auto o = overdriveArea.withTrimmedTop (20);
        auto osRow = o.removeFromBottom (26);
        oversamplingLabel.setBounds (osRow.removeFromLeft (110));
        oversamplingBox.setBounds (osRow.removeFromLeft (90));
        o.removeFromBottom (6);
        driveKnob.setBounds (o.removeFromLeft (o.getWidth() / 2));
        toneKnob.setBounds (o);
    }

    // Cab
    {
        auto b = cabPanel.reduced (14).withTrimmedTop (36);
        irName.setBounds (b.removeFromTop (26));
        irDetails.setBounds (b.removeFromTop (36));
        b.removeFromTop (10);
        auto buttons = b.removeFromTop (28);
        loadIrButton.setBounds (buttons.removeFromLeft (110));
        buttons.removeFromLeft (8);
        builtinIrButton.setBounds (buttons.removeFromLeft (84));
        b.removeFromTop (12);
        cabButton.setBounds (b.removeFromTop (24).withWidth (160));
    }

    // EQ
    {
        auto b = eqPanel.reduced (14).withTrimmedTop (30);
        const int w = b.getWidth() / 3;
        bassKnob.setBounds (b.removeFromLeft (w));
        midKnob.setBounds (b.removeFromLeft (w));
        trebleKnob.setBounds (b);
    }

    // Output
    {
        auto b = outputPanel.reduced (14).withTrimmedTop (30);
        outputMeter.setBounds (b.removeFromRight (14).withTrimmedBottom (4));
        b.removeFromRight (8);
        outputKnob.setBounds (b);
    }
}

void EvilAmplifierEditor::timerCallback()
{
    constexpr float interval = 1.0f / 30.0f;
    inputMeter.pushPeak (audioProcessor.takeInputPeak(), interval);
    outputMeter.pushPeak (audioProcessor.takeOutputPeak(), interval);

    if (++textRefreshCounter % 10 == 0) // ~3 Hz is plenty for text
    {
        refreshStats();
        refreshIrStatus();
        if (presetBox.getSelectedItemIndex() != audioProcessor.getPresets().getCurrentIndex())
            presetBox.setSelectedItemIndex (audioProcessor.getPresets().getCurrentIndex(), juce::dontSendNotification);
    }
    canvas.repaint (inputPanel.getRight() - 40, inputPanel.getY(), 40, 40); // gate LED
}

void EvilAmplifierEditor::refreshStats()
{
    auto& cpu = audioProcessor.getCpuStats();
    cpuLabel.setText (juce::String::formatted ("CPU %.1f%% / %.1f%% peak", 100.0 * cpu.average.load(),
                                               100.0 * cpu.peakForDisplay.exchange (0.0f)),
                      juce::dontSendNotification);
    const auto latency = audioProcessor.getLatencySamples();
    const auto rate = audioProcessor.getSampleRate();
    latencyLabel.setText ("Latency " + juce::String (latency) + " smp"
                              + (rate > 0 ? " (" + juce::String (1000.0 * latency / rate, 2) + " ms)" : juce::String()),
                          juce::dontSendNotification);
}

void EvilAmplifierEditor::changeListenerCallback (juce::ChangeBroadcaster*)
{
    refreshModelStatus();
}

void EvilAmplifierEditor::refreshModelStatus()
{
    using State = ModelManager::State;
    const auto status = audioProcessor.getModels().getStatus();
    switch (status.state)
    {
        case State::empty:
            modelName.setText ("No model loaded", juce::dontSendNotification);
            modelDetails.setText ("Load a .nam capture or pick a built-in one. Without a model the neural stage passes audio clean.",
                                  juce::dontSendNotification);
            break;
        case State::loading:
            modelName.setText ("Loading...", juce::dontSendNotification);
            modelDetails.setText (status.message, juce::dontSendNotification);
            break;
        case State::error:
            modelName.setText ("Model failed to load", juce::dontSendNotification);
            modelDetails.setText (status.message, juce::dontSendNotification);
            break;
        case State::ready:
        {
            const auto& i = status.info;
            modelName.setText (i.name, juce::dontSendNotification);
            juce::String details = i.architecture + ", " + juce::String (i.numWeights) + " weights, "
                                   + juce::String (i.sampleRate / 1000.0, 1) + " kHz";
            if (i.testEsr >= 0.0)
                details << ", held-out ESR " << formatEsr (i.testEsr);
            else if (i.validationEsr >= 0.0)
                details << ", validation ESR " << formatEsr (i.validationEsr);
            if (i.gear.isNotEmpty())
                details = i.gear + "\n" + details;
            if (! i.hasLoudness)
                details << "\n(no loudness metadata: normalisation unavailable)";
            modelDetails.setText (details, juce::dontSendNotification);
            break;
        }
    }
    modelDetails.setColour (juce::Label::textColourId,
                            status.state == State::error ? gui::colours::accent : gui::colours::textDim);
}

void EvilAmplifierEditor::refreshIrStatus()
{
    irName.setText (audioProcessor.getCab().getDisplayName(), juce::dontSendNotification);
    const auto error = audioProcessor.getLastIrError();
    irDetails.setText (error.isNotEmpty() ? error : juce::String ("Zero-latency convolution; IR swaps crossfade"),
                       juce::dontSendNotification);
    irDetails.setColour (juce::Label::textColourId, error.isNotEmpty() ? gui::colours::accent : gui::colours::textDim);
}

void EvilAmplifierEditor::refreshPresetList()
{
    auto& presets = audioProcessor.getPresets();
    presets.refreshUserPresets();
    presetBox.clear (juce::dontSendNotification);
    const auto names = presets.getNames();
    for (int i = 0; i < names.size(); ++i)
    {
        if (i == presets.getNumFactoryPresets() && i > 0)
            presetBox.addSeparator();
        presetBox.addItem (names[i], i + 1);
    }
    presetBox.setSelectedItemIndex (presets.getCurrentIndex(), juce::dontSendNotification);
}

void EvilAmplifierEditor::refreshAmpSections()
{
    const int mode = juce::roundToInt (audioProcessor.getState().getRawParameterValue (params::ampMode)->load());
    for (auto* c : std::initializer_list<juce::Component*> { &driveKnob, &toneKnob, &oversamplingBox, &oversamplingLabel })
        c->setAlpha (mode == 1 ? 1.0f : 0.45f);
    for (auto* c : std::initializer_list<juce::Component*> { &modelName, &modelDetails, &loadModelButton, &builtinModelButton,
                                                              &unloadModelButton, &normaliseButton })
        c->setAlpha (mode == 2 ? 1.0f : 0.45f);
    canvas.repaint (ampPanel);
}

void EvilAmplifierEditor::chooseModelFile()
{
    const auto start = lastModelDirectory.isDirectory() ? lastModelDirectory
                                                        : juce::File::getSpecialLocation (juce::File::userHomeDirectory);
    chooser = std::make_unique<juce::FileChooser> ("Load a NAM model", start, "*.nam");
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [this] (const juce::FileChooser& fc) {
                              const auto file = fc.getResult();
                              if (! file.existsAsFile())
                                  return;
                              lastModelDirectory = file.getParentDirectory();
                              audioProcessor.loadModel (file.getFullPathName());
                          });
}

void EvilAmplifierEditor::chooseIrFile()
{
    const auto start = lastIrDirectory.isDirectory() ? lastIrDirectory
                                                     : juce::File::getSpecialLocation (juce::File::userHomeDirectory);
    chooser = std::make_unique<juce::FileChooser> ("Load a cabinet IR", start, "*.wav;*.aif;*.aiff;*.flac");
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [this] (const juce::FileChooser& fc) {
                              const auto file = fc.getResult();
                              if (! file.existsAsFile())
                                  return;
                              lastIrDirectory = file.getParentDirectory();
                              juce::String error;
                              audioProcessor.loadIr (file.getFullPathName(), error);
                              refreshIrStatus();
                          });
}

void EvilAmplifierEditor::showBuiltinModels()
{
    juce::PopupMenu menu;
    const auto names = ModelManager::getBuiltinNames();
    for (int i = 0; i < names.size(); ++i)
        menu.addItem (i + 1, names[i]);
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (builtinModelButton),
                        [this, names] (int result) {
                            if (result > 0)
                                audioProcessor.loadModel (ModelManager::builtinPrefix + names[result - 1]);
                        });
}

void EvilAmplifierEditor::showBuiltinIrs()
{
    juce::PopupMenu menu;
    const auto names = CabSim::getBuiltinNames();
    for (int i = 0; i < names.size(); ++i)
        menu.addItem (i + 1, names[i]);
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (builtinIrButton),
                        [this, names] (int result) {
                            if (result <= 0)
                                return;
                            juce::String error;
                            audioProcessor.loadIr (CabSim::builtinPrefix + names[result - 1], error);
                            refreshIrStatus();
                        });
}

void EvilAmplifierEditor::stepPreset (int delta)
{
    auto& presets = audioProcessor.getPresets();
    const int count = presets.getNames().size();
    if (count == 0)
        return;
    const int index = (presets.getCurrentIndex() + delta + count) % count;
    if (presets.load (index))
        presetBox.setSelectedItemIndex (index, juce::dontSendNotification);
}

void EvilAmplifierEditor::savePresetDialog()
{
    auto* window = new juce::AlertWindow ("Save preset", "Name your preset. It is saved to\n"
                                                             + PresetManager::getUserPresetDirectory().getFullPathName(),
                                          juce::MessageBoxIconType::NoIcon, this);
    window->addTextEditor ("name", audioProcessor.getPresets().getCurrentName());
    window->addButton ("Save", 1, juce::KeyPress (juce::KeyPress::returnKey));
    window->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
    window->setLookAndFeel (&lookAndFeel);

    juce::Component::SafePointer<EvilAmplifierEditor> safeThis (this);
    window->enterModalState (true, juce::ModalCallbackFunction::create ([safeThis, window] (int result) {
                                 // Runs before the window is deleted (deleteWhenDismissed below).
                                 if (safeThis == nullptr || result != 1)
                                     return;
                                 juce::String error;
                                 if (! safeThis->audioProcessor.getPresets().saveUserPreset (window->getTextEditorContents ("name"), error))
                                 {
                                     juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon,
                                                                             "Preset not saved", error);
                                     return;
                                 }
                                 safeThis->refreshPresetList();
                             }),
                             true);
}
} // namespace evil
