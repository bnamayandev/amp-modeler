#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace evil::gui {
namespace colours {
// Warm-neutral ramp: never pure black, pure white or cool grey.
inline const juce::Colour background{0xff2b2a27}; // warm brown-black "paper"
inline const juce::Colour panel{0xff211f1d};      // a touch darker than paper
inline const juce::Colour text{0xffece6d8};       // warm off-white
inline const juce::Colour textSoft{0xffb9b2a3};   // values, secondary text
inline const juce::Colour muted{0xff8f897b};      // labels, meta
inline const juce::Colour mutedSoft{0xff6f6a5e};  // faintest text, idle marks
inline const juce::Colour line = text.withAlpha(0.15f);     // dividers
inline const juce::Colour lineSoft = text.withAlpha(0.08f); // faint dividers

// The one vivid hue, chosen in settings. Every accent tint (pills, glows,
// highlights) is mixed down from these two rather than defined separately.
constexpr float defaultAccentHue = 350.0f / 360.0f;
juce::Colour accent();
juce::Colour accentHover();
/** The accent as it would look at another hue (0..1), e.g. for swatches. */
juce::Colour accentAt(float hue);
float getAccentHue();
/** Message thread only. Open editors repaint through gui::Settings, which is
    what normally calls this. */
void setAccentHue(float hue);
} // namespace colours

/** The embedded Inter and JetBrains Mono faces, shared by all open editors. */
struct Typefaces {
  Typefaces();
  juce::Typeface::Ptr sans, sansMedium, mono, monoMedium;
};

/** Inter: the product name, model/IR names and prose. Sizes are in points
    (CSS px); tracking is JUCE's kerning factor, a fraction of the line height,
    so it runs a little wider than the same number in CSS ems. */
juce::Font sans(float size, bool medium = false, float tracking = 0.0f);
/** JetBrains Mono: all structural chrome (labels, values, buttons, menus). */
juce::Font mono(float size, bool medium = false, float tracking = 0.0f);
/** Small tracked mono label, e.g. knob titles and section captions. */
inline juce::Font caption() { return mono(10.5f, true, 0.08f); }

/** Draws a TextButton as the accent-tinted primary action. */
void setPrimary(juce::Button &);
/** Draws a TextButton as a navigation tab: bare text, underlined in the accent
   while selected (its toggle state). */
void setTab(juce::Button &);

class EvilLookAndFeel final : public juce::LookAndFeel_V4 {
public:
  EvilLookAndFeel();

  /** Re-applies the colour ids that hold the accent; call after it changes. */
  void refreshAccent();

  void drawRotarySlider(juce::Graphics &, int x, int y, int width, int height,
                        float sliderPos, float startAngle, float endAngle,
                        juce::Slider &) override;
  void drawLinearSlider(juce::Graphics &, int x, int y, int width, int height,
                        float sliderPos, float minSliderPos, float maxSliderPos,
                        juce::Slider::SliderStyle, juce::Slider &) override;
  void drawToggleButton(juce::Graphics &, juce::ToggleButton &,
                        bool highlighted, bool down) override;
  void drawButtonBackground(juce::Graphics &, juce::Button &,
                            const juce::Colour &, bool highlighted,
                            bool down) override;
  void drawButtonText(juce::Graphics &, juce::TextButton &, bool highlighted,
                      bool down) override;
  void drawComboBox(juce::Graphics &, int width, int height, bool isButtonDown,
                    int buttonX, int buttonY, int buttonW, int buttonH,
                    juce::ComboBox &) override;
  void drawPopupMenuBackground(juce::Graphics &, int width,
                               int height) override;
  void drawCallOutBoxBackground(juce::CallOutBox &, juce::Graphics &,
                                const juce::Path &, juce::Image &) override;
  int getCallOutBoxBorderSize(const juce::CallOutBox &) override { return 14; }
  void drawCornerResizer(juce::Graphics &, int w, int h, bool isMouseOver,
                         bool isMouseDragging) override;
  void drawTooltip(juce::Graphics &, const juce::String &text, int width,
                   int height) override;
  juce::Rectangle<int> getTooltipBounds(const juce::String &text,
                                        juce::Point<int> screenPos,
                                        juce::Rectangle<int> parentArea) override;
  juce::Font getTextButtonFont(juce::TextButton &, int buttonHeight) override;
  juce::Font getComboBoxFont(juce::ComboBox &) override;
  juce::Font getPopupMenuFont() override;
  juce::Font getLabelFont(juce::Label &) override;
  juce::Font getAlertWindowTitleFont() override;
  juce::Font getAlertWindowMessageFont() override;
  juce::Font getAlertWindowFont() override;
  juce::Label *createSliderTextBox(juce::Slider &) override;

private:
  juce::SharedResourcePointer<Typefaces> typefaces; // keeps them loaded
};
} // namespace evil::gui
