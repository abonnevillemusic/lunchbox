cmake_minimum_required(VERSION 3.22)

project(Neve1073EQ VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT TARGET juce::juce_core)
    if(DEFINED JUCE_DIR AND EXISTS "${JUCE_DIR}/CMakeLists.txt")
        add_subdirectory("${JUCE_DIR}" JUCE EXCLUDE_FROM_ALL)
    else()
        include(FetchContent)
        FetchContent_Declare(
            JUCE
            GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
            GIT_TAG        8.0.4
            GIT_SHALLOW    TRUE
        )
        FetchContent_MakeAvailable(JUCE)
    endif()
endif()

juce_add_plugin(Neve1073EQ
    COMPANY_NAME                "Pluginz Audio"
    BUNDLE_ID                   "com.pluginzaudio.nv73eq"
    IS_SYNTH                    FALSE
    NEEDS_MIDI_INPUT            FALSE
    NEEDS_MIDI_OUTPUT           FALSE
    IS_MIDI_EFFECT              FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD     FALSE
    VST3_AUTO_MANIFEST          FALSE
    PLUGIN_MANUFACTURER_CODE    Plgz
    PLUGIN_CODE                 N73e
    FORMATS                     AU VST3 Standalone
    PRODUCT_NAME                "NV-73EQ Inductor EQ"
)

target_sources(Neve1073EQ
    PRIVATE
        Source/PluginProcessor.cpp
        Source/PluginEditor.cpp
)

target_include_directories(Neve1073EQ
    PRIVATE
        Source
        "${CMAKE_CURRENT_SOURCE_DIR}/../Neve1073Preamp/Source"
        "${CMAKE_CURRENT_SOURCE_DIR}/../SDK500/Include"
)

target_compile_definitions(Neve1073EQ
    PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
)

target_link_libraries(Neve1073EQ
    PRIVATE
        juce::juce_audio_utils
        juce::juce_audio_processors
        juce::juce_gui_basics
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)

# Pure C++20 Headless Circuit Verification Target
enable_testing()
add_executable(Neve1073EQDspTests
    Tests/EQVerificationTest.cpp
)
target_include_directories(Neve1073EQDspTests
    PRIVATE
        Source
        "${CMAKE_CURRENT_SOURCE_DIR}/../Neve1073Preamp/Source"
)
add_test(NAME Neve1073EQDspTests COMMAND Neve1073EQDspTests)
