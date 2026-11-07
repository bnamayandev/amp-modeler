#include "Hardware.h"

#include "EvilLookAndFeel.h"

#include <vector>

namespace evil::gui {
namespace {
const juce::Colour black = juce::Colours::black, white = juce::Colours::white;

/** A pixel of neutral texture: white or black by sign, stronger the further from
    zero. */
juce::Colour textureColour(float v) {
  return (v >= 0.0f ? white : black).withAlpha(juce::jlimit(0.0f, 1.0f, std::abs(v)));
}

juce::Image makeBrushed() {
  constexpr int width = 512, height = 256, blur = 28;
  juce::Random random(0xe411);
  juce::Image image(juce::Image::ARGB, width, height, true,
                    juce::SoftwareImageType());
  std::vector<float> row((size_t)width), smooth((size_t)width);
  for (int y = 0; y < height; ++y) {
    const float strength = random.nextFloat() * 2.0f - 1.0f;
    for (auto &v : row)
      v = strength * 0.55f + (random.nextFloat() * 2.0f - 1.0f) * 0.45f;
    float sum = 0.0f;
    for (int x = -blur; x <= blur; ++x)
      sum += row[(size_t)((x + width) % width)];
    for (int x = 0; x < width; ++x) { // a running mean, wrapping so it tiles
      smooth[(size_t)x] = sum / (float)(2 * blur + 1);
      sum += row[(size_t)((x + blur + 1) % width)] -
             row[(size_t)((x - blur + width) % width)];
    }
    for (int x = 0; x < width; ++x)
      image.setPixelAt(x, y, textureColour(smooth[(size_t)x] * 1.6f));
  }
  return image;
}

juce::Image makeGrain() {
  constexpr int size = 256;
  juce::Random random(0x6a1e);
  juce::Image image(juce::Image::ARGB, size, size, true, juce::SoftwareImageType());
  for (int y = 0; y < size; ++y)
    for (int x = 0; x < size; ++x)
      image.setPixelAt(x, y, textureColour((random.nextFloat() * 2.0f - 1.0f) * 0.8f));
  return image;
}

juce::Image makeTolex() {
  constexpr int size = 256;
  juce::Random random(0x701e);
  // Pebbles from two scales of smooth noise, lit from the top left.
  const auto layer = [&random](int cell) {
    const int cells = size / cell;
    std::vector<float> points((size_t)(cells * cells));
    for (auto &p : points)
      p = random.nextFloat();
    std::vector<float> out((size_t)(size * size));
    for (int y = 0; y < size; ++y)
      for (int x = 0; x < size; ++x) {
        const float fx = (float)x / (float)cell, fy = (float)y / (float)cell;
        const int x0 = (int)fx % cells, y0 = (int)fy % cells;
        const int x1 = (x0 + 1) % cells, y1 = (y0 + 1) % cells;
        const float tx = fx - std::floor(fx), ty = fy - std::floor(fy);
        const float sx = tx * tx * (3.0f - 2.0f * tx), sy = ty * ty * (3.0f - 2.0f * ty);
        const auto at = [&](int px, int py) { return points[(size_t)(py * cells + px)]; };
        out[(size_t)(y * size + x)] =
            (at(x0, y0) * (1.0f - sx) + at(x1, y0) * sx) * (1.0f - sy) +
            (at(x0, y1) * (1.0f - sx) + at(x1, y1) * sx) * sy;
      }
    return out;
  };
  const auto coarse = layer(4), fine = layer(2);
  const auto heightAt = [&](int x, int y) {
    const auto i = (size_t)(((y + size) % size) * size + ((x + size) % size));
    return coarse[i] + 0.7f * fine[i];
  };
  juce::Image image(juce::Image::ARGB, size, size, true, juce::SoftwareImageType());
  for (int y = 0; y < size; ++y)
    for (int x = 0; x < size; ++x)
      image.setPixelAt(x, y, textureColour((heightAt(x, y) - heightAt(x - 1, y - 1)) * 1.1f));
  return image;
}

struct Textures {
  juce::Image brushed = makeBrushed(), grain = makeGrain(), tolex = makeTolex();
};
} // namespace

void PaintCache::draw(juce::Graphics &g, juce::Point<float> at, int width,
                      int height, juce::int64 key, const Painter &paint) {
  const float scale = juce::jlimit(
      0.25f, 8.0f, g.getInternalContext().getPhysicalPixelScaleFactor());
  const float hue = colours::getAccentHue();
  if (!image.isValid() || width != cachedWidth || height != cachedHeight ||
      !juce::approximatelyEqual(scale, cachedScale) || key != cachedKey ||
      !juce::approximatelyEqual(hue, cachedHue)) {
    image = juce::Image(juce::Image::ARGB,
                        juce::jmax(1, (int)std::ceil((float)width * scale)),
                        juce::jmax(1, (int)std::ceil((float)height * scale)), true);
    juce::Graphics painter(image);
    painter.addTransform(juce::AffineTransform::scale(scale));
    paint(painter);
    cachedWidth = width;
    cachedHeight = height;
    cachedScale = scale;
    cachedHue = hue;
    cachedKey = key;
  }
  const juce::Graphics::ScopedSaveState save(g);
  g.setOpacity(1.0f);
  g.setImageResamplingQuality(juce::Graphics::mediumResamplingQuality);
  g.drawImageTransformed(image, juce::AffineTransform::scale(1.0f / scale)
                                    .translated(at.x, at.y));
}

void fillTexture(juce::Graphics &g, juce::Rectangle<float> area, Texture texture,
                 float opacity) {
  const juce::SharedResourcePointer<Textures> textures;
  const auto &tile = texture == Texture::brushed ? textures->brushed
                     : texture == Texture::grain ? textures->grain
                                                 : textures->tolex;
  const juce::Graphics::ScopedSaveState save(g);
  g.setTiledImageFill(tile, 0, 0, opacity);
  g.fillRect(area);
}

juce::Rectangle<float> circle(juce::Point<float> centre, float radius) {
  return juce::Rectangle<float>(radius * 2.0f, radius * 2.0f).withCentre(centre);
}

void drawScrew(juce::Graphics &g, juce::Point<float> c, float r, int seed) {
  // The countersink, then the head, lit from above.
  g.setColour(black.withAlpha(0.75f));
  g.fillEllipse(circle(c, r + 0.9f));
  g.setColour(white.withAlpha(0.09f));
  g.drawEllipse(circle(c.translated(0.0f, 0.5f), r + 0.9f), 0.7f);
  g.setGradientFill(juce::ColourGradient(juce::Colour(0xff77777c), c.x, c.y - r,
                                         juce::Colour(0xff26262a), c.x, c.y + r, false));
  g.fillEllipse(circle(c, r));

  const float angle = (float)(((unsigned int)seed * 2654435761u) % 628u) / 100.0f;
  juce::Path slot;
  slot.addRoundedRectangle(-r * 0.78f, -r * 0.14f, r * 1.56f, r * 0.28f, r * 0.1f);
  slot.addRoundedRectangle(-r * 0.14f, -r * 0.78f, r * 0.28f, r * 1.56f, r * 0.1f);
  g.setColour(black.withAlpha(0.8f));
  g.fillPath(slot, juce::AffineTransform::rotation(angle).translated(c));
}

void drawRecess(juce::Graphics &g, juce::Rectangle<float> area, float cornerRadius,
                juce::Colour fill) {
  juce::Path shape;
  shape.addRoundedRectangle(area, cornerRadius);

  // The lip it's cut through catches the light along the bottom edge.
  g.setColour(white.withAlpha(0.1f));
  g.fillRoundedRectangle(area.translated(0.0f, 1.0f), cornerRadius);
  g.setColour(fill);
  g.fillPath(shape);
  {
    const juce::Graphics::ScopedSaveState save(g);
    g.reduceClipRegion(shape);
    const float depth = juce::jmin(7.0f, area.getHeight() * 0.4f);
    g.setGradientFill(juce::ColourGradient(black.withAlpha(0.75f), 0.0f, area.getY(),
                                           black.withAlpha(0.0f), 0.0f,
                                           area.getY() + depth, false));
    g.fillRect(area.withHeight(depth));
  }
  g.setColour(black.withAlpha(0.85f));
  g.drawRoundedRectangle(area.reduced(0.5f), cornerRadius, 1.0f);
}

void drawRecess(juce::Graphics &g, juce::Rectangle<float> area, float cornerRadius) {
  drawRecess(g, area, cornerRadius, colours::glass);
}

void drawLed(juce::Graphics &g, juce::Rectangle<float> led, bool lit,
             juce::Colour colour) {
  const auto c = led.getCentre();
  const float r = led.getWidth() * 0.5f;
  if (lit) { // the halo it throws on the panel
    juce::ColourGradient halo(colour.withAlpha(0.62f), c, colour.withAlpha(0.0f),
                              c.translated(r * 3.2f, 0.0f), true);
    halo.addColour(0.35, colour.withAlpha(0.28f));
    g.setGradientFill(halo);
    g.fillEllipse(circle(c, r * 3.2f));
  }
  // The bezel, then the lens.
  g.setColour(black.withAlpha(0.8f));
  g.fillEllipse(circle(c, r + 1.0f));
  g.setColour(white.withAlpha(0.14f));
  g.drawEllipse(circle(c.translated(0.0f, 0.4f), r + 1.0f), 0.6f);
  if (lit) {
    juce::ColourGradient lens(colour.interpolatedWith(white, 0.82f), c,
                              colour.darker(0.25f), c.translated(r, 0.0f), true);
    lens.addColour(0.45, colour.brighter(0.25f));
    g.setGradientFill(lens);
  } else {
    g.setGradientFill(juce::ColourGradient(
        colour.interpolatedWith(black, 0.68f), c.translated(0.0f, -r),
        colour.interpolatedWith(black, 0.9f), c.translated(0.0f, r), false));
  }
  g.fillEllipse(circle(c, r));
  // The glint on the lens.
  g.setColour(white.withAlpha(lit ? 0.7f : 0.28f));
  g.fillEllipse(circle(c.translated(-r * 0.3f, -r * 0.36f), r * 0.26f));
}

juce::Rectangle<float> drawPanel(juce::Graphics &g, juce::Rectangle<int> bounds,
                                 const juce::String &title, float reserveRight) {
  using namespace colours;
  const auto b = bounds.toFloat();
  constexpr float corner = 5.0f;

  juce::DropShadow(black.withAlpha(0.75f), 9, {0, 3}).drawForRectangle(g, bounds);

  juce::Path shape;
  shape.addRoundedRectangle(b, corner);
  {
    const juce::Graphics::ScopedSaveState save(g);
    g.reduceClipRegion(shape);
    juce::ColourGradient face(panel.brighter(0.16f), 0.0f, b.getY(),
                              panel.darker(0.24f), 0.0f, b.getBottom(), false);
    g.setGradientFill(face);
    g.fillRect(b);
    fillTexture(g, b, Texture::brushed, 0.05f);
    fillTexture(g, b, Texture::grain, 0.016f);
    // A soft pool of light from above the rack.
    juce::ColourGradient pool(white.withAlpha(0.05f), b.getCentreX(), b.getY(),
                              white.withAlpha(0.0f), b.getCentreX(),
                              b.getY() + b.getHeight() * 0.8f, true);
    g.setGradientFill(pool);
    g.fillRect(b);
  }
  // A chamfer, catching the light along the top and lost along the bottom.
  g.setColour(black.withAlpha(0.9f));
  g.drawRoundedRectangle(b.reduced(0.5f), corner, 1.0f);
  g.setColour(white.withAlpha(0.13f));
  g.fillRect(b.reduced(corner, 0.0f).withY(b.getY() + 1.0f).withHeight(1.0f));
  g.setColour(black.withAlpha(0.35f));
  g.fillRect(b.reduced(corner, 0.0f).withY(b.getBottom() - 2.0f).withHeight(1.0f));

  constexpr float screwInset = 9.5f, screwRadius = 3.2f;
  int seed = bounds.getX() + 3 * bounds.getY();
  for (const auto at : {b.getTopLeft().translated(screwInset, screwInset),
                        b.getTopRight().translated(-screwInset, screwInset),
                        b.getBottomLeft().translated(screwInset, -screwInset),
                        b.getBottomRight().translated(-screwInset, -screwInset)})
    drawScrew(g, at, screwRadius, ++seed);

  auto row = b.reduced(22.0f, 0.0f).withTop(b.getY() + 8.0f).withHeight(19.0f);
  const auto titleFont = silk(13.5f, 0.13f);
  const auto titleText = title.toUpperCase();
  g.setColour(accent());
  g.fillRect(row.removeFromLeft(2.5f).withSizeKeepingCentre(2.5f, 12.0f));
  row.removeFromLeft(7.0f);
  g.setColour(text);
  g.setFont(titleFont);
  const float titleWidth =
      juce::GlyphArrangement::getStringWidth(titleFont, titleText) + 4.0f;
  g.drawText(titleText, row.removeFromLeft(titleWidth),
             juce::Justification::centredLeft, false);
  row.removeFromLeft(8.0f);

  // An engraved rule: a dark line with its lit lower lip.
  const float y = row.getCentreY();
  g.setColour(black.withAlpha(0.65f));
  const auto rule = row.withTrimmedRight(reserveRight);
  g.fillRect(rule.withY(y).withHeight(1.0f));
  g.setColour(white.withAlpha(0.07f));
  g.fillRect(rule.withY(y + 1.0f).withHeight(1.0f));
  return row;
}

void drawChassis(juce::Graphics &g, juce::Rectangle<int> bounds) {
  const auto b = bounds.toFloat();
  g.setColour(colours::chassis);
  g.fillRect(b);
  fillTexture(g, b, Texture::grain, 0.035f);
}

void drawVinyl(juce::Graphics &g, juce::Rectangle<int> bounds) {
  const auto b = bounds.toFloat();
  g.setGradientFill(juce::ColourGradient(juce::Colour(0xff222225), 0.0f, b.getY(),
                                         juce::Colour(0xff111113), 0.0f, b.getBottom(),
                                         false));
  g.fillRect(b);
  fillTexture(g, b, Texture::tolex, 0.2f);
}

void drawLogo(juce::Graphics &g, juce::Point<float> topLeft, float height) {
  using namespace colours;
  const auto wordFont = logoFont(height * 0.86f);
  juce::GlyphArrangement word;
  word.addLineOfText(wordFont, "Evil", 0.0f, 0.0f);
  juce::Path outline;
  word.createPath(outline);

  // Fit the letters' real ink to the top three quarters of the height.
  const auto ink = outline.getBounds();
  const float wordHeight = height * 0.76f;
  const float fit = wordHeight / ink.getHeight();
  outline.applyTransform(juce::AffineTransform::translation(-ink.getX(), -ink.getY())
                             .scaled(fit)
                             .translated(topLeft));
  const auto letters = outline.getBounds();

  // Backlit, so the chrome stands off a glow in the accent.
  juce::DropShadow(accent().withAlpha(0.9f), juce::roundToInt(height * 0.42f), {})
      .drawForPath(g, outline);
  juce::DropShadow(black.withAlpha(0.9f), 2, {0, 1}).drawForPath(g, outline);

  // Chrome: sky above the horizon line, ground below.
  juce::ColourGradient chrome(white, 0.0f, letters.getY(), juce::Colour(0xffa9a9b0), 0.0f,
                              letters.getBottom(), false);
  chrome.addColour(0.46, juce::Colour(0xffd9d9df));
  chrome.addColour(0.52, juce::Colour(0xff55555c));
  chrome.addColour(0.64, juce::Colour(0xff9b9ba2));
  g.setGradientFill(chrome);
  g.fillPath(outline);
  g.setColour(black.withAlpha(0.55f));
  g.strokePath(outline, juce::PathStrokeType(0.6f));

  // The line under it, tracked out to the width of the word.
  const auto sub = height * 0.19f;
  const juce::String subtitle("AMPLIFIER");
  auto subFont = silk(sub, 0.0f);
  const float bare = juce::GlyphArrangement::getStringWidth(subFont, subtitle);
  subFont = silk(sub, juce::jmax(0.0f, (letters.getWidth() - bare) /
                                           ((float)subtitle.length() * subFont.getHeight())));
  g.setFont(subFont);
  g.setColour(text);
  g.drawText(subtitle,
             juce::Rectangle<float>(letters.getX(), letters.getBottom() + height * 0.035f,
                                    letters.getWidth() + 8.0f, sub * 1.2f),
             juce::Justification::centredLeft, false);
}

void paintKnobBody(juce::Graphics &g, juce::Point<float> c, float r) {
  // The shadow, thrown down and away from the light above.
  {
    const auto at = c.translated(0.0f, r * 0.2f);
    const float reach = r * 1.3f;
    juce::ColourGradient shadow(black.withAlpha(0.8f), at, black.withAlpha(0.0f),
                                at.translated(reach, 0.0f), true);
    shadow.addColour(0.72, black.withAlpha(0.62f));
    g.setGradientFill(shadow);
    g.fillEllipse(circle(at, reach));
  }

  // The skirt, fluted round its edge. Flutes facing up catch the light.
  g.setGradientFill(juce::ColourGradient(juce::Colour(0xff4a4a50), c.x, c.y - r,
                                         juce::Colour(0xff0b0b0d), c.x, c.y + r, false));
  g.fillEllipse(circle(c, r));
  const int flutes = juce::jlimit(20, 64, (int)(r * 2.0f));
  const float fluteWidth = juce::jmax(0.7f, r * 0.05f);
  for (int i = 0; i < flutes * 2; ++i) {
    const float angle = juce::MathConstants<float>::pi * (float)i / (float)flutes;
    const float light = 0.5f + 0.5f * std::cos(angle);
    const juce::Line<float> flute(c.getPointOnCircumference(r * 0.87f, angle),
                                  c.getPointOnCircumference(r - 0.2f, angle));
    g.setColour(i % 2 == 0 ? black.withAlpha(0.62f)
                           : white.withAlpha(0.03f + 0.25f * light * light));
    g.drawLine(flute, fluteWidth);
  }

  // The body: a dark shoulder, lit along its top.
  const float body = r * 0.85f;
  g.setColour(black.withAlpha(0.85f));
  g.fillEllipse(circle(c, body + 0.8f));
  g.setGradientFill(juce::ColourGradient(juce::Colour(0xff36363b), c.x, c.y - body,
                                         juce::Colour(0xff121214), c.x, c.y + body, false));
  g.fillEllipse(circle(c, body));
  {
    juce::Path rim;
    rim.addCentredArc(c.x, c.y, body - 0.6f, body - 0.6f, 0.0f, -1.2f, 1.2f, true);
    g.setColour(white.withAlpha(0.2f));
    g.strokePath(rim, juce::PathStrokeType(juce::jmax(0.7f, r * 0.025f),
                                           juce::PathStrokeType::curved,
                                           juce::PathStrokeType::rounded));
  }

  // The cap: spun metal, its bow-tie highlights turned towards the light.
  const float cap = r * 0.56f;
  g.setColour(black.withAlpha(0.8f));
  g.fillEllipse(circle(c, cap + 1.0f));
  constexpr int wedges = 72;
  const auto dark = juce::Colour(0xff36363a), bright = juce::Colour(0xffc4c4ca);
  for (int i = 0; i < wedges; ++i) {
    const float from = juce::MathConstants<float>::twoPi * (float)i / wedges;
    const float to = juce::MathConstants<float>::twoPi * (float)(i + 1) / wedges;
    const float middle = (from + to) * 0.5f;
    const float sheen = std::pow(0.5f + 0.5f * std::cos(2.0f * (middle - 0.45f)), 3.0f);
    juce::Path wedge;
    wedge.addPieSegment(circle(c, cap), from, to + 0.015f, 0.0f);
    g.setColour(dark.interpolatedWith(bright, sheen));
    g.fillPath(wedge);
  }
  // Lathe rings, then a shade over the lower half and a lit chamfer.
  g.setColour(black.withAlpha(0.07f));
  for (float ring = cap * 0.25f; ring < cap; ring += juce::jmax(1.4f, cap * 0.09f))
    g.drawEllipse(circle(c, ring), 0.6f);
  g.setGradientFill(juce::ColourGradient(white.withAlpha(0.14f), c.x, c.y - cap,
                                         black.withAlpha(0.38f), c.x, c.y + cap, false));
  g.fillEllipse(circle(c, cap));
  g.setColour(white.withAlpha(0.28f));
  g.drawEllipse(circle(c, cap - 0.4f), 0.7f);
}

// --- tubes -------------------------------------------------------------------
namespace {
constexpr int numTubes = 3;

juce::Rectangle<float> tubeArea(juce::Rectangle<float> bounds, int index) {
  const float width = bounds.getWidth() / numTubes;
  return bounds.withX(bounds.getX() + width * (float)index)
      .withWidth(width)
      .reduced(width * 0.07f, 0.0f);
}

juce::Path tubeGlass(juce::Rectangle<float> tube) {
  const float baseTop = tube.getBottom() - tube.getHeight() * 0.14f;
  juce::Path glass;
  glass.addRoundedRectangle(tube.getX(), tube.getY(), tube.getWidth(),
                            baseTop - tube.getY() + 4.0f, tube.getWidth() * 0.46f,
                            tube.getWidth() * 0.46f, true, true, false, false);
  return glass;
}

/** The plate, with the slot the heater shows through. */
juce::Rectangle<float> plateOf(juce::Rectangle<float> tube) {
  return juce::Rectangle<float>(tube.getWidth() * 0.5f, tube.getHeight() * 0.42f)
      .withCentre({tube.getCentreX(), tube.getY() + tube.getHeight() * 0.47f});
}

void paintTubeBehind(juce::Graphics &g, juce::Rectangle<float> tube) {
  const auto glass = tubeGlass(tube);
  const auto cx = tube.getCentreX();
  g.setColour(juce::Colour(0xff0a0b0d));
  g.fillPath(glass);
  {
    const juce::Graphics::ScopedSaveState save(g);
    g.reduceClipRegion(glass);
    // The getter flash, silvering the crown of the glass.
    const float crown = tube.getHeight() * 0.13f;
    g.setGradientFill(juce::ColourGradient(juce::Colour(0xff4c4c52), cx, tube.getY(),
                                           juce::Colour(0x00000000), cx,
                                           tube.getY() + crown * 1.5f, false));
    g.fillRect(tube.withHeight(crown * 1.5f));

    // The internals: mica spacers and the plate, in dark steel.
    const auto plate = plateOf(tube);
    g.setColour(juce::Colour(0xff34343a));
    const auto mica = juce::Rectangle<float>(tube.getWidth() * 0.74f, 2.0f);
    g.fillRect(mica.withCentre({cx, plate.getY() - 5.0f}));
    g.fillRect(mica.withCentre({cx, plate.getBottom() + 5.0f}));
    g.setGradientFill(juce::ColourGradient(juce::Colour(0xff4b4b52), plate.getX(), 0.0f,
                                           juce::Colour(0xff17171a), plate.getRight(), 0.0f,
                                           false));
    g.fillRoundedRectangle(plate, 1.5f);
    g.setColour(black.withAlpha(0.92f));
    g.fillRect(plate.withSizeKeepingCentre(plate.getWidth() * 0.26f, plate.getHeight() - 8.0f));
  }
}

void paintTubeInFront(juce::Graphics &g, juce::Rectangle<float> tube) {
  const auto glass = tubeGlass(tube);
  const float baseTop = tube.getBottom() - tube.getHeight() * 0.14f;

  // The glass catches the room along one side, and has a thin bright edge.
  {
    const juce::Graphics::ScopedSaveState save(g);
    g.reduceClipRegion(glass);
    const auto streak = tube.withWidth(tube.getWidth() * 0.16f)
                            .translated(tube.getWidth() * 0.12f, tube.getHeight() * 0.1f)
                            .withHeight(tube.getHeight() * 0.62f);
    g.setGradientFill(juce::ColourGradient(white.withAlpha(0.32f), streak.getCentreX(),
                                           streak.getY(), white.withAlpha(0.0f),
                                           streak.getCentreX(), streak.getBottom(), false));
    g.fillRoundedRectangle(streak, streak.getWidth() * 0.5f);
  }
  g.setColour(white.withAlpha(0.2f));
  g.strokePath(glass, juce::PathStrokeType(0.9f));

  // The bakelite base, and the socket it plugs into.
  const auto base = tube.withTop(baseTop);
  g.setGradientFill(juce::ColourGradient(juce::Colour(0xff35302b), base.getX(), 0.0f,
                                         juce::Colour(0xff0e0d0c), base.getRight(), 0.0f, false));
  g.fillRoundedRectangle(base.withTrimmedBottom(3.0f), 2.0f);
  g.setColour(white.withAlpha(0.1f));
  g.fillRect(base.withHeight(1.0f));
  g.setGradientFill(juce::ColourGradient(juce::Colour(0xff57575d), base.getX(), 0.0f,
                                         juce::Colour(0xff1c1c1f), base.getRight(), 0.0f, false));
  g.fillRoundedRectangle(base.withTop(base.getBottom() - 4.0f).expanded(3.0f, 0.0f), 1.5f);
}

void paintTubeGlow(juce::Graphics &g, juce::Rectangle<float> tube, float brightness) {
  if (brightness <= 0.002f)
    return;
  const auto glass = tubeGlass(tube);
  const auto plate = plateOf(tube);
  const auto centre = plate.getCentre();
  // Warmer than the accent itself, as a heater's own orange shows through.
  const auto hot = colours::accent().interpolatedWith(juce::Colour(0xffffa640), 0.22f);

  // Light spilling out past the glass, onto the display behind.
  {
    const float reach = tube.getWidth() * (0.9f + 0.55f * brightness);
    juce::ColourGradient spill(hot.withAlpha(0.42f * brightness), centre,
                               hot.withAlpha(0.0f), centre.translated(reach, 0.0f), true);
    g.setGradientFill(spill);
    g.fillEllipse(circle(centre, reach));
  }
  {
    const juce::Graphics::ScopedSaveState save(g);
    g.reduceClipRegion(glass);
    // The glow inside the glass.
    const float reach = tube.getHeight() * 0.48f;
    juce::ColourGradient inside(hot.withAlpha(0.78f * brightness), centre,
                                hot.withAlpha(0.0f), centre.translated(reach, 0.0f), true);
    g.setGradientFill(inside);
    g.fillEllipse(circle(centre, reach));

    // The heater, white-hot through the plate's slot.
    const auto slot =
        plate.withSizeKeepingCentre(plate.getWidth() * 0.2f, plate.getHeight() - 10.0f);
    g.setColour(hot.withAlpha(juce::jmin(1.0f, 1.3f * brightness)));
    g.fillRoundedRectangle(slot, 1.0f);
    g.setColour(hot.interpolatedWith(white, 0.82f).withAlpha(juce::jmin(1.0f, brightness * 1.5f)));
    g.fillRoundedRectangle(slot.reduced(slot.getWidth() * 0.28f, 1.0f), 0.8f);
  }
}
} // namespace

void TubeBank::update(bool powered, float signal) {
  // Slow to warm up and cool down, quick to answer the guitar.
  heat += (powered ? 1.0f : 0.0f) > heat ? juce::jmin(0.035f, 1.0f - heat)
                                         : -juce::jmin(0.05f, heat);
  const float target = juce::jlimit(0.0f, 1.0f, signal);
  drive += (target > drive ? 0.5f : 0.08f) * (target - drive);
  const float brightness = heat * (0.42f + 0.58f * drive);
  if (std::abs(brightness - painted) > 0.006f)
    repaint();
}

void TubeBank::snapTo(bool powered, float signal) {
  heat = powered ? 1.0f : 0.0f;
  drive = juce::jlimit(0.0f, 1.0f, signal);
  repaint();
}

void TubeBank::paint(juce::Graphics &g) {
  const auto bounds = getLocalBounds().toFloat();
  const auto tubes = [bounds](juce::Graphics &c, auto &&paintOne) {
    for (int i = 0; i < numTubes; ++i)
      paintOne(c, tubeArea(bounds, i));
  };
  behind.draw(g, getLocalBounds(), 0,
              [&](juce::Graphics &c) { tubes(c, paintTubeBehind); });
  painted = heat * (0.42f + 0.58f * drive);
  for (int i = 0; i < numTubes; ++i)
    paintTubeGlow(g, tubeArea(bounds, i), painted);
  inFront.draw(g, getLocalBounds(), 0,
               [&](juce::Graphics &c) { tubes(c, paintTubeInFront); });
}

// --- speaker -----------------------------------------------------------------
void Speaker::setActive(bool shouldBeActive) {
  if (active != shouldBeActive) {
    active = shouldBeActive;
    repaint();
  }
}

void Speaker::paint(juce::Graphics &g) {
  const auto size = juce::jmin(getWidth(), getHeight());
  cone.draw(g, getLocalBounds(), 0, [size](juce::Graphics &c) {
    const auto centre = juce::Point<float>((float)size, (float)size) * 0.5f;
    const float r = (float)size * 0.5f - 1.0f;

    // The frame, with its four mounting bolts.
    c.setGradientFill(juce::ColourGradient(juce::Colour(0xff3a3a3f), centre.x, centre.y - r,
                                           juce::Colour(0xff121214), centre.x, centre.y + r,
                                           false));
    c.fillEllipse(circle(centre, r));
    c.setColour(black.withAlpha(0.9f));
    c.drawEllipse(circle(centre, r), 1.0f);
    for (int i = 0; i < 4; ++i)
      drawScrew(c,
                centre.getPointOnCircumference(
                    r * 0.92f, juce::MathConstants<float>::pi * (0.25f + 0.5f * (float)i)),
                juce::jmax(1.6f, r * 0.045f), i + 7);

    // The rolled surround.
    const float surround = r * 0.86f;
    juce::ColourGradient roll(juce::Colour(0xff050506), centre, juce::Colour(0xff26262a),
                              centre.translated(surround, 0.0f), true);
    roll.addColour(0.86, juce::Colour(0xff050506));
    roll.addColour(0.93, juce::Colour(0xff2c2c30));
    c.setGradientFill(roll);
    c.fillEllipse(circle(centre, surround));

    // The paper cone: darker towards its throat, with pressed ribs.
    const float paper = surround * 0.84f;
    juce::ColourGradient coneFill(juce::Colour(0xff0a0a0b), centre, juce::Colour(0xff2a2826),
                                  centre.translated(paper, 0.0f), true);
    coneFill.addColour(0.4, juce::Colour(0xff121212));
    c.setGradientFill(coneFill);
    c.fillEllipse(circle(centre, paper));
    for (float ring = 0.46f; ring < 0.98f; ring += 0.13f) {
      c.setColour(black.withAlpha(0.5f));
      c.drawEllipse(circle(centre, paper * ring), 0.8f);
      c.setColour(white.withAlpha(0.045f));
      c.drawEllipse(circle(centre, paper * ring + 0.9f), 0.7f);
    }
    // The room's light across the cone.
    juce::Path sheen;
    sheen.addPieSegment(circle(centre, paper), -0.9f, 0.5f, 0.32f);
    c.setColour(white.withAlpha(0.035f));
    c.fillPath(sheen);

    // The dust cap, a dome.
    const float dome = paper * 0.34f;
    c.setColour(black.withAlpha(0.8f));
    c.fillEllipse(circle(centre.translated(0.0f, 1.0f), dome + 1.5f));
    juce::ColourGradient cap(juce::Colour(0xff58585e), centre.translated(-dome * 0.3f, -dome * 0.42f),
                             juce::Colour(0xff0e0e10), centre.translated(dome * 0.7f, dome * 0.9f),
                             true);
    c.setGradientFill(cap);
    c.fillEllipse(circle(centre, dome));
  });
  if (!active) { // off: the speaker in the dark
    g.setColour(black.withAlpha(0.55f));
    g.fillEllipse(circle(getLocalBounds().toFloat().getCentre(), (float)size * 0.5f));
  }
}
} // namespace evil::gui
