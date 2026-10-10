#pragma once

#include "PluginProcessor.h"
#include "gui/EvilLookAndFeel.h"
#include "gui/Widgets.h"

namespace evil {
class EvilAmplifierEditor final : public juce::AudioProcessorEditor,
                                  private juce::Timer,
                                  private juce::ChangeListener {
public:
  explicit EvilAmplifierEditor(EvilAmplifierProcessor &);
  ~EvilAmplifierEditor() override;

  void paint(juce::Graphics &) override;
  void resized() override;

  static constexpr int designWidth = 980, designHeight = 600;

private:
  /** Everything is laid out once in a fixed 980x600 design space on this
     canvas, which is then scaled uniformly to whatever size the host or window
     manager gives the editor. */
  struct Canvas final : juce::Component {
    explicit Canvas(EvilAmplifierEditor &e) : owner(e) {}
    void paint(juce::Graphics &g) override { owner.paintCanvas(g); }
    void resized() override { owner.layoutCanvas(); }
    EvilAmplifierEditor &owner;
  };

  void paintCanvas(juce::Graphics &);
  void layoutCanvas();

  void timerCallback() override;
  void changeListenerCallback(juce::ChangeBroadcaster *) override;

  void refreshModelStatus();
  void refreshStats();
  void refreshIrStatus();
  void refreshPresetList();
  void refreshAmpSections();
  void chooseModelFile();
  void chooseIrFile();
  void showBuiltinModels();
  void showBuiltinIrs();
  void savePresetDialog();
  void stepPreset(int delta);

  EvilAmplifierProcessor &audioProcessor;
  gui::EvilLookAndFeel lookAndFeel;
  Canvas canvas{*this};
  juce::TooltipWindow tooltips{this, 600};

  // Header
  juce::ComboBox presetBox;
  juce::TextButton prevPreset{"<"}, nextPreset{">"}, savePreset{"Save"};
  juce::Label cpuLabel, latencyLabel;

  // Input
  gui::Knob inputKnob, gateKnob;
  juce::ToggleButton gateButton{"Gate"};
  juce::AudioProcessorValueTreeState::ButtonAttachment gateAttachment;
  gui::LevelMeter inputMeter;

  // Amp
  juce::TextButton modeButtons[3]{juce::TextButton("Off"),
                                  juce::TextButton("Overdrive"),
                                  juce::TextButton("Neural")};
  std::unique_ptr<juce::ParameterAttachment> modeAttachment;
  juce::Label modelName, modelDetails;
  juce::TextButton loadModelButton{"Load .nam..."},
      builtinModelButton{"Built-in"}, unloadModelButton{"Unload"};
  juce::ToggleButton normaliseButton{"Normalise level"};
  juce::AudioProcessorValueTreeState::ButtonAttachment normaliseAttachment;
  gui::Knob driveKnob, toneKnob;
  juce::Label oversamplingLabel;
  juce::ComboBox oversamplingBox;
  juce::AudioProcessorValueTreeState::ComboBoxAttachment oversamplingAttachment;

  // Cab
  juce::Label irName, irDetails;
  juce::TextButton loadIrButton{"Load IR..."}, builtinIrButton{"Built-in"};
  juce::ToggleButton cabButton{"Cabinet on"};
  juce::AudioProcessorValueTreeState::ButtonAttachment cabAttachment;

  // EQ + output
  gui::Knob bassKnob, midKnob, trebleKnob, outputKnob;
  gui::LevelMeter outputMeter;

  juce::Rectangle<int> inputPanel, ampPanel, neuralArea, overdriveArea,
      cabPanel, eqPanel, outputPanel;
  std::unique_ptr<juce::FileChooser> chooser;
  juce::File lastModelDirectory, lastIrDirectory;
  int textRefreshCounter = 0;

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(EvilAmplifierEditor)
};
} // namespace evil
