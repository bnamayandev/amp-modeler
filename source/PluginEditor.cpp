#include "PluginEditor.h"

namespace evil {
namespace {
void styleLabel(juce::Label &label, const juce::Font &font,
                juce::Colour colour) {
  label.setFont(font);
  label.setColour(juce::Label::textColourId, colour);
  label.setJustificationType(juce::Justification::centredLeft);
  label.setMinimumHorizontalScale(0.7f);
}

juce::ComboBox &withItems(juce::ComboBox &box, const juce::StringArray &items) {
  box.addItemList(items,
                  1); // must happen before the attachment selects an item
  return box;
}

juce::String formatEsr(double esr) {
  return esr >= 0.0 ? juce::String(esr, esr < 0.01 ? 4 : 3)
                    : juce::String("n/a");
}
} // namespace

EvilAmplifierEditor::EvilAmplifierEditor(EvilAmplifierProcessor &p)
    : AudioProcessorEditor(p), audioProcessor(p), pedalBoard(p.getState()),
      inputKnob(p.getState(), params::input, "Input"),
      gateKnob(p.getState(), params::gateThreshold, "Gate"),
      gateAttachment(p.getState(), params::gateOn, gateButton),
      cabAttachment(p.getState(), params::cabOn, cabButton),
      outputKnob(p.getState(), params::output, "Output"),
      bpmAttachment(p.getState(), params::metronomeBpm, bpmSlider),
      metronomeLevelAttachment(p.getState(), params::metronomeLevel,
                               metronomeLevelSlider),
      beatsAttachment(p.getState(), params::metronomeBeats,
                      withItems(beatsBox, {"2/4", "3/4", "4/4", "5/4", "6/4",
                                           "7/4"})) {
  // Header
  for (auto [page, tab] : {std::pair{0, &ampTab}, std::pair{1, &pedalsTab}}) {
    gui::setTab(*tab);
    tab->onClick = [this, page = page] { showPage(page); };
    canvas.addAndMakeVisible(tab);
  }
  ampTab.setTooltip("Amp, cabinet and EQ");
  pedalsTab.setTooltip("Stomp boxes before and after the amp");
  canvas.addAndMakeVisible(presetBox);
  presetBox.setTooltip("Presets: factory first, then yours");
  presetBox.onChange = [this] {
    const int index = presetBox.getSelectedItemIndex();
    if (index >= 0 && index != audioProcessor.getPresets().getCurrentIndex())
      audioProcessor.getPresets().load(index);
  };
  for (auto *b : {&prevPreset, &nextPreset, &savePreset})
    canvas.addAndMakeVisible(b);
  prevPreset.onClick = [this] { stepPreset(-1); };
  nextPreset.onClick = [this] { stepPreset(1); };
  savePreset.onClick = [this] { savePresetDialog(); };
  savePreset.setTooltip("Save the current settings as a user preset");
  gui::setPrimary(savePreset);
  refreshPresetList();

  // Input
  for (auto *c : std::initializer_list<juce::Component *>{
           &inputKnob, &gateKnob, &gateButton, &inputMeter})
    ampPage.addAndMakeVisible(c);
  gateButton.setTooltip("Noise gate before the amp");

  // Amp
  styleLabel(modelName, gui::sans(19.0f, true, -0.015f), gui::colours::text);
  styleLabel(modelDetails, gui::mono(11.0f), gui::colours::muted);
  modelDetails.setJustificationType(juce::Justification::topLeft);
  for (auto *c : std::initializer_list<juce::Component *>{
           &modelName, &modelDetails, &loadModelButton, &builtinModelButton,
           &unloadModelButton})
    ampPage.addAndMakeVisible(c);
  loadModelButton.onClick = [this] { chooseModelFile(); };
  builtinModelButton.onClick = [this] { showBuiltinModels(); };
  unloadModelButton.onClick = [this] { audioProcessor.unloadModel(); };
  loadModelButton.setTooltip("Load a Neural Amp Modeler .nam capture");
  unloadModelButton.setTooltip("Play without a model: the amp passes clean");

  // Cab
  styleLabel(irName, gui::sans(17.0f, true, -0.015f), gui::colours::text);
  styleLabel(irDetails, gui::mono(11.0f), gui::colours::muted);
  for (auto *c : std::initializer_list<juce::Component *>{
           &irName, &irDetails, &loadIrButton, &builtinIrButton, &cabButton})
    ampPage.addAndMakeVisible(c);
  loadIrButton.onClick = [this] { chooseIrFile(); };
  builtinIrButton.onClick = [this] { showBuiltinIrs(); };
  loadIrButton.setTooltip(
      "Load a cabinet impulse response (WAV, AIFF or FLAC)");

  // EQ + output
  for (size_t b = 0; b < eqFaders.size(); ++b) {
    eqFaders[b] = std::make_unique<gui::Fader>(p.getState(), params::eqBands[b],
                                               params::eqBandLabel(b));
    ampPage.addAndMakeVisible(*eqFaders[b]);
  }
  for (auto *c :
       std::initializer_list<juce::Component *>{&outputKnob, &outputMeter})
    ampPage.addAndMakeVisible(c);

  ampPage.setInterceptsMouseClicks(false, true);
  canvas.addAndMakeVisible(ampPage);
  canvas.addChildComponent(pedalBoard);

  // Footer: metronome
  canvas.addAndMakeVisible(metronomeButton);
  metronomeButton.onClick = [this] {
    audioProcessor.setMetronomeRunning(!audioProcessor.isMetronomeRunning());
    refreshMetronome();
  };
  metronomeButton.setTooltip("Start or stop the click (mixed into the output)");
  canvas.addAndMakeVisible(tapButton);
  tapButton.onClick = [this] { tapTempo(); };
  tapButton.setTooltip(
      "Tap the tempo; each tap also restarts the bar on the beat");
  for (auto *slider : {&bpmSlider, &metronomeLevelSlider}) {
    slider->setSliderSnapsToMousePosition(false);
    canvas.addAndMakeVisible(slider);
  }
  bpmSlider.setTooltip("Tempo (drag, or double-click to type)");
  metronomeLevelSlider.setTooltip("Click level (drag, or double-click to type)");
  canvas.addAndMakeVisible(beatsBox);
  beatsBox.setTooltip("Beats per bar; beat one clicks higher");

  // Footer: transposer
  for (auto [delta, button] :
       {std::pair{-1, &transposeDown}, std::pair{1, &transposeUp}}) {
    button->onClick = [this, delta = delta] { stepTranspose(delta); };
    canvas.addAndMakeVisible(button);
  }
  transposeDown.setTooltip("Transpose down a semitone");
  transposeUp.setTooltip("Transpose up a semitone");
  styleLabel(transposeLabel, gui::mono(12.5f), gui::colours::textSoft);
  transposeLabel.setJustificationType(juce::Justification::centred);
  transposeLabel.setTooltip(
      "Transposer: shifts everything you play by semitones, e.g. -1 for "
      "E-flat tuning. Presets leave it as it is.");
  canvas.addAndMakeVisible(transposeLabel);
  transposeAttachment = std::make_unique<juce::ParameterAttachment>(
      *p.getState().getParameter(params::transpose), [this](float value) {
        const int st = juce::roundToInt(value);
        transposeLabel.setText(st == 0 ? juce::String("off")
                                       : (st > 0 ? "+" : "") +
                                             juce::String(st) + " st",
                               juce::dontSendNotification);
        transposeLabel.setColour(juce::Label::textColourId,
                                 st == 0 ? gui::colours::muted
                                         : gui::colours::accent);
      });
  transposeAttachment->sendInitialUpdate();

  // Footer: stats
  for (auto *l : {&cpuLabel, &latencyLabel}) {
    styleLabel(*l, gui::mono(11.5f), gui::colours::text);
    l->setJustificationType(juce::Justification::centredRight);
    canvas.addAndMakeVisible(l);
  }
  cpuLabel.setTooltip("Processing time per block as a share of the block's "
                      "real-time deadline (average / peak)");
  latencyLabel.setTooltip("Latency reported to the host");

  audioProcessor.getModels().addChangeListener(this);
  refreshModelStatus();
  refreshIrStatus();
  refreshStats();
  refreshMetronome();
  showPage(audioProcessor.editorPage);

  addAndMakeVisible(canvas);
  // After every child exists: sliders build their value boxes from the
  // look-and-feel they see when it changes.
  setLookAndFeel(&lookAndFeel);
  setResizable(true, true);
  setResizeLimits(designWidth * 3 / 5, designHeight * 3 / 5, designWidth * 2,
                  designHeight * 2);
  getConstrainer()->setFixedAspectRatio((double)designWidth / designHeight);
  setSize(designWidth, designHeight);
  startTimerHz(30);
}

EvilAmplifierEditor::~EvilAmplifierEditor() {
  stopTimer();
  audioProcessor.getModels().removeChangeListener(this);
  setLookAndFeel(nullptr);
}

void EvilAmplifierEditor::paint(juce::Graphics &g) {
  g.fillAll(gui::colours::background); // behind any letterboxing
}

void EvilAmplifierEditor::resized() {
  const float scale = juce::jmin((float)getWidth() / designWidth,
                                 (float)getHeight() / designHeight);
  canvas.setBounds(0, 0, designWidth, designHeight);
  canvas.setTransform(juce::AffineTransform::scale(scale).translated(
      (getWidth() - designWidth * scale) * 0.5f,
      (getHeight() - designHeight * scale) * 0.5f));
}

void EvilAmplifierEditor::paintCanvas(juce::Graphics &g) {
  using namespace gui::colours;
  g.fillAll(background);

  // Header: lowercase name, "evil" under a marker-pen highlight.
  {
    const auto font = gui::sans(25.0f, true, -0.035f);
    const auto evilWidth = juce::GlyphArrangement::getStringWidth(font, "evil");
    g.setColour(background.interpolatedWith(accent, 0.32f));
    g.fillRect(juce::Rectangle<float>(18.0f, 19.0f, evilWidth + 6.0f, 23.0f));
    g.setFont(font);
    g.setColour(text);
    g.drawText("evil", juce::Rectangle<float>(21.0f, 14.0f, 80.0f, 32.0f),
               juce::Justification::centredLeft, false);
    g.drawText("amplifier",
               juce::Rectangle<float>(21.0f + evilWidth + 9.0f, 14.0f, 140.0f,
                                      32.0f),
               juce::Justification::centredLeft, false);
  }
  g.setColour(line);
  g.fillRect(0, headerHeight - 1, designWidth, 1);

  paintFooter(g);
  if (!ampPage.isVisible())
    return;

  const auto inputRule = gui::drawPanel(g, inputPanel, 1, "Input");
  gui::drawPanel(g, ampPanel, 2, "Amp");
  gui::drawPanel(g, cabPanel, 3, "Cabinet");
  gui::drawPanel(g, eqPanel, 4, "EQ");
  gui::drawPanel(g, outputPanel, 5, "Output");

  // Gate activity LED, at the end of the input panel's header rule.
  gateLed = juce::Rectangle<float>(8.0f, 8.0f).withCentre(
      {inputRule.getRight() - 4.0f, inputRule.getCentreY()});
  const bool gateEnabled =
      audioProcessor.getState().getRawParameterValue(params::gateOn)->load() >=
      0.5f;
  const bool gateOpen = gateEnabled && audioProcessor.isGateOpen();
  g.setColour(panel);
  g.fillRect(gateLed.expanded(8.0f, 0.0f).withHeight(2.0f).withCentre(
      gateLed.getCentre())); // gap in the rule
  if (gateOpen) {
    g.setGradientFill(juce::ColourGradient(
        accent.withAlpha(0.55f), gateLed.getCentre(), accent.withAlpha(0.0f),
        gateLed.getCentre().translated(10.0f, 0.0f), true));
    g.fillEllipse(gateLed.expanded(6.0f));
  }
  g.setColour(gateOpen ? accent : gateEnabled ? mutedSoft : lineSoft);
  g.fillEllipse(gateLed);
  if (!gateEnabled) {
    g.setColour(line);
    g.drawEllipse(gateLed.reduced(0.5f), 1.0f);
  }
}

void EvilAmplifierEditor::layoutCanvas() {
  const juce::Rectangle<int> design(designWidth, designHeight);

  // Header: name, tabs (underlined on the header rule), presets.
  auto header = design.withHeight(headerHeight).reduced(16, 14);
  header.removeFromLeft(196);
  {
    auto tabs = header.removeFromLeft(156).withBottom(headerHeight);
    ampTab.setBounds(tabs.removeFromLeft(60));
    tabs.removeFromLeft(4);
    pedalsTab.setBounds(tabs);
  }
  header.removeFromLeft(24);
  header = header.withSizeKeepingCentre(header.getWidth(), 30);
  prevPreset.setBounds(header.removeFromLeft(30));
  header.removeFromLeft(6);
  presetBox.setBounds(header.removeFromLeft(280));
  header.removeFromLeft(6);
  nextPreset.setBounds(header.removeFromLeft(30));
  header.removeFromLeft(12);
  savePreset.setBounds(header.removeFromLeft(70));

  // Footer: metronome on the left; transposer and stats on the right.
  {
    auto footer = design.withTop(designHeight - footerHeight).reduced(16, 10);
    footer.removeFromRight(14); // clear of the window's resize corner
    const auto next = [&footer](int width, int gapAfter) {
      const auto r = footer.removeFromLeft(width);
      footer.removeFromLeft(gapAfter);
      return r;
    };
    metronomeCaption = next(74, 8);
    metronomeButton.setBounds(next(56, 6));
    bpmSlider.setBounds(next(84, 6));
    tapButton.setBounds(next(44, 6));
    beatsBox.setBounds(next(62, 6));
    metronomeLevelSlider.setBounds(next(72, 12));
    beatLeds = next(7 * 11, 0);

    auto stats = footer.removeFromRight(170).withSizeKeepingCentre(170, 30);
    cpuLabel.setBounds(stats.removeFromTop(15));
    latencyLabel.setBounds(stats);
    footer.removeFromRight(24);
    transposeUp.setBounds(footer.removeFromRight(28));
    footer.removeFromRight(4);
    transposeLabel.setBounds(footer.removeFromRight(56));
    footer.removeFromRight(4);
    transposeDown.setBounds(footer.removeFromRight(28));
    footer.removeFromRight(8);
    transposeCaption = footer.removeFromRight(70);
  }

  ampPage.setBounds(design);
  auto body = design.withTrimmedTop(headerHeight)
                  .withTrimmedBottom(footerHeight)
                  .reduced(16, 12);
  pedalBoard.setBounds(body);
  auto top = body.removeFromTop(262);
  body.removeFromTop(12);
  auto bottom = body;

  inputPanel = top.removeFromLeft(230);
  top.removeFromLeft(12);
  ampPanel = top;

  cabPanel = bottom.removeFromLeft(340);
  bottom.removeFromLeft(12);
  outputPanel = bottom.removeFromRight(210);
  bottom.removeFromRight(12);
  eqPanel = bottom;

  // Input: two knobs side by side, gate switch, meter on the right.
  {
    auto b = inputPanel.reduced(14).withTrimmedTop(30);
    inputMeter.setBounds(b.removeFromRight(6).withTrimmedBottom(4));
    b.removeFromRight(8);
    gateButton.setBounds(b.removeFromBottom(24).withTrimmedLeft(4));
    auto knobs = b.withTrimmedBottom(6);
    inputKnob.setBounds(knobs.removeFromLeft(knobs.getWidth() / 2));
    gateKnob.setBounds(knobs);
  }

  // Amp: the loaded model, as the cab shows its IR.
  {
    auto b = ampPanel.reduced(14).withTrimmedTop(36);
    modelName.setBounds(b.removeFromTop(28));
    modelDetails.setBounds(b.removeFromTop(40));
    b.removeFromTop(10);
    auto buttons = b.removeFromTop(28);
    loadModelButton.setBounds(buttons.removeFromLeft(120));
    buttons.removeFromLeft(8);
    builtinModelButton.setBounds(buttons.removeFromLeft(84));
    buttons.removeFromLeft(8);
    unloadModelButton.setBounds(buttons.removeFromLeft(76));
  }

  // Cab
  {
    auto b = cabPanel.reduced(14).withTrimmedTop(36);
    irName.setBounds(b.removeFromTop(26));
    irDetails.setBounds(b.removeFromTop(36));
    b.removeFromTop(10);
    auto buttons = b.removeFromTop(28);
    loadIrButton.setBounds(buttons.removeFromLeft(110));
    buttons.removeFromLeft(8);
    builtinIrButton.setBounds(buttons.removeFromLeft(84));
    b.removeFromTop(12);
    cabButton.setBounds(b.removeFromTop(24).withWidth(160));
  }

  // EQ
  {
    auto b = eqPanel.reduced(14).withTrimmedTop(30);
    const auto edge = [&b, this](size_t i) {
      return b.getX() + (int)(i * (size_t)b.getWidth() / eqFaders.size());
    };
    for (size_t i = 0; i < eqFaders.size(); ++i)
      eqFaders[i]->setBounds(b.withLeft(edge(i)).withRight(edge(i + 1)));
  }

  // Output
  {
    auto b = outputPanel.reduced(14).withTrimmedTop(30);
    outputMeter.setBounds(b.removeFromRight(6).withTrimmedBottom(4));
    b.removeFromRight(8);
    outputKnob.setBounds(b);
  }
}

void EvilAmplifierEditor::paintFooter(juce::Graphics &g) {
  using namespace gui::colours;
  g.setColour(line);
  g.fillRect(0, designHeight - footerHeight, designWidth, 1);

  g.setFont(gui::caption());
  g.setColour(muted);
  g.drawText("METRONOME", metronomeCaption, juce::Justification::centredLeft);
  g.drawText("TRANSPOSE", transposeCaption, juce::Justification::centredRight);
  g.drawText("CPU", cpuLabel.getBounds(), juce::Justification::centredLeft);
  g.drawText("LATENCY", latencyLabel.getBounds(),
             juce::Justification::centredLeft);

  // One light per beat; the current one flashes on its click, beat one in the
  // accent.
  const int beats =
      2 + juce::roundToInt(audioProcessor.getState()
                               .getRawParameterValue(params::metronomeBeats)
                               ->load());
  const int current = audioProcessor.isMetronomeRunning()
                          ? audioProcessor.getMetronome().getBeatInBar()
                          : -1;
  const bool flashing =
      juce::Time::getMillisecondCounterHiRes() - lastClickTime < 130.0;
  for (int i = 0; i < beats; ++i) {
    const auto led = juce::Rectangle<float>(7.0f, 7.0f).withCentre(
        {(float)beatLeds.getX() + 5.5f + 11.0f * (float)i,
         (float)beatLeds.getCentreY()});
    if (i == current && flashing)
      gui::drawLed(g, led, true, i == 0 ? accent : text);
    else if (i == current) {
      g.setColour(muted);
      g.fillEllipse(led);
    } else
      gui::drawLed(g, led, false, accent);
  }
}

void EvilAmplifierEditor::timerCallback() {
  constexpr float interval = 1.0f / 30.0f;
  inputMeter.pushPeak(audioProcessor.takeInputPeak(), interval);
  outputMeter.pushPeak(audioProcessor.takeOutputPeak(), interval);

  const auto clicks = audioProcessor.getMetronome().getClickCount();
  if (clicks != lastClickCount) {
    lastClickCount = clicks;
    lastClickTime = juce::Time::getMillisecondCounterHiRes();
  }
  canvas.repaint(beatLeds.expanded(6));

  if (++textRefreshCounter % 10 == 0) // ~3 Hz is plenty for text
  {
    refreshStats();
    refreshIrStatus();
    refreshPedalsTab();
    refreshMetronome();
    if (presetBox.getSelectedItemIndex() !=
        audioProcessor.getPresets().getCurrentIndex())
      presetBox.setSelectedItemIndex(
          audioProcessor.getPresets().getCurrentIndex(),
          juce::dontSendNotification);
  }
  if (ampPage.isVisible())
    canvas.repaint(gateLed.expanded(10.0f).getSmallestIntegerContainer());
}

void EvilAmplifierEditor::showPage(int page) {
  page = juce::jlimit(0, 1, page);
  audioProcessor.editorPage = page;
  ampPage.setVisible(page == 0);
  pedalBoard.setVisible(page == 1);
  ampTab.setToggleState(page == 0, juce::dontSendNotification);
  pedalsTab.setToggleState(page == 1, juce::dontSendNotification);
  refreshPedalsTab();
  canvas.repaint();
}

void EvilAmplifierEditor::refreshPedalsTab() {
  // The count keeps engaged pedals visible from the amp page.
  const int active = pedalBoard.getNumActive();
  pedalsTab.setButtonText(
      active > 0 ? "pedals " + juce::String::charToString(0x00b7) + " " +
                       juce::String(active)
                 : juce::String("pedals"));
}

void EvilAmplifierEditor::refreshMetronome() {
  const bool running = audioProcessor.isMetronomeRunning();
  metronomeButton.setButtonText(running ? "stop" : "start");
  metronomeButton.setToggleState(running, juce::dontSendNotification);
}

void EvilAmplifierEditor::tapTempo() {
  const double now = juce::Time::getMillisecondCounterHiRes();
  if (!tapTimes.empty() && now - tapTimes.back() > 2000.0)
    tapTimes.clear(); // a pause starts a new count
  tapTimes.push_back(now);
  if (tapTimes.size() > 5)
    tapTimes.erase(tapTimes.begin());
  audioProcessor.getMetronome().restartBar();
  if (tapTimes.size() < 2)
    return;

  const double interval =
      (tapTimes.back() - tapTimes.front()) / (double)(tapTimes.size() - 1);
  const float bpm = juce::jlimit(Metronome::minBpm, Metronome::maxBpm,
                                 (float)std::round(60000.0 / interval));
  auto *param = audioProcessor.getState().getParameter(params::metronomeBpm);
  param->beginChangeGesture();
  param->setValueNotifyingHost(param->convertTo0to1(bpm));
  param->endChangeGesture();
}

void EvilAmplifierEditor::stepTranspose(int delta) {
  const int current = juce::roundToInt(
      audioProcessor.getState().getRawParameterValue(params::transpose)->load());
  transposeAttachment->setValueAsCompleteGesture((float)juce::jlimit(
      -PitchShifter::maxSemitones, PitchShifter::maxSemitones, current + delta));
}

void EvilAmplifierEditor::refreshStats() {
  auto &cpu = audioProcessor.getCpuStats();
  cpuLabel.setText(juce::String::formatted(
                       "%.1f%% / %.1f%% peak", 100.0 * cpu.average.load(),
                       100.0 * cpu.peakForDisplay.exchange(0.0f)),
                   juce::dontSendNotification);
  const auto latency = audioProcessor.getLatencySamples();
  const auto rate = audioProcessor.getSampleRate();
  latencyLabel.setText(
      juce::String(latency) + " smp" +
          (rate > 0 ? " (" + juce::String(1000.0 * latency / rate, 2) + " ms)"
                    : juce::String()),
      juce::dontSendNotification);
}

void EvilAmplifierEditor::changeListenerCallback(juce::ChangeBroadcaster *) {
  refreshModelStatus();
}

void EvilAmplifierEditor::refreshModelStatus() {
  using State = ModelManager::State;
  const auto status = audioProcessor.getModels().getStatus();
  switch (status.state) {
  case State::empty:
    modelName.setText("No model loaded", juce::dontSendNotification);
    modelDetails.setText("Load a .nam capture or pick a built-in one. Without "
                         "a model the amp passes audio clean.",
                         juce::dontSendNotification);
    break;
  case State::loading:
    modelName.setText("Loading...", juce::dontSendNotification);
    modelDetails.setText(status.message, juce::dontSendNotification);
    break;
  case State::error:
    modelName.setText("Model failed to load", juce::dontSendNotification);
    modelDetails.setText(status.message, juce::dontSendNotification);
    break;
  case State::ready: {
    const auto &i = status.info;
    modelName.setText(i.name, juce::dontSendNotification);
    juce::String details = i.architecture + ", " + juce::String(i.numWeights) +
                           " weights, " +
                           juce::String(i.sampleRate / 1000.0, 1) + " kHz";
    if (i.testEsr >= 0.0)
      details << ", held-out ESR " << formatEsr(i.testEsr);
    else if (i.validationEsr >= 0.0)
      details << ", validation ESR " << formatEsr(i.validationEsr);
    if (i.gear.isNotEmpty())
      details = i.gear + "\n" + details;
    modelDetails.setText(details, juce::dontSendNotification);
    break;
  }
  }
  modelDetails.setColour(juce::Label::textColourId,
                         status.state == State::error ? gui::colours::accent
                                                      : gui::colours::muted);
}

void EvilAmplifierEditor::refreshIrStatus() {
  irName.setText(audioProcessor.getCab().getDisplayName(),
                 juce::dontSendNotification);
  const auto error = audioProcessor.getLastIrError();
  irDetails.setText(
      error.isNotEmpty()
          ? error
          : juce::String("Zero-latency convolution; IR swaps crossfade"),
      juce::dontSendNotification);
  irDetails.setColour(juce::Label::textColourId, error.isNotEmpty()
                                                     ? gui::colours::accent
                                                     : gui::colours::muted);
}

void EvilAmplifierEditor::refreshPresetList() {
  auto &presets = audioProcessor.getPresets();
  presets.refreshUserPresets();
  presetBox.clear(juce::dontSendNotification);
  const auto names = presets.getNames();
  for (int i = 0; i < names.size(); ++i) {
    if (i == presets.getNumFactoryPresets() && i > 0)
      presetBox.addSeparator();
    presetBox.addItem(names[i], i + 1);
  }
  presetBox.setSelectedItemIndex(presets.getCurrentIndex(),
                                 juce::dontSendNotification);
}

void EvilAmplifierEditor::chooseModelFile() {
  const auto start =
      lastModelDirectory.isDirectory()
          ? lastModelDirectory
          : juce::File::getSpecialLocation(juce::File::userHomeDirectory);
  chooser =
      std::make_unique<juce::FileChooser>("Load a NAM model", start, "*.nam");
  chooser->launchAsync(juce::FileBrowserComponent::openMode |
                           juce::FileBrowserComponent::canSelectFiles,
                       [this](const juce::FileChooser &fc) {
                         const auto file = fc.getResult();
                         if (!file.existsAsFile())
                           return;
                         lastModelDirectory = file.getParentDirectory();
                         audioProcessor.loadModel(file.getFullPathName());
                       });
}

void EvilAmplifierEditor::chooseIrFile() {
  const auto start =
      lastIrDirectory.isDirectory()
          ? lastIrDirectory
          : juce::File::getSpecialLocation(juce::File::userHomeDirectory);
  chooser = std::make_unique<juce::FileChooser>("Load a cabinet IR", start,
                                                "*.wav;*.aif;*.aiff;*.flac");
  chooser->launchAsync(juce::FileBrowserComponent::openMode |
                           juce::FileBrowserComponent::canSelectFiles,
                       [this](const juce::FileChooser &fc) {
                         const auto file = fc.getResult();
                         if (!file.existsAsFile())
                           return;
                         lastIrDirectory = file.getParentDirectory();
                         juce::String error;
                         audioProcessor.loadIr(file.getFullPathName(), error);
                         refreshIrStatus();
                       });
}

void EvilAmplifierEditor::showBuiltinModels() {
  juce::PopupMenu menu;
  const auto names = ModelManager::getBuiltinNames();
  for (int i = 0; i < names.size(); ++i)
    menu.addItem(i + 1, names[i]);
  menu.showMenuAsync(
      juce::PopupMenu::Options().withTargetComponent(builtinModelButton),
      [this, names](int result) {
        if (result > 0)
          audioProcessor.loadModel(ModelManager::builtinPrefix +
                                   names[result - 1]);
      });
}

void EvilAmplifierEditor::showBuiltinIrs() {
  juce::PopupMenu menu;
  const auto names = CabSim::getBuiltinNames();
  for (int i = 0; i < names.size(); ++i)
    menu.addItem(i + 1, names[i]);
  menu.showMenuAsync(
      juce::PopupMenu::Options().withTargetComponent(builtinIrButton),
      [this, names](int result) {
        if (result <= 0)
          return;
        juce::String error;
        audioProcessor.loadIr(CabSim::builtinPrefix + names[result - 1], error);
        refreshIrStatus();
      });
}

void EvilAmplifierEditor::stepPreset(int delta) {
  auto &presets = audioProcessor.getPresets();
  const int count = presets.getNames().size();
  if (count == 0)
    return;
  const int index = (presets.getCurrentIndex() + delta + count) % count;
  if (presets.load(index))
    presetBox.setSelectedItemIndex(index, juce::dontSendNotification);
}

void EvilAmplifierEditor::savePresetDialog() {
  auto *window = new juce::AlertWindow(
      "Save preset",
      "Name your preset. It is saved to\n" +
          PresetManager::getUserPresetDirectory().getFullPathName(),
      juce::MessageBoxIconType::NoIcon, this);
  window->addTextEditor("name", audioProcessor.getPresets().getCurrentName());
  window->addButton("Save", 1, juce::KeyPress(juce::KeyPress::returnKey));
  window->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));
  window->setLookAndFeel(&lookAndFeel);

  juce::Component::SafePointer<EvilAmplifierEditor> safeThis(this);
  window->enterModalState(
      true, juce::ModalCallbackFunction::create([safeThis, window](int result) {
        // Runs before the window is deleted (deleteWhenDismissed below).
        if (safeThis == nullptr || result != 1)
          return;
        juce::String error;
        if (!safeThis->audioProcessor.getPresets().saveUserPreset(
                window->getTextEditorContents("name"), error)) {
          juce::AlertWindow::showMessageBoxAsync(
              juce::MessageBoxIconType::WarningIcon, "Preset not saved", error);
          return;
        }
        safeThis->refreshPresetList();
      }),
      true);
}
} // namespace evil
