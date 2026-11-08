#include "PluginEditor.h"

#include "StandaloneApp.h"

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
      agentPage(p.getAgent(), p.getAgentSettings()),
      inputKnob(p.getState(), params::input, "Gain"),
      gateKnob(p.getState(), params::gateThreshold, "Gate"),
      gateAttachment(p.getState(), params::gateOn, gateButton),
      cabAttachment(p.getState(), params::cabOn, cabButton),
      outputKnob(p.getState(), params::output, "Level"),
      bpmAttachment(p.getState(), params::metronomeBpm, bpmSlider),
      metronomeLevelAttachment(p.getState(), params::metronomeLevel,
                               metronomeLevelSlider),
      beatsAttachment(p.getState(), params::metronomeBeats,
                      withItems(beatsBox, {"2/4", "3/4", "4/4", "5/4", "6/4",
                                           "7/4"})) {
  // Header
  for (auto [page, tab] : {std::pair{0, &ampTab}, std::pair{1, &pedalsTab},
                           std::pair{2, &tunerTab}, std::pair{3, &agentTab}}) {
    gui::setTab(*tab);
    tab->onClick = [this, page = page] { showPage(page); };
    canvas.addAndMakeVisible(tab);
  }
  ampTab.setTooltip("Amp, cabinet and EQ");
  pedalsTab.setTooltip("Stomp boxes before and after the amp");
  tunerTab.setTooltip("Tune your guitar, to standard or a tuning of your own");
  agentTab.setTooltip("Ask for a tone, and an AI on your own API key sets it up");
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
  savePreset.setTooltip("Save the current settings, capture and IR as a user preset");
  gui::setPrimary(savePreset);
  refreshPresetList();
  canvas.addAndMakeVisible(settingsButton);
  settingsButton.onClick = [this] { showSettings(); };
  settingsButton.setTooltip(isStandalone()
                                ? "Audio device, saved states and accent colour"
                                : "Accent colour, for every instance");
  if (isStandalone()) {
    canvas.addAndMakeVisible(minimiseButton);
    canvas.addAndMakeVisible(closeButton);
    minimiseButton.onClick = [] { standalone::minimiseWindow(); };
    closeButton.onClick = [] { standalone::closeWindow(); };
    minimiseButton.setTooltip("Minimise");
    closeButton.setTooltip("Quit");
  }

  // Input
  for (auto *c : std::initializer_list<juce::Component *>{
           &inputKnob, &gateKnob, &gateButton, &inputMeter})
    ampPage.addAndMakeVisible(c);
  gateButton.setTooltip("Noise gate before the amp");

  // Amp
  styleLabel(modelName, gui::display(36.0f), gui::colours::text);
  styleLabel(modelDetails, gui::sans(14.0f), gui::colours::textSoft);
  modelDetails.setJustificationType(juce::Justification::topLeft);
  for (auto *c : std::initializer_list<juce::Component *>{
           &tubes, &modelName, &modelDetails, &loadModelButton,
           &builtinModelButton, &unloadModelButton})
    ampPage.addAndMakeVisible(c);
  loadModelButton.onClick = [this] { chooseModelFile(); };
  builtinModelButton.onClick = [this] { showBuiltinModels(); };
  unloadModelButton.onClick = [this] { audioProcessor.unloadModel(); };
  loadModelButton.setTooltip("Load a Neural Amp Modeler .nam capture");
  unloadModelButton.setTooltip("Play without a model: the amp passes clean");

  // Cab
  styleLabel(irName, gui::display(24.0f), gui::colours::text);
  styleLabel(irDetails, gui::sans(13.0f), gui::colours::textSoft);
  irDetails.setJustificationType(juce::Justification::topLeft);
  for (auto *c : std::initializer_list<juce::Component *>{
           &speaker, &irName, &irDetails, &loadIrButton, &builtinIrButton,
           &cabButton})
    ampPage.addAndMakeVisible(c);
  loadIrButton.onClick = [this] { chooseIrFile(); };
  builtinIrButton.onClick = [this] { showBuiltinIrs(); };
  loadIrButton.setTooltip(
      "Load a cabinet impulse response (WAV, AIFF or FLAC)");

  // EQ + output
  ampPage.addAndMakeVisible(eqSpectrum); // first, so it sits behind the faders
  for (size_t b = 0; b < eqFaders.size(); ++b) {
    eqFaders[b] = std::make_unique<gui::Fader>(p.getState(), params::eqBands[b],
                                               params::eqBandLabel(b));
    ampPage.addAndMakeVisible(*eqFaders[b]);
  }
  for (auto *c :
       std::initializer_list<juce::Component *>{&outputKnob, &outputMeter})
    ampPage.addAndMakeVisible(c);
  outputKnob.setMaxDiameter(128);

  ampPage.setInterceptsMouseClicks(false, true);
  canvas.addAndMakeVisible(ampPage);
  canvas.addChildComponent(pedalBoard);
  canvas.addChildComponent(tunerPage);
  canvas.addChildComponent(agentPage);

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
    gui::ignoreTextWithoutNumber(*slider);
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
  styleLabel(transposeLabel, gui::readout(14.0f), gui::colours::textSoft);
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
                                         : gui::colours::accent());
      });
  transposeAttachment->sendInitialUpdate();

  // Footer: stats
  for (auto *l : {&cpuLabel, &latencyLabel}) {
    styleLabel(*l, gui::readout(12.0f), gui::colours::textSoft);
    l->setJustificationType(juce::Justification::centredRight);
    canvas.addAndMakeVisible(l);
  }
  cpuLabel.setTooltip("Processing time per block as a share of the block's "
                      "real-time deadline (average / peak)");
  latencyLabel.setTooltip("Latency reported to the host");

  audioProcessor.getModels().addChangeListener(this);
  settings->addChangeListener(this);
  refreshModelStatus();
  refreshAmpHardware(0.0f);
  tubes.snapTo(modelLive, 0.0f);
  refreshIrStatus();
  refreshStats();
  refreshMetronome();
  applyPresetColour();
  showPage(audioProcessor.editorPage);

  addAndMakeVisible(canvas);
  // After every child exists: sliders build their value boxes from the
  // look-and-feel they see when it changes.
  setLookAndFeel(&lookAndFeel);
  setResizable(true, true);
  setResizeLimits(designWidth * 3 / 5, designHeight * 3 / 5, designWidth * 2,
                  designHeight * 2);
  // A locked aspect ratio makes drag-resizing in a host feel right, but the
  // standalone's window manager may impose any size (tiling, maximise, snap);
  // the constrainer would then shrink the editor inside the bigger window and
  // leave it unpainted. resized() letterboxes, so any size works there.
  if (!isStandalone())
    getConstrainer()->setFixedAspectRatio((double)designWidth / designHeight);
  setSize(designWidth, designHeight);
  startTimerHz(30);
}

EvilAmplifierEditor::~EvilAmplifierEditor() {
  stopTimer();
  audioProcessor.getModels().removeChangeListener(this);
  settings->removeChangeListener(this);
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
  fixedParts.draw(g, canvas.getLocalBounds(), ampPage.isVisible() ? 1 : 0,
                  [this](juce::Graphics &c) { paintFixedParts(c); });
  paintBeatLights(g);
  if (!ampPage.isVisible())
    return;

  // The gate's activity light, and the amp's pilot light.
  const bool gateEnabled =
      audioProcessor.getState().getRawParameterValue(params::gateOn)->load() >= 0.5f;
  gui::drawLed(g, gateLed, gateEnabled && audioProcessor.isGateOpen(),
               gui::colours::accent());
  gui::drawLed(g, liveLed, modelLive, gui::colours::accent());
  g.setFont(gui::caption());
  g.setColour(modelLive ? gui::colours::text : gui::colours::muted);
  g.drawText(modelLive ? "LIVE" : "STANDBY",
             juce::Rectangle<float>(liveLed.getX() - 69.0f, liveLed.getY() - 4.0f, 60.0f,
                                    liveLed.getHeight() + 8.0f),
             juce::Justification::centredRight, false);
}

void EvilAmplifierEditor::paintFixedParts(juce::Graphics &g) {
  using namespace gui::colours;
  const juce::Rectangle<int> design(designWidth, designHeight);
  gui::drawChassis(g, design);

  // Vinyl along the top, and a shadow from it across the rack.
  gui::drawVinyl(g, design.withHeight(headerHeight));
  g.setGradientFill(juce::ColourGradient(juce::Colours::black.withAlpha(0.75f), 0.0f,
                                         (float)headerHeight, juce::Colours::black.withAlpha(0.0f),
                                         0.0f, (float)headerHeight + 14.0f, false));
  g.fillRect(0, headerHeight, designWidth, 14);
  g.setColour(juce::Colours::white.withAlpha(0.13f));
  g.fillRect(0, headerHeight - 1, designWidth, 1);
  gui::drawLogo(g, {18.0f, 7.0f}, 50.0f);
  gui::drawRecess(g, tabTray.toFloat(), 4.0f);

  paintFooter(g);
  if (!ampPage.isVisible())
    return;

  gui::drawPanel(g, inputPanel, "Input", 22.0f);
  gui::drawPanel(g, ampPanel, "Amp", 86.0f);
  gui::drawPanel(g, cabPanel, "Cabinet");
  gui::drawPanel(g, eqPanel, "EQ");
  gui::drawPanel(g, outputPanel, "Output");

  // The amp's display: smoked glass with the tubes behind it.
  const auto display = ampDisplay.toFloat();
  gui::drawRecess(g, display, 4.0f);
  {
    const juce::Graphics::ScopedSaveState save(g);
    g.reduceClipRegion(ampDisplay.reduced(1));
    // The glass picks up a reflection across its top corner.
    juce::Path reflection;
    reflection.startNewSubPath(display.getX(), display.getY());
    reflection.lineTo(display.getX() + display.getWidth() * 0.62f, display.getY());
    reflection.lineTo(display.getX() + display.getWidth() * 0.38f, display.getBottom());
    reflection.lineTo(display.getX(), display.getBottom());
    reflection.closeSubPath();
    g.setGradientFill(juce::ColourGradient(juce::Colours::white.withAlpha(0.045f),
                                           display.getX(), display.getY(),
                                           juce::Colours::white.withAlpha(0.0f),
                                           display.getX() + display.getWidth() * 0.5f,
                                           display.getBottom(), false));
    g.fillPath(reflection);
  }
  g.setFont(gui::silk(12.0f, 0.15f));
  g.setColour(accent());
  g.drawText("NEURAL AMP MODEL", ampCaption, juce::Justification::centredLeft, false);
}

void EvilAmplifierEditor::layoutCanvas() {
  const juce::Rectangle<int> design(designWidth, designHeight);

  // Header: logo, tabs in their tray, presets.
  auto header = design.withHeight(headerHeight).reduced(16, 0);
  header.removeFromLeft(100);
  header.removeFromLeft(16);
  tabTray = header.removeFromLeft(298).withSizeKeepingCentre(298, 38);
  {
    auto tabs = tabTray.reduced(3);
    ampTab.setBounds(tabs.removeFromLeft(58));
    tabs.removeFromLeft(1);
    pedalsTab.setBounds(tabs.removeFromLeft(98));
    tabs.removeFromLeft(1);
    tunerTab.setBounds(tabs.removeFromLeft(67));
    tabs.removeFromLeft(1);
    agentTab.setBounds(tabs);
  }
  header.removeFromLeft(16);
  header = header.withSizeKeepingCentre(header.getWidth(), 32);
  prevPreset.setBounds(header.removeFromLeft(28));
  header.removeFromLeft(4);
  presetBox.setBounds(header.removeFromLeft(isStandalone() ? 166 : 214));
  header.removeFromLeft(4);
  nextPreset.setBounds(header.removeFromLeft(28));
  header.removeFromLeft(10);
  savePreset.setBounds(header.removeFromLeft(58));
  if (isStandalone()) {
    closeButton.setBounds(header.removeFromRight(24));
    header.removeFromRight(2);
    minimiseButton.setBounds(header.removeFromRight(24));
    header.removeFromRight(8);
  }
  settingsButton.setBounds(header.removeFromRight(78));

  // Footer: metronome on the left; transposer and stats on the right.
  {
    auto footer = design.withTop(designHeight - footerHeight).reduced(16, 9);
    footer.removeFromRight(14); // clear of the window's resize corner
    const auto next = [&footer](int width, int gapAfter) {
      const auto r = footer.removeFromLeft(width);
      footer.removeFromLeft(gapAfter);
      return r;
    };
    metronomeCaption = next(70, 8);
    metronomeButton.setBounds(next(54, 6));
    bpmSlider.setBounds(next(84, 6));
    tapButton.setBounds(next(44, 6));
    beatsBox.setBounds(next(62, 6));
    metronomeLevelSlider.setBounds(next(74, 12));
    beatLeds = next(7 * 12, 0);

    statsArea = footer.removeFromRight(176).withSizeKeepingCentre(176, 30);
    auto stats = statsArea;
    cpuLabel.setBounds(stats.removeFromTop(15));
    latencyLabel.setBounds(stats);
    footer.removeFromRight(20);
    transposeUp.setBounds(footer.removeFromRight(28));
    footer.removeFromRight(4);
    transposeLabel.setBounds(footer.removeFromRight(52));
    footer.removeFromRight(4);
    transposeDown.setBounds(footer.removeFromRight(28));
    footer.removeFromRight(8);
    transposeCaption = footer.removeFromRight(66);
  }

  ampPage.setBounds(design);
  auto body = design.withTrimmedTop(headerHeight)
                  .withTrimmedBottom(footerHeight)
                  .reduced(14, 12);
  pedalBoard.setBounds(body);
  tunerPage.setBounds(body);
  agentPage.setBounds(body);
  auto top = body.removeFromTop(268);
  body.removeFromTop(10);
  auto bottom = body;

  inputPanel = top.removeFromLeft(232);
  top.removeFromLeft(10);
  outputPanel = top.removeFromRight(186);
  top.removeFromRight(10);
  ampPanel = top;

  cabPanel = bottom.removeFromLeft(340);
  bottom.removeFromLeft(10);
  eqPanel = bottom;

  const auto titleLight = [](juce::Rectangle<int> panel, float size) {
    return juce::Rectangle<float>(size, size).withCentre(
        {(float)panel.getRight() - 30.0f, (float)panel.getY() + 17.5f});
  };

  // Input: two knobs side by side, gate switch, meter on the right.
  {
    gateLed = titleLight(inputPanel, 8.0f);
    auto b = inputPanel.reduced(14, 0).withTrimmedTop(34).withTrimmedBottom(18);
    inputMeter.setBounds(b.removeFromRight(12));
    b.removeFromRight(6);
    gateButton.setBounds(b.removeFromBottom(24).withTrimmedLeft(10));
    auto knobs = b.withTrimmedBottom(6);
    inputKnob.setBounds(knobs.removeFromLeft(knobs.getWidth() / 2));
    gateKnob.setBounds(knobs);
  }

  // Amp: the display, with the tubes and what's loaded, and what to load.
  {
    liveLed = titleLight(ampPanel, 9.0f);
    auto b = ampPanel.reduced(16, 0).withTrimmedTop(34).withTrimmedBottom(16);
    auto buttons = b.removeFromBottom(30);
    b.removeFromBottom(10);
    ampDisplay = b;
    auto d = ampDisplay.reduced(14, 10);
    tubes.setBounds(d.removeFromLeft(156));
    d.removeFromLeft(18);
    ampCaption = d.removeFromTop(18);
    modelName.setBounds(d.removeFromTop(44));
    d.removeFromTop(2);
    modelDetails.setBounds(d);

    loadModelButton.setBounds(buttons.removeFromLeft(112));
    buttons.removeFromLeft(6);
    builtinModelButton.setBounds(buttons.removeFromLeft(84));
    buttons.removeFromLeft(6);
    unloadModelButton.setBounds(buttons.removeFromLeft(74));
  }

  // Cab: the speaker, what's loaded, and how to change it.
  {
    auto b = cabPanel.reduced(14, 0).withTrimmedTop(34).withTrimmedBottom(18);
    constexpr int cone = 146;
    speaker.setBounds(b.removeFromLeft(cone).withSizeKeepingCentre(cone, cone));
    b.removeFromLeft(14);
    irName.setBounds(b.removeFromTop(30));
    irDetails.setBounds(b.removeFromTop(52));
    auto switchRow = b.removeFromBottom(26);
    cabButton.setBounds(switchRow);
    b.removeFromBottom(10);
    auto buttons = b.removeFromBottom(30);
    const int half = (buttons.getWidth() - 6) / 2;
    loadIrButton.setBounds(buttons.removeFromLeft(half));
    builtinIrButton.setBounds(buttons.removeFromRight(half));
  }

  // EQ
  {
    auto b = eqPanel.reduced(14, 0).withTrimmedTop(32).withTrimmedBottom(14);
    const auto edge = [&b, this](size_t i) {
      return b.getX() + (int)(i * (size_t)b.getWidth() / eqFaders.size());
    };
    for (size_t i = 0; i < eqFaders.size(); ++i)
      eqFaders[i]->setBounds(b.withLeft(edge(i)).withRight(edge(i + 1)));
    // The fader tracks' height, between their value and title labels.
    eqSpectrum.setBounds(b.withTrimmedTop(18).withTrimmedBottom(18));
  }

  // Output
  {
    auto b = outputPanel.reduced(14, 0).withTrimmedTop(34).withTrimmedBottom(18);
    outputMeter.setBounds(b.removeFromRight(12));
    b.removeFromRight(4);
    outputKnob.setBounds(b);
  }
}

void EvilAmplifierEditor::canvasMouseDown(const juce::MouseEvent &e) {
  // Bare header in the standalone app drags the window, as a title bar would.
  draggingWindow = isStandalone() && e.y < headerHeight;
  if (draggingWindow)
    windowDragger.startDraggingComponent(getTopLevelComponent(), e);
}

void EvilAmplifierEditor::canvasMouseDrag(const juce::MouseEvent &e) {
  if (draggingWindow)
    windowDragger.dragComponent(getTopLevelComponent(), e, nullptr);
}

void EvilAmplifierEditor::paintFooter(juce::Graphics &g) {
  using namespace gui::colours;
  const auto strip = juce::Rectangle<int>(designWidth, designHeight)
                         .withTop(designHeight - footerHeight);
  gui::drawVinyl(g, strip);
  g.setGradientFill(juce::ColourGradient(juce::Colours::black.withAlpha(0.0f), 0.0f,
                                         (float)strip.getY() - 10.0f,
                                         juce::Colours::black.withAlpha(0.6f), 0.0f,
                                         (float)strip.getY(), false));
  g.fillRect(0, strip.getY() - 10, designWidth, 10);
  g.setColour(juce::Colours::white.withAlpha(0.13f));
  g.fillRect(0, strip.getY(), designWidth, 1);

  gui::drawRecess(g, transposeLabel.getBounds().toFloat(), 3.0f);
  gui::drawRecess(g, statsArea.toFloat().expanded(6.0f, 0.0f), 3.0f);

  g.setFont(gui::caption());
  g.setColour(textSoft);
  g.drawText("METRONOME", metronomeCaption, juce::Justification::centredLeft);
  g.drawText("TRANSPOSE", transposeCaption, juce::Justification::centredRight);
  g.setColour(muted);
  g.drawText("CPU", cpuLabel.getBounds(), juce::Justification::centredLeft);
  g.drawText("LATENCY", latencyLabel.getBounds(),
             juce::Justification::centredLeft);
}

void EvilAmplifierEditor::paintBeatLights(juce::Graphics &g) {
  using namespace gui::colours;
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
        {(float)beatLeds.getX() + 5.5f + 12.0f * (float)i,
         (float)beatLeds.getCentreY()});
    if (i == current && flashing)
      gui::drawLed(g, led, true, i == 0 ? accent() : text);
    else
      gui::drawLed(g, led, false, i == current ? text : accent());
  }
}

void EvilAmplifierEditor::timerCallback() {
  constexpr float interval = 1.0f / 30.0f;
  inputMeter.pushPeak(audioProcessor.takeInputPeak(), interval);
  const float outputPeak = audioProcessor.takeOutputPeak();
  outputMeter.pushPeak(outputPeak, interval);

  std::array<float, 4096> samples;
  auto &tap = audioProcessor.getAnalyserTap();
  for (int n; (n = tap.pull(samples.data(), (int)samples.size())) > 0;)
    eqSpectrum.push(samples.data(), n);
  if (ampPage.isVisible()) {
    eqSpectrum.refresh(audioProcessor.getSampleRate(), interval);
    refreshAmpHardware(outputPeak);
  }

  // Drained on every page, so the tuner never starts on stale samples.
  auto &tunerTap = audioProcessor.getTunerTap();
  for (int n; (n = tunerTap.pull(samples.data(), (int)samples.size())) > 0;)
    if (tunerPage.isVisible())
      tunerPage.push(samples.data(), n);
  if (tunerPage.isVisible())
    tunerPage.refresh(audioProcessor.getSampleRate(), interval);

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
    if (agentPage.isVisible())
      agentPage.refresh();
    if (presetBox.getSelectedItemIndex() !=
        audioProcessor.getPresets().getCurrentIndex())
      presetBox.setSelectedItemIndex(
          audioProcessor.getPresets().getCurrentIndex(),
          juce::dontSendNotification);
  }
  if (ampPage.isVisible())
    canvas.repaint(gateLed.expanded(16.0f).getSmallestIntegerContainer());
}

void EvilAmplifierEditor::refreshAmpHardware(float outputPeak) {
  // The tubes follow the signal over the top of a guitar's range.
  const auto db = juce::Decibels::gainToDecibels(outputPeak, -100.0f);
  tubes.update(modelLive, juce::jmap(juce::jlimit(-42.0f, -3.0f, db), -42.0f, -3.0f, 0.0f, 1.0f));
  speaker.setActive(audioProcessor.getState().getRawParameterValue(params::cabOn)->load() >= 0.5f);

  std::array<float, Equaliser::numBands> bands;
  for (size_t b = 0; b < bands.size(); ++b)
    bands[b] = audioProcessor.getState().getParameter(params::eqBands[b])->getValue();
  eqSpectrum.setBands(bands, (double)Equaliser::maxGainDb);
}

void EvilAmplifierEditor::showPage(int page) {
  page = juce::jlimit(0, 3, page);
  audioProcessor.editorPage = page;
  ampPage.setVisible(page == 0);
  pedalBoard.setVisible(page == 1);
  tunerPage.setVisible(page == 2);
  agentPage.setVisible(page == 3);
  tunerPage.reset();
  ampTab.setToggleState(page == 0, juce::dontSendNotification);
  pedalsTab.setToggleState(page == 1, juce::dontSendNotification);
  tunerTab.setToggleState(page == 2, juce::dontSendNotification);
  agentTab.setToggleState(page == 3, juce::dontSendNotification);
  refreshPedalsTab();
  if (page == 3)
    agentPage.pageShown();
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

void EvilAmplifierEditor::changeListenerCallback(
    juce::ChangeBroadcaster *source) {
  if (source == &settings.get())
    applyAccent();
  else
    refreshModelStatus();
}

void EvilAmplifierEditor::applyAccent() {
  lookAndFeel.refreshAccent();
  sendLookAndFeelChange(); // slider value boxes take the new edit tint
  applyPresetColour();
  // Labels coloured by state.
  transposeAttachment->sendInitialUpdate();
  refreshModelStatus();
  refreshIrStatus();
  repaint();
}

void EvilAmplifierEditor::applyPresetColour() {
  presetBox.setColour(juce::ComboBox::textColourId, gui::colours::accent());
}

void EvilAmplifierEditor::refreshModelStatus() {
  using State = ModelManager::State;
  const auto status = audioProcessor.getModels().getStatus();
  if (const bool live = status.state == State::ready; live != modelLive) {
    modelLive = live;
    canvas.repaint(ampPanel.withHeight(34));
  }
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
    if (const auto credit = agent::Tone3000::creditForSource(i.source);
        credit.isNotEmpty())
      details = credit + "\n" + details;
    else if (i.gear.isNotEmpty())
      details = i.gear + "\n" + details;
    modelDetails.setText(details, juce::dontSendNotification);
    break;
  }
  }
  modelDetails.setColour(juce::Label::textColourId,
                         status.state == State::error ? gui::colours::accent()
                                                      : gui::colours::textSoft);
}

void EvilAmplifierEditor::refreshIrStatus() {
  irName.setText(audioProcessor.getCab().getDisplayName(),
                 juce::dontSendNotification);
  const auto error = audioProcessor.getLastIrError();
  if (const auto source = audioProcessor.getCab().getSource();
      source != creditedIrSource) {
    creditedIrSource = source;
    irCredit = agent::Tone3000::creditForSource(source);
  }
  irDetails.setText(
      error.isNotEmpty()      ? error
      : irCredit.isNotEmpty() ? irCredit
                              : juce::String("Zero-latency convolution; IR swaps crossfade"),
      juce::dontSendNotification);
  irDetails.setColour(juce::Label::textColourId, error.isNotEmpty()
                                                     ? gui::colours::accent()
                                                     : gui::colours::textSoft);
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

void EvilAmplifierEditor::showSettings() {
  std::optional<gui::SettingsPanel::AppActions> app;
  if (isStandalone())
    app = gui::SettingsPanel::AppActions{
        standalone::showAudioSettings, standalone::saveState,
        standalone::loadState, standalone::resetToDefaultState};
  juce::CallOutBox::launchAsynchronously(
      std::make_unique<gui::SettingsPanel>(std::move(app)),
      settingsButton.getBounds(), &canvas);
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
      "Name your preset. It is saved, with its capture and IR, to\n" +
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
        juce::String error, warning;
        if (!safeThis->audioProcessor.getPresets().saveUserPreset(
                window->getTextEditorContents("name"), error, &warning)) {
          juce::AlertWindow::showMessageBoxAsync(
              juce::MessageBoxIconType::WarningIcon, "Preset not saved", error);
          return;
        }
        if (warning.isNotEmpty())
          juce::AlertWindow::showMessageBoxAsync(
              juce::MessageBoxIconType::WarningIcon, "Preset saved", warning);
        safeThis->refreshPresetList();
      }),
      true);
}
} // namespace evil
