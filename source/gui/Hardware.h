#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>

namespace evil::gui {
/** Keeps a static paint job in an image at the pixel scale it's drawn at, so
    grain, shadows and machined parts cost one paint rather than one per frame.
    It paints again if the size, the scale, the accent or the key changes. */
class PaintCache {
public:
  using Painter = std::function<void(juce::Graphics &)>;

  /** Draws the job with its top left at `at`; it paints from (0, 0). */
  void draw(juce::Graphics &g, juce::Point<float> at, int width, int height,
            juce::int64 key, const Painter &paint);
  void draw(juce::Graphics &g, juce::Rectangle<int> area, juce::int64 key,
            const Painter &paint) {
    draw(g, area.getPosition().toFloat(), area.getWidth(), area.getHeight(), key,
         paint);
  }

private:
  juce::Image image;
  int cachedWidth = 0, cachedHeight = 0;
  float cachedScale = 0.0f, cachedHue = -1.0f;
  juce::int64 cachedKey = 0;
};

enum class Texture {
  brushed, // long horizontal streaks: machined metal
  grain,   // fine speckle: powder coat, velcro
  tolex    // embossed pebbles: amp vinyl
};

/** Lays a texture over an area. Grey-neutral, so it works over any colour. */
void fillTexture(juce::Graphics &, juce::Rectangle<float> area, Texture,
                 float opacity);

juce::Rectangle<float> circle(juce::Point<float> centre, float radius);

/** A machined screw head, turned to wherever `seed` says. */
void drawScrew(juce::Graphics &, juce::Point<float> centre, float radius, int seed);

/** A window cut into the panel: a dark field with its top edge in shadow. */
void drawRecess(juce::Graphics &, juce::Rectangle<float> area, float cornerRadius,
                juce::Colour fill);
void drawRecess(juce::Graphics &, juce::Rectangle<float> area, float cornerRadius);

/** A jewel status light, with a halo when lit. */
void drawLed(juce::Graphics &g, juce::Rectangle<float> led, bool lit,
             juce::Colour colour);

/** A rack module: a screwed-down steel plate with its title silkscreened on,
    followed by a rule. The rule stops `reserveRight` short of its row, leaving
    room for an indicator there. Returns the row. */
juce::Rectangle<float> drawPanel(juce::Graphics &g, juce::Rectangle<int> bounds,
                                 const juce::String &title, float reserveRight = 0.0f);

/** The rack the modules are bolted into. */
void drawChassis(juce::Graphics &g, juce::Rectangle<int> bounds);

/** A strip of the amp's vinyl covering: the header and footer. */
void drawVinyl(juce::Graphics &g, juce::Rectangle<int> bounds);

/** The logo, chrome over the accent's glow, filling this height. */
void drawLogo(juce::Graphics &g, juce::Point<float> topLeft, float height);

/** Paints a knob's fixed parts, centred on `centre`, as lit from above: the
    shadow it throws, its fluted skirt and spun metal cap. */
void paintKnobBody(juce::Graphics &g, juce::Point<float> centre, float radius);

/** A row of power tubes behind the glass: dark until a model's loaded, warming
    up then, and brightening with the signal. */
class TubeBank final : public juce::Component {
public:
  TubeBank() { setInterceptsMouseClicks(false, false); }

  /** Call at the GUI rate. `signal` is 0..1. */
  void update(bool powered, float signal);
  /** Jumps to this state, skipping the warm-up, e.g. as the editor opens. */
  void snapTo(bool powered, float signal);
  void paint(juce::Graphics &) override;

private:
  PaintCache behind, inFront;
  float heat = 0.0f, drive = 0.0f, painted = -1.0f;
};

/** The cabinet's speaker, on its baffle. Dims when the cab is off. */
class Speaker final : public juce::Component {
public:
  Speaker() { setInterceptsMouseClicks(false, false); }
  void setActive(bool);
  void paint(juce::Graphics &) override;

private:
  PaintCache cone;
  bool active = true;
};
} // namespace evil::gui
