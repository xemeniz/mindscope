cmake_minimum_required(VERSION 3.22)
project(Mindscope VERSION 0.1.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(APPLE)
    set(MS_FORMATS AU VST3 Standalone)
else()
    set(MS_FORMATS VST3 Standalone)
endif()

include(FetchContent)
FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 8.0.4
    GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(JUCE)

# Medidor para el master: onda por bandas, espectro, loudness (EBU R128) y estéreo. No toca el audio.
juce_add_plugin(Mindscope
    COMPANY_NAME "Dj Nane"
    BUNDLE_ID com.djnane.mindscope
    PLUGIN_MANUFACTURER_CODE DjNa
    PLUGIN_CODE MnSc
    FORMATS ${MS_FORMATS}
    NEEDS_WEB_BROWSER TRUE
    PRODUCT_NAME "MINDSCOPE"
    PLUGIN_NAME "MINDSCOPE"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    COPY_PLUGIN_AFTER_BUILD FALSE
    AU_MAIN_TYPE kAudioUnitType_Effect
    VST3_CATEGORIES Fx Analyzer)

juce_add_binary_data(MsData SOURCES ui/index.html)

target_sources(Mindscope PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/Analyzer.cpp
    Source/Spectrum.cpp)

target_compile_definitions(Mindscope PUBLIC
    JUCE_WEB_BROWSER=1
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0)

target_link_libraries(Mindscope
    PRIVATE
        MsData
        juce::juce_audio_utils
        juce::juce_gui_extra
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)

# Pruebas del motor sin interfaz (Linux/CI): cmake -DMS_TESTS=ON
option(MS_TESTS "Build analyzer tests" OFF)
if(MS_TESTS)
    juce_add_console_app(MsTest PRODUCT_NAME "MsTest")
    target_sources(MsTest PRIVATE tests/analyzer_test.cpp Source/Analyzer.cpp Source/Spectrum.cpp)
    target_compile_definitions(MsTest PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
    target_link_libraries(MsTest PRIVATE juce::juce_dsp juce::juce_recommended_config_flags)
endif()
if(MS_TESTS)
    # el audio sale idéntico (usa el plugin entero)
    juce_add_console_app(MsPassTest PRODUCT_NAME "MsPassTest")
    target_sources(MsPassTest PRIVATE tests/pass_test.cpp)
    target_link_libraries(MsPassTest PRIVATE Mindscope)
    target_include_directories(MsPassTest PRIVATE $<TARGET_PROPERTY:Mindscope,INCLUDE_DIRECTORIES>)
    target_compile_definitions(MsPassTest PRIVATE $<TARGET_PROPERTY:Mindscope,COMPILE_DEFINITIONS>)
endif()
