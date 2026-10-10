#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace evil::gui {
namespace colours {
inline const juce::Colour background{0xff111214};
inline const juce::Colour panel{0xff1b1c20};
inline const juce::Colour panelEdge{0xff2c2e34};
inline const juce::Colour control{0xff26282e};
inline const juce::Colour controlEdge{0xff3a3d44};
inline const juce::Colour text{0xffe9e6e1};
inline const juce::Colour textDim{0xff8d9097};
inline const juce::Colour accent{0xffe63946};
inline const juce::Colour good{0xff59c47a};
inline const juce::Colour warn{0xffe8b04a};
} // namespace colours

juce::Font uiFont(float height, bool bold = false);

class EvilLookAndFeel final : public juce::LookAndFeel_V4 {
public:
  EvilLookAndFeel();

  void drawRotarySlider(juce::Graphics &, int x, int y, int width, int height,
                        float sliderPos, float startAngle, float endAngle,
                        juce::Slider &) override;
  void drawToggleButton(juce::Graphics &, juce::ToggleButton &,
                        bool highlighted, bool down) override;
  void drawButtonBackground(juce::Graphics &, juce::Button &,
                            const juce::Colour &, bool highlighted,
                            bool down) override;
  void drawComboBox(juce::Graphics &, int width, int height, bool isButtonDown,
                    int buttonX, int buttonY, int buttonW, int buttonH,
                    juce::ComboBox &) override;
  juce::Font getTextButtonFont(juce::TextButton &, int buttonHeight) override;
  juce::Font getComboBoxFont(juce::ComboBox &) override;
  juce::Font getPopupMenuFont() override;
  juce::Font getLabelFont(juce::Label &) override;
  juce::Label *createSliderTextBox(juce::Slider &) override;
};
} // namespace evil::gui
