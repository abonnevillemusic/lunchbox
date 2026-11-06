cmake_minimum_required(VERSION 3.22)

project(LBX500Chassis VERSION 1.0.0 LANGUAGES C CXX)

juce_add_plugin(LBX500Chassis
    COMPANY_NAME                "Pluginz Audio"
    BUNDLE_ID                   "com.pluginzaudio.lbx500chassis"
    IS_SYNTH                    FALSE
    NEEDS_MIDI_INPUT            FALSE
    NEEDS_MIDI_OUTPUT           FALSE
    IS_MIDI_EFFECT              FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD     FALSE
    VST3_AUTO_MANIFEST          FALSE
    PLUGIN_MANUFACTURER_CODE    Plgz
    PLUGIN_CODE                 Lb50
    FORMATS                     AU VST3 Standalone
    PRODUCT_NAME                "LBX-500 Lunchbox"
)

target_sources(LBX500Chassis
    PRIVATE
        Source/PluginProcessor.cpp
        Source/PluginEditor.cpp
)

target_include_directories(LBX500Chassis
    PRIVATE
        Source
        "${CMAKE_CURRENT_SOURCE_DIR}/../SDK500/Include"
        "${CMAKE_CURRENT_SOURCE_DIR}/../Neve1073Preamp/Source"
        "${CMAKE_CURRENT_SOURCE_DIR}/../Neve1073Preamp/Source/Module500"
        "${CMAKE_CURRENT_SOURCE_DIR}/../Neve1073EQ/Source"
        "${CMAKE_CURRENT_SOURCE_DIR}/../Neve1073EQ/Source/Module500"
)

target_compile_definitions(LBX500Chassis
    PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
)

target_link_libraries(LBX500Chassis
    PRIVATE
        juce::juce_audio_utils
        juce::juce_audio_processors
        juce::juce_gui_basics
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)

add_executable(LBX500BackplaneTests
    Tests/ChassisBackplaneTest.cpp
)

target_include_directories(LBX500BackplaneTests
    PRIVATE
        Source
        "${CMAKE_CURRENT_SOURCE_DIR}/../SDK500/Include"
        "${CMAKE_CURRENT_SOURCE_DIR}/../Neve1073Preamp/Source"
        "${CMAKE_CURRENT_SOURCE_DIR}/../Neve1073Preamp/Source/Module500"
        "${CMAKE_CURRENT_SOURCE_DIR}/../Neve1073EQ/Source"
        "${CMAKE_CURRENT_SOURCE_DIR}/../Neve1073EQ/Source/Module500"
)

target_compile_definitions(LBX500BackplaneTests
    PRIVATE
        JUCE_GLOBAL_MODULE_SETTINGS_INCLUDED=1
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
)

target_link_libraries(LBX500BackplaneTests
    PRIVATE
        juce::juce_gui_basics
        juce::juce_core
)

add_test(NAME LBX500BackplaneTests COMMAND LBX500BackplaneTests)
