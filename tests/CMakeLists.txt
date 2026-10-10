juce_add_console_app(evil_tests PRODUCT_NAME "evil_tests")

target_sources(evil_tests PRIVATE
    AllocationTracker.cpp
    TestMain.cpp
    TestDsp.cpp
    TestResampler.cpp
    TestNam.cpp
    TestRealtime.cpp)

target_compile_definitions(evil_tests PRIVATE
    ${EVIL_JUCE_DEFINITIONS}
    EVIL_TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data"
    NAM_EXAMPLE_MODELS_DIR="${NAM_DIR}/example_models")

# No juce_recommended_warning_flags here: tests compare floats exactly on purpose
# (bit-exact pass-through, determinism). The shared sources are warning-checked in the
# plugin and CLI builds.
target_link_libraries(evil_tests
    PRIVATE
        evil_core
        Catch2::Catch2
    PUBLIC
        juce::juce_recommended_config_flags)

add_test(NAME evil_tests COMMAND evil_tests)
