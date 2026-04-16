cmake_minimum_required(VERSION 3.22)
set(CMAKE_POLICY_VERSION_MINIMUM 3.5 CACHE STRING "")
project(SkoomaLoud VERSION 1.0.1)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)
FetchContent_Declare(
    JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        8.0.6
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(JUCE)

juce_add_plugin(SkoomaLoud
    COMPANY_NAME       "skoomaDSP"
    PLUGIN_MANUFACTURER_CODE Skom
    PLUGIN_CODE        SkLd
    FORMATS            VST3
    PRODUCT_NAME       "skoomaLoud"
    IS_SYNTH           FALSE
    NEEDS_MIDI_INPUT   FALSE
    NEEDS_MIDI_OUTPUT  FALSE
    IS_MIDI_EFFECT     FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE
)

juce_add_binary_data(FontData
    SOURCES
        Source/fonts/JetBrainsMono-Bold.ttf
        Source/icons/theme.svg
)

target_sources(SkoomaLoud
    PRIVATE
        Source/PluginProcessor.cpp
        Source/PluginEditor.cpp
        Source/dsp/LoudnessMeter.cpp
)

target_include_directories(SkoomaLoud
    PRIVATE
        Source
)

target_compile_definitions(SkoomaLoud
    PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
)

target_link_libraries(SkoomaLoud
    PRIVATE
        FontData
        juce::juce_audio_utils
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags
)
