cmake_minimum_required(VERSION 3.22)

project(Decoherence VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Static MSVC runtime: the plugin runs on machines without the VC++ redistributable.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

add_subdirectory(JUCE)

juce_add_plugin(Decoherence
    VERSION                     1.0.0
    COMPANY_NAME                "Decoherence"
    BUNDLE_ID                   com.decoherence.plugin
    PLUGIN_MANUFACTURER_CODE    Dcoh
    PLUGIN_CODE                 Dcr1
    FORMATS                     VST3 Standalone
    PRODUCT_NAME                "Decoherence"
    IS_SYNTH                    FALSE
    NEEDS_MIDI_INPUT            FALSE
    NEEDS_MIDI_OUTPUT           FALSE
    IS_MIDI_EFFECT              FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD     FALSE
    VST3_CATEGORIES             Fx Reverb
    NEEDS_WEB_BROWSER           FALSE
    NEEDS_CURL                  FALSE)

target_sources(Decoherence
    PRIVATE
        Source/PluginProcessor.cpp
        Source/PluginEditor.cpp
        Source/dsp/CascadeEngine.cpp
        Source/ui/WaterfallLookAndFeel.cpp
        Source/ui/Widgets.cpp)

target_compile_definitions(Decoherence
    PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0)

target_link_libraries(Decoherence
    PRIVATE
        juce::juce_audio_utils
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

# Stand-alone engine test + demo renderer (no JUCE dependency)
option(CW_BUILD_TESTS "Build the engine test tool" ON)
if(CW_BUILD_TESTS)
    add_executable(EngineTest tests/EngineTest.cpp Source/dsp/CascadeEngine.cpp)

    # Loads the built VST3 through JUCE's hosting code, like a DAW would.
    juce_add_console_app(HostTest PRODUCT_NAME "HostTest")
    target_sources(HostTest PRIVATE tests/HostTest.cpp)
    target_compile_definitions(HostTest PRIVATE
        JUCE_PLUGINHOST_VST3=1
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0)
    target_link_libraries(HostTest PRIVATE
        juce::juce_audio_processors
        juce::juce_gui_basics
        juce::juce_recommended_config_flags)
endif()

# Marketing helper: renders the live editor and a YouTube thumbnail (tools\Thumbnail.cpp)
option(CW_BUILD_TOOLS "Build the thumbnail/snapshot tool" OFF)
if(CW_BUILD_TOOLS)
    juce_add_console_app(DecoherenceThumbnail PRODUCT_NAME "DecoherenceThumbnail")
    target_sources(DecoherenceThumbnail PRIVATE
        tools/Thumbnail.cpp
        Source/PluginProcessor.cpp
        Source/PluginEditor.cpp
        Source/dsp/CascadeEngine.cpp
        Source/ui/WaterfallLookAndFeel.cpp
        Source/ui/Widgets.cpp)
    target_compile_definitions(DecoherenceThumbnail PRIVATE
        JucePlugin_Name="Decoherence"
        JUCE_MODAL_LOOPS_PERMITTED=1
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0)
    target_link_libraries(DecoherenceThumbnail PRIVATE
        juce::juce_audio_utils
        juce::juce_recommended_config_flags)
endif()
