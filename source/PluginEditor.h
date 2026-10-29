#pragma once

#include "PluginProcessor.h"
#include "gui/EvilLookAndFeel.h"
#include "gui/PedalBoard.h"
#include "gui/Settings.h"
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

  static constexpr int designWidth = 980, designHeight = 648;
  static constexpr int headerHeight = 60, footerHeight = 48;

private:
  /** Everything is laid out once in a fixed 980x648 design space on this
     canvas, which is then scaled uniformly to whatever size the host or window
     manager gives the editor. */
  struct Canvas final : juce::Component {
    explicit Canvas(EvilAmplifierEditor &e) : owner(e) {}
    void paint(juce::Graphics &g) override { owner.paintCanvas(g); }
    void resized() override { owner.layoutCanvas(); }
    EvilAmplifierEditor &owner;
  };

  void paintCanvas(juce::Graphics &);
  void paintFooter(juce::Graphics &);
  void layoutCanvas();

  void timerCallback() override;
  void changeListenerCallback(juce::ChangeBroadcaster *) override;

  void refreshModelStatus();
  void refreshStats();
  void refreshIrStatus();
  void refreshPresetList();
  void chooseModelFile();
  void chooseIrFile();
  void showBuiltinModels();
  void showBuiltinIrs();
  void showSettings();
  void applyAccent();
  void savePresetDialog();
  void stepPreset(int delta);
  void showPage(int page);
  void refreshPedalsTab();
  void refreshMetronome();
  void tapTempo();
  void stepTranspose(int delta);

  EvilAmplifierProcessor &audioProcessor;
  juce::SharedResourcePointer<gui::Settings> settings; // sets the accent first
  gui::EvilLookAndFeel lookAndFeel;
  Canvas canvas{*this};
  juce::TooltipWindow tooltips{this, 600};

  // Header
  juce::TextButton ampTab{"amp"}, pedalsTab{"pedals"};
  juce::ComboBox presetBox;
  juce::TextButton prevPreset{"<"}, nextPreset{">"}, savePreset{"save"};
  juce::TextButton settingsButton{"settings"};

  // Pages: the amp controls live on ampPage (laid out in canvas coordinates),
  // the pedals on the board.
  juce::Component ampPage;
  gui::PedalBoard pedalBoard;

  // Input
  gui::Knob inputKnob, gateKnob;
  juce::ToggleButton gateButton{"gate"};
  juce::AudioProcessorValueTreeState::ButtonAttachment gateAttachment;
  gui::LevelMeter inputMeter;

  // Amp
  juce::Label modelName, modelDetails;
  juce::TextButton loadModelButton{"load .nam..."},
      builtinModelButton{"built-in"}, unloadModelButton{"unload"};

  // Cab
  juce::Label irName, irDetails;
  juce::TextButton loadIrButton{"load ir..."}, builtinIrButton{"built-in"};
  juce::ToggleButton cabButton{"cabinet on"};
  juce::AudioProcessorValueTreeState::ButtonAttachment cabAttachment;

  // EQ + output. The analyser spans the fader columns, an octave each, so
  // every band's fader sits over its centre frequency.
  gui::Spectrum eqSpectrum{
      Equaliser::frequencies.front() / juce::MathConstants<double>::sqrt2,
      Equaliser::frequencies.back() * juce::MathConstants<double>::sqrt2};
  std::array<std::unique_ptr<gui::Fader>, Equaliser::numBands> eqFaders;
  gui::Knob outputKnob;
  gui::LevelMeter outputMeter;

  // Footer: metronome, transposer, stats
  juce::TextButton metronomeButton{"start"}, tapButton{"tap"};
  juce::Slider bpmSlider{juce::Slider::LinearBar, juce::Slider::TextBoxLeft},
      metronomeLevelSlider{juce::Slider::LinearBar, juce::Slider::TextBoxLeft};
  juce::AudioProcessorValueTreeState::SliderAttachment bpmAttachment,
      metronomeLevelAttachment;
  juce::ComboBox beatsBox;
  juce::AudioProcessorValueTreeState::ComboBoxAttachment beatsAttachment;
  juce::TextButton transposeDown{"-"}, transposeUp{"+"};
  juce::Label transposeLabel;
  std::unique_ptr<juce::ParameterAttachment> transposeAttachment;
  juce::Label cpuLabel, latencyLabel;

  juce::Rectangle<int> inputPanel, ampPanel, cabPanel, eqPanel, outputPanel,
      metronomeCaption, beatLeds, transposeCaption;
  juce::Rectangle<float> gateLed;
  std::vector<double> tapTimes;
  std::uint32_t lastClickCount = 0;
  double lastClickTime = 0.0;
  std::unique_ptr<juce::FileChooser> chooser;
  juce::File lastModelDirectory, lastIrDirectory;
  int textRefreshCounter = 0;

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(EvilAmplifierEditor)
};
} // namespace evil
