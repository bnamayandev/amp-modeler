# Third-party dependencies, pinned to exact versions.
#
# Set FETCHCONTENT_SOURCE_DIR_<NAME> (e.g. -DFETCHCONTENT_SOURCE_DIR_JUCE=~/src/JUCE) to build
# against a local checkout instead of downloading.

include(FetchContent)

FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 8.0.15
    GIT_SHALLOW TRUE)

# NeuralAmpModelerCore is not added as a subdirectory (its CMakeLists builds tools and
# forces global flags). SOURCE_SUBDIR points at a directory without a CMakeLists.txt so
# MakeAvailable only downloads it; nam_core below compiles its sources.
FetchContent_Declare(NeuralAmpModelerCore
    GIT_REPOSITORY https://github.com/sdatkinson/NeuralAmpModelerCore.git
    GIT_TAG v0.6.0
    GIT_SHALLOW TRUE
    GIT_SUBMODULES ""
    SOURCE_SUBDIR do-not-add-subdirectory)

# The exact Eigen commit NeuralAmpModelerCore v0.6.0 pins as its submodule.
FetchContent_Declare(Eigen
    URL https://gitlab.com/libeigen/eigen/-/archive/bc3b39870ecb690a623a3f49149a358b95c5781d/eigen-bc3b39870ecb690a623a3f49149a358b95c5781d.tar.gz
    SOURCE_SUBDIR do-not-add-subdirectory
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)

FetchContent_MakeAvailable(JUCE NeuralAmpModelerCore Eigen)

if(EVIL_BUILD_TESTS)
    FetchContent_Declare(Catch2
        GIT_REPOSITORY https://github.com/catchorg/Catch2.git
        GIT_TAG v3.8.1
        GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(Catch2)
endif()

# --- nam_core: NeuralAmpModelerCore as a static library ------------------------------------
set(NAM_DIR ${neuralampmodelercore_SOURCE_DIR})
file(GLOB NAM_SOURCES CONFIGURE_DEPENDS "${NAM_DIR}/NAM/*.cpp" "${NAM_DIR}/NAM/*/*.cpp")

add_library(nam_core STATIC ${NAM_SOURCES})
target_include_directories(nam_core SYSTEM PUBLIC
    "${NAM_DIR}"
    "${NAM_DIR}/Dependencies/nlohmann"
    "${eigen_SOURCE_DIR}")
target_compile_features(nam_core PUBLIC cxx_std_20)
target_compile_definitions(nam_core PUBLIC
    NAM_SAMPLE_FLOAT          # process float buffers directly: no conversion copies on the audio thread
    NAM_ENABLE_A2_FAST)       # upstream default: fast path for "A2" WaveNet models
set_target_properties(nam_core PROPERTIES POSITION_INDEPENDENT_CODE ON)

if(MSVC)
    # PRIVATE: only NAM's own sources need these; JUCE sets up <windows.h> itself.
    target_compile_definitions(nam_core PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN _USE_MATH_DEFINES)
    target_compile_options(nam_core PRIVATE /bigobj /W0 "$<$<NOT:$<CONFIG:Debug>>:/O2>")
else()
    # Upstream code is not warning-clean under our flags; it is third-party, so silence it.
    target_compile_options(nam_core PRIVATE -w "$<$<NOT:$<CONFIG:Debug>>:-O3>")
endif()
