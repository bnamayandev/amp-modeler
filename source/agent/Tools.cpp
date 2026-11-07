#include "Tools.h"

#include "Json.h"
#include "dsp/ModelManager.h"
#include "state/Parameters.h"

namespace evil::agent {
namespace {
juce::var stringProperty(const char *description) {
  return json::object({{"type", "string"}, {"description", description}});
}

juce::var schema(std::initializer_list<std::pair<const char *, juce::var>> properties,
                 std::initializer_list<juce::var> required) {
  return json::object({{"type", "object"},
                       {"properties", json::object(properties)},
                       {"required", json::array(required)}});
}

ToolBox::Outcome reply(const ToolCall &call, const juce::var &value) {
  return {{call.id, call.name, json::dump(value), false}, false};
}

ToolBox::Outcome problem(const ToolCall &call, const juce::String &message) {
  return {{call.id, call.name, json::dump(json::object({{"error", message}})), true},
          false};
}

/** The gear types a NAM file can stand in for the amp as. There's one neural
    slot, so a capture of a pedal would replace the amp rather than sit before it. */
bool canBeTheAmp(const juce::String &gear) {
  return gear == "amp" || gear == "amp-cab" || gear == "full-rig";
}

bool includesCab(const juce::String &gear) {
  return gear == "amp-cab" || gear == "full-rig";
}

juce::String formatDb(float db) { return juce::String(db, 1) + " dB"; }
} // namespace

ToolBox::ToolBox(AmpControl &a, Tone3000 &t, Note n, Tone3000::ShouldCancel cancel)
    : amp(a), tone3000(t), note(std::move(n)), shouldCancel(std::move(cancel)) {}

std::vector<ToolSpec> ToolBox::specs() {
  return {
      {"set_parameters",
       "Turn the amp's controls, and they can be set while a pedal is off, ready for "
       "a later part of a song. Give each change exactly one of clock, dial or value. "
       "Applies straight away, and the player hears it. The result gives what was "
       "really set, which can differ if one was out of range or over a safety limit.",
       schema({{"changes",
                json::object(
                    {{"type", "array"},
                     {"items",
                      schema({{"id", stringProperty("A control's id from the table")},
                              {"clock",
                               stringProperty("A knob's position on a clock face, as "
                                              "guides give it: \"10:30\", \"noon\". "
                                              "7:00 is full left, 5:00 full right.")},
                              {"dial",
                               json::object({{"type", "number"},
                                             {"description",
                                              "A knob's position as a number from 0 "
                                              "to 10, as guides also give it."}})},
                              {"value",
                               json::object({{"type", "number"},
                                             {"description",
                                              "In the control's own units, when "
                                              "you know them: dB, Hz, ms. Switches: "
                                              "1 for on, 0 for off."}})}},
                             {"id"})}})}},
              {"changes"})},
      {"reset_controls",
       "Put every pedal off, every knob back to its starting position and the EQ flat, "
       "leaving the loaded capture, the cabinet IR and the output level alone. Do this "
       "first when starting on a new tone, or whatever was set for the last one "
       "stays in the sound.",
       schema({}, {})},
      {"measure_tone",
       "Measure the sound as it's now set, as you can't hear it. Plays a test guitar "
       "through a copy of the amp, leaving the live sound alone, and reports its "
       "spectrum, how hard it distorts and how much it compresses, each with the same "
       "for the capture alone to compare. Modulation, delay and reverb are left out. "
       "Use it after setting up a tone, and fix what doesn't fit what you're after.",
       schema({}, {})},
      {"load_builtin",
       "Load a capture or cabinet that ships with the amp. Needs no account.",
       schema({{"kind", json::object({{"type", "string"},
                                      {"enum", json::array({"model", "ir"})}})},
               {"name", stringProperty("A built-in's name from the system prompt, "
                                       "or \"none\" with kind \"model\" to play "
                                       "with no amp model")}},
              {"kind", "name"})},
      {"list_tone3000_tones",
       "List tones from the player's TONE3000 account or its fixed feeds. Worth "
       "checking their favorites first: they already like those.",
       schema({{"list",
                json::object({{"type", "string"},
                              {"enum", json::array({"favorited", "downloaded",
                                                    "created", "trending", "latest"})}})},
               {"gear", stringProperty("Optional: amp, amp-cab, pedal, outboard, "
                                       "cab, space or experimental")},
               {"query", stringProperty("Optional words to match in the title. "
                                        "Favorited, downloaded and created only.")}},
              {"list"})},
      {"inspect_tone3000_tone",
       "See one TONE3000 tone and its files: what was captured, by whom, and the "
       "variants available (often several gain settings).",
       schema({{"tone", stringProperty("The tone's page URL, or the number at the "
                                       "end of it")}},
              {"tone"})},
      {"load_tone3000_capture",
       "Download one file of a TONE3000 tone and load it. A NAM amp or amp-and-cab "
       "capture becomes the amp, and the cabinet switches to suit it. An IR becomes "
       "the cabinet. The output level is trimmed so the new capture isn't a jump in "
       "volume.",
       schema({{"tone", stringProperty("The tone's page URL or number")},
               {"model_id",
                json::object({{"type", "integer"},
                              {"description",
                               "A file's id from inspect_tone3000_tone"}})}},
              {"tone", "model_id"})},
  };
}

ToolBox::Outcome ToolBox::execute(const ToolCall &call) {
  if (call.name == "set_parameters")
    return setParameters(call);
  if (call.name == "reset_controls")
    return resetControls(call);
  if (call.name == "measure_tone")
    return measureTone(call);
  if (call.name == "load_builtin")
    return loadBuiltin(call);
  if (call.name == "list_tone3000_tones")
    return listTones(call);
  if (call.name == "inspect_tone3000_tone")
    return inspectTone(call);
  if (call.name == "load_tone3000_capture")
    return loadCapture(call);
  return problem(call, "There's no tool called " + call.name + ".");
}

float ToolBox::positionFromClock(const juce::String &raw) {
  auto text = raw.trim().toLowerCase().replace("o'clock", "").replace("oclock", "").trim();
  if (text == "noon")
    text = "12";
  const auto hourText = text.upToFirstOccurrenceOf(":", false, false).trim();
  const auto minuteText = text.fromFirstOccurrenceOf(":", false, false).trim();
  if (hourText.isEmpty() || !hourText.containsOnly("0123456789") ||
      !minuteText.containsOnly("0123456789"))
    return -1.0f;
  const int hour = hourText.getIntValue();
  const int minute = minuteText.getIntValue();
  if (hour < 1 || hour > 12 || minute > 59)
    return -1.0f;
  const float hoursFromSeven = (float)(hour >= 7 ? hour - 7 : hour + 5) + (float)minute / 60.0f;
  return hoursFromSeven <= 10.0f ? hoursFromSeven / 10.0f : -1.0f; // nothing past 5:00
}

juce::String ToolBox::clockFromPosition(float position) {
  const int minutes = juce::roundToInt(juce::jlimit(0.0f, 1.0f, position) * 600.0f);
  const int hour = (7 + minutes / 60 - 1) % 12 + 1;
  return juce::String(hour) + ":" + juce::String(minutes % 60).paddedLeft('0', 2);
}

float ToolBox::limitValue(const ParameterInfo &p, float wanted, juce::String &why) {
  auto value = juce::jlimit(p.min, p.max, wanted);
  if (p.isSwitch)
    return value >= 0.5f ? 1.0f : 0.0f;

  if (p.id == params::output) {
    // Someone may be wearing headphones, and no web page gets to change that.
    const auto ceiling = juce::jmin(maxOutputDb, p.value + maxOutputRisePerCallDb);
    if (value > ceiling) {
      value = juce::jmax(ceiling, juce::jmin(value, p.value));
      why = "output can rise at most " + formatDb(maxOutputRisePerCallDb) +
            " at a time, to " + formatDb(maxOutputDb);
    }
  } else if (p.id == params::input && value > maxInputDb) {
    value = maxInputDb;
    why = "input is limited to " + formatDb(maxInputDb);
  }
  if (why.isEmpty() && !juce::approximatelyEqual(value, wanted))
    why = "the control's range is " + juce::String(p.min) + " to " + juce::String(p.max);
  return value;
}

float ToolBox::loudnessTrimDb(std::optional<double> from, std::optional<double> to) {
  if (!from.has_value() || !to.has_value())
    return unknownLoudnessTrimDb;
  return juce::jlimit(-36.0f, maxOutputRisePerCallDb, (float)(*from - *to));
}

std::optional<ParameterInfo> ToolBox::findParameter(const juce::String &id) const {
  for (const auto &p : amp.getParameters())
    if (p.id == id)
      return p;
  return {};
}

ToolBox::Outcome ToolBox::setParameters(const ToolCall &call) {
  auto applied = json::array(), rejected = json::array();
  juce::StringArray described;
  bool changed = false;

  for (const auto &change : json::items(call.arguments["changes"])) {
    const auto id = change["id"].toString();
    const auto rawValue = change["value"];
    const auto clock = change["clock"].toString();
    const auto dial = change["dial"];
    const auto p = findParameter(id);
    if (!p.has_value()) {
      rejected.append(json::object(
          {{"id", id},
           {"why", "No such control. The transposer and metronome are the "
                   "player's, so they aren't available."}}));
      continue;
    }
    const auto isNumber = [](const juce::var &v) {
      return (v.isDouble() || v.isInt() || v.isInt64() || v.isBool()) &&
             std::isfinite((double)v);
    };

    float wanted = 0.0f;
    if (clock.isNotEmpty()) {
      const auto position = positionFromClock(clock);
      if (position < 0.0f) {
        rejected.append(json::object(
            {{"id", id}, {"why", "That isn't a position between 7:00 and 5:00."}}));
        continue;
      }
      wanted = amp.valueAtPosition(id, position);
    } else if (isNumber(dial)) {
      wanted = amp.valueAtPosition(id, juce::jlimit(0.0f, 1.0f, (float)(double)dial / 10.0f));
    } else if (isNumber(rawValue)) {
      // The Filter's stored value runs against its knob, so this is its position.
      wanted = id == params::distTone ? amp.valueAtPosition(id, (float)(double)rawValue)
                                      : (float)(double)rawValue;
    } else {
      rejected.append(json::object(
          {{"id", id}, {"why", "It needs a clock position, a dial number or a value."}}));
      continue;
    }

    juce::String why;
    const auto value = limitValue(*p, wanted, why);
    if (!amp.setParameter(id, value)) {
      rejected.append(json::object({{"id", id}, {"why", "The amp didn't accept it."}}));
      continue;
    }
    changed = true;
    const auto after = findParameter(id);
    // Rounded: a float's 0.550000011920929 is only noise to a model.
    auto entry = json::object(
        {{"id", id}, {"value", juce::roundToInt(value * 1000.0f) / 1000.0}});
    if (after.has_value()) {
      entry.getDynamicObject()->setProperty("panel_shows", after->text);
      if (!after->isSwitch)
        entry.getDynamicObject()->setProperty("clock", clockFromPosition(after->position));
    }
    if (why.isNotEmpty())
      entry.getDynamicObject()->setProperty("limited", why);
    applied.append(entry);
    if (after.has_value())
      described.add(after->name + " " + after->text);
  }

  if (!described.isEmpty()) {
    constexpr int shown = 6;
    auto line = described.joinIntoString(", ", 0, shown);
    if (described.size() > shown)
      line << " and " << (described.size() - shown) << " more";
    note("set " + line, {});
  }
  auto outcome = reply(call, json::object({{"applied", applied}, {"rejected", rejected}}));
  outcome.changedAmp = changed;
  return outcome;
}

ToolBox::Outcome ToolBox::resetControls(const ToolCall &call) {
  bool changed = false;
  for (const auto &p : amp.getParameters())
    if (p.id != params::output && !juce::approximatelyEqual(p.value, p.defaultValue))
      changed = amp.setParameter(p.id, p.defaultValue) || changed;
  note("cleared the pedals and EQ, to start from the capture alone", {});
  auto outcome = reply(call, json::object({{"reset", true}}));
  outcome.changedAmp = changed;
  return outcome;
}

ToolBox::Outcome ToolBox::measureTone(const ToolCall &call) {
  const auto reading = amp.measureTone();
  if (reading.error.isNotEmpty())
    return problem(call, "It couldn't be measured: " + reading.error);

  const auto oneDecimal = [](float x) { return juce::roundToInt(x * 10.0f) / 10.0; };
  auto bands = json::object(), changes = json::object();
  for (size_t b = 0; b < reading.bandDb.size(); ++b) {
    const auto label = params::eqBandLabel(b);
    bands.getDynamicObject()->setProperty(label, oneDecimal(reading.bandDb[b]));
    changes.getDynamicObject()->setProperty(
        label, oneDecimal(reading.bandDb[b] - reading.bandDbCaptureAlone[b]));
  }
  note("measured the tone: " + juce::String(juce::roundToInt(reading.distortionHardPercent)) +
           "% distortion picked hard, " + juce::String(oneDecimal(reading.touchDb)) +
           " dB of touch response",
       {});
  return reply(
      call,
      json::object(
          {{"level_db", oneDecimal(reading.levelDb)},
           {"level_change_from_capture_alone_db",
            oneDecimal(reading.levelDb - reading.levelDbCaptureAlone)},
           {"spectrum_db_by_eq_band", bands},
           {"spectrum_change_from_capture_alone_db", changes},
           {"touch_db", oneDecimal(reading.touchDb)},
           {"touch_db_capture_alone", oneDecimal(reading.touchDbCaptureAlone)},
           {"distortion_percent_picked_gently", oneDecimal(reading.distortionLightPercent)},
           {"distortion_percent_picked_hard", oneDecimal(reading.distortionHardPercent)},
           {"distortion_percent_picked_gently_capture_alone",
            oneDecimal(reading.distortionLightCaptureAlone)},
           {"distortion_percent_picked_hard_capture_alone",
            oneDecimal(reading.distortionHardCaptureAlone)}}));
}

bool ToolBox::setOutput(float db) {
  const auto p = findParameter(params::output);
  return p.has_value() && amp.setParameter(params::output, juce::jlimit(p->min, p->max, db));
}

juce::String ToolBox::swapModel(const std::function<juce::String()> &load) {
  // There's no telling how loud the file is until it's loaded, so duck first.
  const auto output = findParameter(params::output);
  const auto before = amp.getModel();
  if (output.has_value())
    setOutput(output->value - swapDuckDb);

  const auto error = load();
  if (error.isNotEmpty()) {
    if (output.has_value())
      setOutput(output->value);
    return error;
  }
  if (output.has_value())
    setOutput(juce::jmin(juce::jmax(output->value, maxOutputDb),
                         output->value +
                             loudnessTrimDb(before.loudnessDb, amp.getModel().loudnessDb)));
  return {};
}

ToolBox::Outcome ToolBox::loadBuiltin(const ToolCall &call) {
  const auto kind = call.arguments["kind"].toString();
  const auto name = call.arguments["name"].toString();
  ToolBox::Outcome outcome;

  if (kind == "model") {
    if (name == "none") {
      const auto error = amp.unloadModel();
      if (error.isNotEmpty())
        return problem(call, error);
      note("removed the amp model", {});
    } else {
      if (!amp.getBuiltinModels().contains(name))
        return problem(call, "No built-in model called " + name + ". There are: " +
                                 amp.getBuiltinModels().joinIntoString(", "));
      const auto error = swapModel([this, &name] {
        return amp.loadModel(ModelManager::builtinPrefix + name);
      });
      if (error.isNotEmpty())
        return problem(call, error);
      note("loaded the built-in " + name + " model", {});
    }
  } else if (kind == "ir") {
    if (!amp.getBuiltinIrs().contains(name))
      return problem(call, "No built-in IR called " + name + ". There are: " +
                               amp.getBuiltinIrs().joinIntoString(", "));
    const auto error = amp.loadIr(CabSim::builtinPrefix + name);
    if (error.isNotEmpty())
      return problem(call, error);
    note("loaded the built-in " + name + " cabinet", {});
  } else {
    return problem(call, "kind has to be model or ir.");
  }
  outcome = reply(call, json::object({{"loaded", name}}));
  outcome.changedAmp = true;
  return outcome;
}

juce::String ToolBox::ensureConnected() {
  if (!tone3000.hasKey())
    return "TONE3000 isn't set up: the player needs to add their publishable key "
           "under keys. Until then, use the built-in capture and the pedals, and "
           "tell them.";
  if (tone3000.isConnected())
    return {};
  const juce::String notSignedIn =
      "The player isn't signed in to TONE3000. Use the built-in capture and the "
      "pedals, and tell them they can sign in under keys.";
  if (!signInPromptAllowed)
    return notSignedIn;

  const auto error = tone3000.signInWithCode(shouldCancel, signInWaitMs);
  if (error.isEmpty())
    return {};
  // Once is enough: they're not asked again on every message.
  signInPromptAllowed = false;
  return notSignedIn + " (" + error + ")";
}

ToolBox::Outcome ToolBox::listTones(const ToolCall &call) {
  const auto list = call.arguments["list"].toString();
  if (!juce::StringArray{"favorited", "downloaded", "created", "trending", "latest"}
           .contains(list))
    return problem(call, "That isn't one of the lists.");
  if (const auto notReady = ensureConnected(); notReady.isNotEmpty())
    return problem(call, notReady);
  juce::String error;
  const auto tones = tone3000.listTones(list, call.arguments["gear"].toString(),
                                        call.arguments["query"].toString(), error);
  if (error.isNotEmpty())
    return problem(call, error);

  auto found = json::array();
  for (const auto &tone : json::items(tones))
    found.append(json::object({{"id", tone["id"]},
                               {"credit", Tone3000::credit(tone)},
                               {"gear", tone["gear"]},
                               {"format", tone["format"]}}));
  note("read your TONE3000 " + list + " list", {});
  return reply(call, json::object({{"tones", found}}));
}

ToolBox::Outcome ToolBox::inspectTone(const ToolCall &call) {
  const auto toneId = Tone3000::parseToneId(call.arguments["tone"]);
  if (toneId <= 0)
    return problem(call, "That isn't a tone number or a tone page's URL.");
  if (const auto notReady = ensureConnected(); notReady.isNotEmpty())
    return problem(call, notReady);

  juce::String error;
  const auto tone = tone3000.getTone(toneId, error);
  if (error.isNotEmpty() || !tone.isObject())
    return problem(call, error.isNotEmpty() ? error : "TONE3000 sent nothing for this tone.");
  const auto format = tone["format"].toString();
  const auto models = tone3000.listModels(toneId, format, error);
  if (error.isNotEmpty())
    return problem(call, error);

  auto files = json::array();
  constexpr int maxFiles = 40; // some packs have hundreds
  for (const auto &model : json::items(models)) {
    if (files.getArray()->size() >= maxFiles)
      break;
    files.append(json::object({{"id", model["id"]},
                               {"name", model["name"]},
                               {"size", model["size"]},
                               {"architecture", model["architecture_version"]}}));
  }

  note("looked at " + Tone3000::credit(tone), {{"on TONE3000", Tone3000::pageUrl(tone)}});
  return reply(
      call,
      json::object(
          {{"id", toneId},
           {"title", tone["title"]},
           {"creator_and_license", Tone3000::credit(tone)},
           {"gear", tone["gear"]},
           {"format", format},
           {"description", tone["description"].toString().substring(0, 800)},
           {"downloads", tone["downloads_count"]},
           {"favorites", tone["favorites_count"]},
           {"files", files},
           {"usable_as",
            format == "ir"            ? "the cabinet"
            : format != "nam"         ? "nothing: this amp only loads NAM and IR files"
            : canBeTheAmp(tone["gear"].toString())
                ? "the amp"
                : "nothing: there's one neural slot and it's the amp, so use a "
                  "built-in pedal instead"}}));
}

ToolBox::Outcome ToolBox::loadCapture(const ToolCall &call) {
  const auto toneId = Tone3000::parseToneId(call.arguments["tone"]);
  const auto modelId = (juce::int64)call.arguments["model_id"];
  if (toneId <= 0 || modelId <= 0)
    return problem(call, "That needs a tone and a file's id.");
  if (const auto notReady = ensureConnected(); notReady.isNotEmpty())
    return problem(call, notReady);

  // Everything to download comes from TONE3000 by these IDs. A URL from a model
  // or a web page is never fetched, let alone sent the sign-in.
  juce::String error;
  const auto tone = tone3000.getTone(toneId, error);
  if (error.isNotEmpty() || !tone.isObject())
    return problem(call, error.isNotEmpty() ? error : "TONE3000 sent nothing for this tone.");
  const auto format = tone["format"].toString();
  const auto gear = tone["gear"].toString();
  if (format != "nam" && format != "ir")
    return problem(call, "This amp only loads NAM and IR files, and this is " + format + ".");
  if (format == "nam" && !canBeTheAmp(gear))
    return problem(call, "This is a capture of a " + gear +
                             ", and the one neural slot is the amp. Use a built-in "
                             "pedal instead and say it's an approximation.");

  juce::var model;
  for (const auto &candidate : json::items(tone3000.listModels(toneId, format, error)))
    if ((juce::int64)candidate["id"] == modelId)
      model = candidate;
  if (!model.isObject())
    return problem(call, error.isNotEmpty()
                             ? error
                             : "This tone has no file with that id. "
                               "inspect_tone3000_tone lists them.");

  note("downloading " + model["name"].toString() + " from TONE3000", {});
  const auto file = tone3000.download(tone, model, error);
  if (file == juce::File())
    return problem(call, error);
  const bool isNamFile = file.hasFileExtension("nam");
  if (isNamFile != (format == "nam"))
    return problem(call, "That file isn't the kind this tone says it is.");

  const std::vector<Source> links{{"on TONE3000", Tone3000::pageUrl(tone)}};
  auto result = json::object({{"loaded", model["name"]},
                              {"credit", Tone3000::credit(tone)},
                              {"gear", gear}});
  if (isNamFile) {
    const auto loadError = swapModel(
        [this, &file] { return amp.loadModel(file.getFullPathName()); });
    if (loadError.isNotEmpty())
      return problem(call, "It wouldn't load: " + loadError);
    // A capture with a cab in it plays through no second cab, and vice versa.
    const bool cabOn = !includesCab(gear);
    amp.setParameter(params::cabOn, cabOn ? 1.0f : 0.0f);
    result.getDynamicObject()->setProperty("cabinet_on", cabOn);
    note("loaded amp: " + Tone3000::credit(tone) +
             (cabOn ? ", through the cabinet" : ", cabinet off since it has its own"),
         links);
  } else {
    const auto loadError = amp.loadIr(file.getFullPathName());
    if (loadError.isNotEmpty())
      return problem(call, "It wouldn't load: " + loadError);
    amp.setParameter(params::cabOn, 1.0f);
    result.getDynamicObject()->setProperty("cabinet_on", true);
    note("loaded cabinet: " + Tone3000::credit(tone), links);
  }
  if (const auto output = findParameter(params::output))
    result.getDynamicObject()->setProperty("output_db", output->value);

  auto outcome = reply(call, result);
  outcome.changedAmp = true;
  return outcome;
}

juce::String tidyForDisplay(const juce::String &modelText) {
  juce::StringArray lines;
  for (auto line : juce::StringArray::fromLines(modelText)) {
    line = line.replace("**", "").replace("__", "").trimEnd();
    const auto trimmed = line.trimStart();
    if (trimmed.startsWith("#"))
      line = trimmed.trimCharactersAtStart("#").trimStart();
    else if (trimmed.startsWith("- ") || trimmed.startsWith("* "))
      line = line.upToFirstOccurrenceOf(trimmed, false, false) +
             juce::String::charToString(0x2022) + trimmed.substring(1);
    lines.add(line);
  }
  return lines.joinIntoString("\n").trim();
}

juce::String describeAmpState(AmpControl &amp) {
  const auto model = amp.getModel();
  const auto ir = amp.getIr();
  juce::String state;
  state << "[The amp right now. Model: "
        << (model.name.isNotEmpty() ? model.name : juce::String("none"))
        << ". Cabinet IR: " << ir.name << ". Controls away from their default: ";
  juce::StringArray changed;
  for (const auto &p : amp.getParameters())
    if (!juce::approximatelyEqual(p.value, p.defaultValue))
      changed.add(p.id + "=" + (p.isSwitch ? (p.value >= 0.5f ? juce::String("on") : "off")
                                           : p.text + " (" +
                                                 ToolBox::clockFromPosition(p.position) + ")"));
  state << (changed.isEmpty() ? juce::String("none") : changed.joinIntoString(", "))
        << ".]";
  return state;
}
} // namespace evil::agent
