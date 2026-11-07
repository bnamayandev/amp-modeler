#pragma once

#include "Hardware.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <map>

namespace evil::gui {
namespace colours {
// Blackened steel and silkscreen ink: neutral blacks, so the accent is the only
// thing on the panel with any colour in it.
inline const juce::Colour chassis{0xff09090b};    // the rack behind the modules
inline const juce::Colour background{0xff131316}; // behind any letterboxing
inline const juce::Colour panel{0xff1d1d21};      // a module's face
inline const juce::Colour glass{0xff07080a};      // displays and recessed fields
inline const juce::Colour text{0xfff1ede4};       // silkscreen ink
inline const juce::Colour textSoft{0xffc6c2b8};   // values, secondary text
inline const juce::Colour muted{0xff96938b};      // labels, meta
inline const juce::Colour mutedSoft{0xff66645f};  // faintest text, idle marks
inline const juce::Colour line = juce::Colours::white.withAlpha(0.13f);     // dividers
inline const juce::Colour lineSoft = juce::Colours::white.withAlpha(0.06f); // faint dividers
inline const juce::Colour recordLight{0xffe8352a}; // red on any gear, whatever the accent

// The one vivid hue, chosen in settings: what the lights, tubes and values glow
// in. Every accent tint is mixed down from these two rather than defined
// separately.
constexpr float defaultAccentHue = 10.0f / 360.0f;
juce::Colour accent();
juce::Colour accentHover();
/** The accent as it would look at another hue (0..1), e.g. for swatches. */
juce::Colour accentAt(float hue);
float getAccentHue();
/** Message thread only. Open editors repaint through gui::Settings, which is
    what normally calls this. */
void setAccentHue(float hue);
} // namespace colours

/** The embedded faces, shared by all open editors. */
struct Typefaces {
  Typefaces();
  juce::Typeface::Ptr sans, sansMedium, silk, display, readout, logo;
};

/** Barlow: prose, anything read as a sentence. Sizes are in points (CSS px);
    tracking is JUCE's kerning factor, a fraction of the line height, so it runs
    a little wider than the same number in CSS ems. */
juce::Font sans(float size, bool medium = false, float tracking = 0.0f);
/** Barlow Semi Condensed SemiBold: what's printed on the hardware. Knob titles,
    section titles, buttons, tabs. Always upper case. */
juce::Font silk(float size, float tracking = 0.06f);
/** Barlow Condensed ExtraBold Italic: the names of things. Amp, cab, pedals. */
juce::Font display(float size, float tracking = 0.0f);
/** Share Tech Mono: every number and value, so they hold still as they change. */
juce::Font readout(float size, float tracking = 0.0f);
/** New Rocker, for the logo alone. */
juce::Font logoFont(float size);
/** Small tracked silkscreen label, e.g. knob titles and section captions. */
inline juce::Font caption() { return silk(11.0f, 0.09f); }

/** Draws a TextButton as the primary action, picked out in the accent. */
void setPrimary(juce::Button &);
/** Draws a TextButton as a navigation tab: a key in a tray, lit while selected
   (its toggle state). */
void setTab(juce::Button &);
/** Draws a TextButton's text as written rather than as a panel label, for
    buttons holding a sentence. */
void setProse(juce::Button &);
/** Marks a slider as one of the small knobs on a pedal: smaller value text. */
void setCompact(juce::Slider &);

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
  std::map<int, PaintCache> knobBodies;             // by size in 1/10 px
  PaintCache faderCap;
};
} // namespace evil::gui
