cmake_minimum_required(VERSION 3.22)

project(MidiCoach VERSION 0.1.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Link the MSVC runtime statically so the built exe is copy-and-run on a clean
# Windows machine, with no Visual C++ Redistributable to install. This has to be
# set before JUCE's targets are created - otherwise they compile against the DLL
# runtime and the link fails on the mismatch.
if(MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

# Optional: path to the Steinberg ASIO SDK. JUCE isn't allowed to redistribute
# it, so download it from https://www.steinberg.net/developers/ and point this
# at the extracted folder, e.g. -DMIDICOACH_ASIO_SDK_PATH=C:/asiosdk
set(MIDICOACH_ASIO_SDK_PATH "" CACHE PATH "Path to the Steinberg ASIO SDK")

# Optional: reuse an existing JUCE checkout instead of cloning a fresh copy.
set(MIDICOACH_JUCE_PATH "" CACHE PATH "Existing JUCE checkout to build against")

if(MIDICOACH_JUCE_PATH)
    add_subdirectory("${MIDICOACH_JUCE_PATH}" juce-build)
else()
    include(FetchContent)
    FetchContent_Declare(
        JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG 7.0.12
        GIT_SHALLOW TRUE
    )
    FetchContent_MakeAvailable(JUCE)
endif()

juce_add_gui_app(MidiCoach
    PRODUCT_NAME "MidiCoach"
    COMPANY_NAME "MidiCoach"
)

juce_generate_juce_header(MidiCoach)

# The music font, converted from Bravura and renamed.
#
# The TrueType conversion is deliberate. JUCE embeds fonts on Windows through
# GDI's AddFontMemResourceEx, which cannot load OpenType/CFF ("OTTO") fonts -
# it silently substitutes a different font and every SMuFL codepoint comes back
# as "glyph not found".
#
# The rename is a licensing requirement: Bravura is under the SIL Open Font
# License with the Reserved Font Name "Bravura", and converting the outlines
# makes this a Modified Version, which may not carry that name. Produced from
# the upstream .otf by Tools/otf_to_ttf.py; regenerate it when updating Bravura.
juce_add_binary_data(MidiCoachData SOURCES Resources/MidiCoachMusic.ttf)

target_sources(MidiCoach PRIVATE
    Source/Main.cpp
    Source/MainComponent.cpp
    Source/Model/Melody.cpp
    Source/Model/MidiIO.cpp
    Source/Model/TuneLibrary.cpp
    Source/Export/ScorePdf.cpp
    Source/UI/NoteValueButton.cpp
    Source/Theory/MusicTheory.cpp
    Source/Analysis/PitchDetector.cpp
    Source/Analysis/AudioAnalyser.cpp
    Source/Audio/MelodyPlayer.cpp
    Source/Audio/Metronome.cpp
    Source/Audio/PianoSynth.cpp
    Source/Practice/RehearsalEngine.cpp
    Source/Practice/ExerciseGenerator.cpp
    Source/Practice/CorpusData.cpp
    Source/Audio/PluginHost.cpp
    Source/UI/NotationComponent.cpp
    Source/UI/ScaleKeyboardComponent.cpp
    Source/UI/MusicFont.cpp
    Source/UI/StaffLayout.cpp
    Source/UI/MelodyStaffComponent.cpp
    Source/UI/Beaming.cpp
    Source/UI/SettingsPanel.cpp
)

target_compile_definitions(MidiCoach PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_PLUGINHOST_VST3=1
    JUCE_PLUGINHOST_VST=0
    JUCE_PLUGINHOST_AU=0
    JUCE_APPLICATION_NAME_STRING="$<TARGET_PROPERTY:MidiCoach,JUCE_PRODUCT_NAME>"
    JUCE_APPLICATION_VERSION_STRING="$<TARGET_PROPERTY:MidiCoach,JUCE_VERSION>"
)

if(MIDICOACH_ASIO_SDK_PATH)
    target_compile_definitions(MidiCoach PRIVATE JUCE_ASIO=1)
    target_include_directories(MidiCoach PRIVATE "${MIDICOACH_ASIO_SDK_PATH}/common")
    message(STATUS "MidiCoach: ASIO enabled (${MIDICOACH_ASIO_SDK_PATH})")
else()
    target_compile_definitions(MidiCoach PRIVATE JUCE_ASIO=0)
    message(STATUS "MidiCoach: ASIO disabled - set MIDICOACH_ASIO_SDK_PATH to enable")
endif()

target_link_libraries(MidiCoach PRIVATE
    MidiCoachData
    juce::juce_audio_utils
    juce::juce_audio_devices
    juce::juce_audio_processors
    juce::juce_audio_basics
    juce::juce_dsp
    juce::juce_gui_extra
    juce::juce_gui_basics
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags
)

#==============================================================================
# Structural tests for the melody model. Pure logic, so it builds as a console
# app against juce_core alone and runs in well under a second:
#
#   cmake --build build --config Release --target MelodyTests
#   ./build/MelodyTests_artefacts/Release/MelodyTests.exe
juce_add_console_app(MelodyTests PRODUCT_NAME "MelodyTests")
juce_generate_juce_header(MelodyTests)

target_sources(MelodyTests PRIVATE
    Tests/MelodyTests.cpp
    Source/Model/Melody.cpp
    Source/Model/MidiIO.cpp
    Source/UI/Beaming.cpp
)

target_compile_definitions(MelodyTests PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_STANDALONE_APPLICATION=1
)

target_link_libraries(MelodyTests PRIVATE
    juce::juce_core
    juce::juce_audio_basics
    juce::juce_data_structures
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags
)

#==============================================================================
# Tests for the rehearsal engine. It deliberately takes plain values rather than
# the analyser's snapshot struct, so it links without any of the audio side.
juce_add_console_app(RehearsalTests PRODUCT_NAME "RehearsalTests")
juce_generate_juce_header(RehearsalTests)

target_sources(RehearsalTests PRIVATE
    Tests/RehearsalTests.cpp
    Source/Practice/RehearsalEngine.cpp
)

target_compile_definitions(RehearsalTests PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_STANDALONE_APPLICATION=1
)

target_link_libraries(RehearsalTests PRIVATE
    juce::juce_core
    juce::juce_data_structures
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags
)

#==============================================================================
# Property tests for the exercise generator: musical invariants asserted over a
# few thousand exercises, since the interesting failures are the rare seeds.
juce_add_console_app(ExerciseTests PRODUCT_NAME "ExerciseTests")
juce_generate_juce_header(ExerciseTests)

target_sources(ExerciseTests PRIVATE
    Tests/ExerciseTests.cpp
    Source/Practice/ExerciseGenerator.cpp
    Source/Practice/CorpusData.cpp
    Source/Model/Melody.cpp
    Source/Theory/MusicTheory.cpp
)

target_compile_definitions(ExerciseTests PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_STANDALONE_APPLICATION=1
)

target_link_libraries(ExerciseTests PRIVATE
    juce::juce_core
    juce::juce_data_structures
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags
)

#==============================================================================
# Tests for the PDF export. Needs the GUI modules and the embedded font, since
# it renders the score for real - the point is to prove the page is not blank.
juce_add_console_app(ExportTests PRODUCT_NAME "ExportTests")
juce_generate_juce_header(ExportTests)

target_sources(ExportTests PRIVATE
    Tests/ExportTests.cpp
    Source/Export/ScorePdf.cpp
    Source/UI/MelodyStaffComponent.cpp
    Source/UI/StaffLayout.cpp
    Source/UI/MusicFont.cpp
    Source/UI/Beaming.cpp
    Source/Model/Melody.cpp
    Source/Theory/MusicTheory.cpp
)

target_compile_definitions(ExportTests PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_STANDALONE_APPLICATION=1
)

target_link_libraries(ExportTests PRIVATE
    MidiCoachData
    juce::juce_core
    juce::juce_data_structures
    juce::juce_graphics
    juce::juce_gui_basics
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags
)
