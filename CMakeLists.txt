cmake_minimum_required(VERSION 3.24) # 3.24: $<LINK_LIBRARY:WHOLE_ARCHIVE>

# Must be set before project(). C++20 std::filesystem needs macOS 10.15; 11.0 covers arm64.
set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS version")

project(EvilAmplifier VERSION 0.2.0 LANGUAGES C CXX)

# Windows: link the C++ runtime statically so users don't need the Visual C++ redistributable.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    # Neural inference through Eigen is far too slow for real time in an unoptimised build.
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

option(EVIL_BUILD_TESTS "Build the Catch2 test suite" ON)
option(EVIL_BUILD_TOOLS "Build the evil-cli render/benchmark tool" ON)
option(EVIL_COPY_PLUGIN_AFTER_BUILD "Install plugins into the user plugin folders after building" OFF)

include(cmake/Dependencies.cmake)

# --- Plugin -------------------------------------------------------------------------------
set(EVIL_FORMATS VST3 Standalone)
if(APPLE)
    list(APPEND EVIL_FORMATS AU)
endif()

juce_add_plugin(EvilAmplifier
    PRODUCT_NAME "Evil Amplifier"
    COMPANY_NAME "Evil Amplifier"
    BUNDLE_ID com.evilamplifier.evilamplifier
    VERSION ${PROJECT_VERSION}
    PLUGIN_MANUFACTURER_CODE Evla
    PLUGIN_CODE Evam
    FORMATS ${EVIL_FORMATS}
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE # the agent tab has a text box
    NEEDS_CURL TRUE # JUCE's only HTTPS on Linux; loaded at runtime, see below
    VST3_CATEGORIES Fx Distortion
    AU_MAIN_TYPE kAudioUnitType_Effect
    MICROPHONE_PERMISSION_ENABLED TRUE
    MICROPHONE_PERMISSION_TEXT "Evil Amplifier processes your guitar from the audio input."
    COPY_PLUGIN_AFTER_BUILD ${EVIL_COPY_PLUGIN_AFTER_BUILD})

juce_add_binary_data(EvilAmplifierData
    HEADER_NAME EvilBinaryData.h
    NAMESPACE EvilBinaryData
    SOURCES
        resources/fonts/Barlow-Regular.ttf
        resources/fonts/Barlow-Medium.ttf
        resources/fonts/BarlowSemiCondensed-SemiBold.ttf
        resources/fonts/BarlowCondensed-ExtraBoldItalic.ttf
        resources/fonts/ShareTechMono-Regular.ttf
        resources/fonts/NewRocker-Regular.ttf
        resources/irs/EvilCab4x12.wav
        resources/models/BlackstarHT1.nam
        resources/presets/factory_presets.xml
        resources/brand/TONE3000-logo.png
        resources/brand/T3K-mark.png)
set_target_properties(EvilAmplifierData PROPERTIES POSITION_INDEPENDENT_CODE ON)

# DSP + state code shared by the plugin, the tests and the CLI. An INTERFACE library so each
# consumer compiles it with its own JUCE module configuration.
add_library(evil_core INTERFACE)
target_sources(evil_core INTERFACE
    source/agent/AgentSettings.cpp
    source/agent/Http.cpp
    source/agent/ProcessorAmp.cpp
    source/agent/Prompt.cpp
    source/agent/Provider.cpp
    source/agent/Session.cpp
    source/agent/ToneMeter.cpp
    source/agent/Tone3000.cpp
    source/agent/Tools.cpp
    source/dsp/CabSim.cpp
    source/dsp/Metronome.cpp
    source/dsp/ModelManager.cpp
    source/dsp/NamModelRunner.cpp
    source/dsp/Pedals.cpp
    source/dsp/PitchDetector.cpp
    source/dsp/PitchShifter.cpp
    source/dsp/ReverbPedal.cpp
    source/dsp/SignalChain.cpp
    source/dsp/StreamingResampler.cpp
    source/record/Mp4Writer.cpp
    source/record/Recorder.cpp
    source/state/Parameters.cpp
    source/state/PresetManager.cpp
    source/state/Tunings.cpp)
target_include_directories(evil_core INTERFACE source)
target_link_libraries(evil_core INTERFACE
    # NAM Core registers each architecture (WaveNet, LSTM, ...) from a static initialiser in
    # an otherwise unreferenced object file; a plain static link would drop them.
    "$<LINK_LIBRARY:WHOLE_ARCHIVE,nam_core>"
    EvilAmplifierData
    faac_core
    juce::juce_audio_utils
    juce::juce_cryptography # SHA-256 for the TONE3000 sign-in
    juce::juce_dsp)

# JUCE's built-in Linux sockets have no TLS, so the agent's HTTPS needs libcurl. It's loaded
# when first used rather than linked, so a machine without it still runs the amp: the agent
# tab says what's missing. Building needs the headers (libcurl4-openssl-dev).
set(EVIL_JUCE_DEFINITIONS
    JUCE_WEB_BROWSER=0
    JUCE_LOAD_CURL_SYMBOLS_LAZILY=1
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_STRICT_REFCOUNTEDPOINTER=1
    JUCE_MODAL_LOOPS_PERMITTED=0)

# Linux: build JACK support when its headers are installed (libjack-jackd2-dev). JUCE loads
# libjack at runtime; with PipeWire, launching via `pw-jack` gives the lowest-latency path.
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    find_path(JACK_INCLUDE_DIR jack/jack.h)
    if(JACK_INCLUDE_DIR)
        list(APPEND EVIL_JUCE_DEFINITIONS JUCE_JACK=1 "JUCE_JACK_CLIENT_NAME=\"Evil Amplifier\"")
        message(STATUS "JACK support: on")
    else()
        message(STATUS "JACK support: off (install libjack-jackd2-dev to enable)")
    endif()
endif()

# Windows: ASIO in the standalone's audio settings. JUCE 8 bundles the ASIO SDK headers
# (GPLv3), so no separate download is needed. Without ASIO, Windows Audio (shared mode) only
# offers the system mixer's single sample rate and buffer size.
if(WIN32)
    list(APPEND EVIL_JUCE_DEFINITIONS JUCE_ASIO=1)
endif()

target_sources(EvilAmplifier PRIVATE
    source/PluginProcessor.cpp
    source/PluginEditor.cpp
    source/PipeWireJack.cpp
    source/StandaloneApp.cpp
    source/gui/AgentPage.cpp
    source/gui/EvilLookAndFeel.cpp
    source/gui/Hardware.cpp
    source/gui/PedalBoard.cpp
    source/gui/Settings.cpp
    source/gui/TunerPage.cpp
    source/gui/Widgets.cpp)
target_compile_definitions(EvilAmplifier PUBLIC
    ${EVIL_JUCE_DEFINITIONS}
    JUCE_USE_CUSTOM_PLUGIN_STANDALONE_APP=1) # source/StandaloneApp.cpp: no title bar
target_link_libraries(EvilAmplifier
    PRIVATE
        evil_core
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

# --- Tests ----------------------------------------------------------------------------------
if(EVIL_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

# --- CLI: offline render, CPU benchmark, model/IR checks -------------------------------------
if(EVIL_BUILD_TOOLS)
    juce_add_console_app(evil-cli PRODUCT_NAME "evil-cli" NEEDS_CURL TRUE)
    target_sources(evil-cli PRIVATE tools/evil_cli.cpp)
    target_compile_definitions(evil-cli PRIVATE ${EVIL_JUCE_DEFINITIONS})
    target_link_libraries(evil-cli
        PRIVATE
            evil_core
        PUBLIC
            juce::juce_recommended_config_flags
            juce::juce_recommended_warning_flags)

    # Renders the editor offscreen to PNG (docs, CI artefacts, layout checks).
    juce_add_console_app(evil-snapshot PRODUCT_NAME "evil-snapshot" NEEDS_CURL TRUE)
    target_sources(evil-snapshot PRIVATE
        tools/evil_snapshot.cpp
        source/PluginProcessor.cpp
        source/PluginEditor.cpp
        source/PipeWireJack.cpp
        source/StandaloneApp.cpp
        source/gui/AgentPage.cpp
        source/gui/EvilLookAndFeel.cpp
        source/gui/Hardware.cpp
        source/gui/PedalBoard.cpp
        source/gui/Settings.cpp
        source/gui/TunerPage.cpp
        source/gui/Widgets.cpp)
    target_compile_definitions(evil-snapshot PRIVATE
        ${EVIL_JUCE_DEFINITIONS}
        JucePlugin_Name="Evil Amplifier"
        JucePlugin_VersionString="${PROJECT_VERSION}")
    target_link_libraries(evil-snapshot
        PRIVATE
            evil_core
        PUBLIC
            juce::juce_recommended_config_flags)
endif()
