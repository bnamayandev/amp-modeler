#pragma once

#include <juce_core/juce_core.h>

#include <initializer_list>
#include <utility>

/** Small helpers for building JSON request bodies out of juce::var. */
namespace evil::agent::json {
inline juce::var object(
    std::initializer_list<std::pair<const char *, juce::var>> items = {}) {
  auto *o = new juce::DynamicObject();
  for (const auto &[key, value] : items)
    o->setProperty(key, value);
  return juce::var(o);
}

inline juce::var array(std::initializer_list<juce::var> items = {}) {
  juce::Array<juce::var> a;
  for (const auto &item : items)
    a.add(item);
  return juce::var(a);
}

inline juce::String dump(const juce::var &v) {
  return juce::JSON::toString(v, true);
}

/** The elements of a JSON array; empty for anything else, so a malformed reply
    reads as "nothing there" rather than crashing. */
inline juce::Array<juce::var> items(const juce::var &v) {
  if (const auto *a = v.getArray())
    return *a;
  return {};
}
} // namespace evil::agent::json
