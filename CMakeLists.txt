cmake_minimum_required(VERSION 3.10 FATAL_ERROR) # because of c++17
project(boo)
cmake_policy(SET CMP0074 NEW)

if (CMAKE_CURRENT_SOURCE_DIR STREQUAL CMAKE_SOURCE_DIR)
if (MSVC)
  # Shaddup MSVC
  add_compile_definitions(UNICODE=1 _UNICODE=1 __SSE__=1
      _CRT_SECURE_NO_WARNINGS=1 D_SCL_SECURE_NO_WARNINGS=1
      _SCL_SECURE_NO_DEPRECATE=1 _CRT_NONSTDC_NO_WARNINGS=1
      _ENABLE_EXTENDED_ALIGNED_STORAGE=1 NOMINMAX=1)
  add_compile_options(/IGNORE:4221
      $<$<OR:$<COMPILE_LANGUAGE:C>,$<COMPILE_LANGUAGE:CXX>>:/wd4018>
      $<$<OR:$<COMPILE_LANGUAGE:C>,$<COMPILE_LANGUAGE:CXX>>:/wd4800>
      $<$<OR:$<COMPILE_LANGUAGE:C>,$<COMPILE_LANGUAGE:CXX>>:/wd4005>
      $<$<OR:$<COMPILE_LANGUAGE:C>,$<COMPILE_LANGUAGE:CXX>>:/wd4311>
      $<$<OR:$<COMPILE_LANGUAGE:C>,$<COMPILE_LANGUAGE:CXX>>:/wd4068>
      $<$<OR:$<COMPILE_LANGUAGE:C>,$<COMPILE_LANGUAGE:CXX>>:/wd4267>
      $<$<OR:$<COMPILE_LANGUAGE:C>,$<COMPILE_LANGUAGE:CXX>>:/wd4244>
      $<$<OR:$<COMPILE_LANGUAGE:C>,$<COMPILE_LANGUAGE:CXX>>:/wd4200>
      $<$<OR:$<COMPILE_LANGUAGE:C>,$<COMPILE_LANGUAGE:CXX>>:/wd4305>
      $<$<OR:$<COMPILE_LANGUAGE:C>,$<COMPILE_LANGUAGE:CXX>>:/wd4067>
      $<$<OR:$<COMPILE_LANGUAGE:C>,$<COMPILE_LANGUAGE:CXX>>:/wd4146>
      $<$<OR:$<COMPILE_LANGUAGE:C>,$<COMPILE_LANGUAGE:CXX>>:/wd4309>
      $<$<OR:$<COMPILE_LANGUAGE:C>,$<COMPILE_LANGUAGE:CXX>>:/wd4805>
      ${VS_OPTIONS})

  string(REPLACE "/GR " "" CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")
  string(REPLACE " /EHsc" "" CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")
  add_compile_options(
      # Disable exceptions
      $<$<COMPILE_LANGUAGE:CXX>:/EHsc->

      # Disable RTTI
      $<$<COMPILE_LANGUAGE:CXX>:/GR->

      # Enforce various standards compliant behavior.
      $<$<COMPILE_LANGUAGE:CXX>:/permissive->

      # Enable standard volatile semantics.
      $<$<COMPILE_LANGUAGE:CXX>:/volatile:iso>

      # Reports the proper value for the __cplusplus preprocessor macro.
      $<$<COMPILE_LANGUAGE:CXX>:/Zc:__cplusplus>

      # Use latest C++ standard.
      $<$<COMPILE_LANGUAGE:CXX>:/std:c++latest>
  )

  if ("${CMAKE_CXX_COMPILER_ID}" STREQUAL "MSVC")
    # Flags for MSVC (not clang-cl)
    add_compile_options(
        # Allow constexpr variables to have explicit external linkage.
        $<$<COMPILE_LANGUAGE:CXX>:/Zc:externConstexpr>

        # Assume that new throws exceptions, allowing better code generation.
        $<$<COMPILE_LANGUAGE:CXX>:/Zc:throwingNew>

        # Link-time Code Generation for Release builds
        $<$<CONFIG:Release>:/GL>
    )

    # Link-time Code Generation for Release builds
    set(CMAKE_STATIC_LINKER_FLAGS_RELEASE "/LTCG")
    set(CMAKE_EXE_LINKER_FLAGS_RELEASE "/RELEASE /LTCG /OPT:REF /OPT:ICF /INCREMENTAL:NO")
    set(CMAKE_EXE_LINKER_FLAGS_RELWITHDEBINFO "/DEBUG /RELEASE /OPT:REF /OPT:ICF /INCREMENTAL:NO /DEBUGTYPE:cv,fixup")
  endif ()
else ()
  set(CMAKE_CXX_STANDARD 20)
  set(CMAKE_CXX_STANDARD_REQUIRED ON)
endif ()
endif ()

set(CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}")

add_subdirectory(soxr/src)

add_library(boo
  lib/audiodev/Common.hpp
  lib/audiodev/AudioMatrix.hpp
  lib/audiodev/AudioSubmix.cpp
  lib/audiodev/AudioSubmix.hpp
  lib/audiodev/AudioVoice.cpp
  lib/audiodev/AudioVoice.hpp
  lib/audiodev/AudioVoiceEngine.cpp
  lib/audiodev/AudioVoiceEngine.hpp
  lib/audiodev/LtRtProcessing.cpp
  lib/audiodev/LtRtProcessing.hpp
  lib/audiodev/MIDICommon.cpp
  lib/audiodev/MIDICommon.hpp
  lib/audiodev/MIDIDecoder.cpp
  lib/audiodev/MIDIEncoder.cpp
  lib/audiodev/WAVOut.cpp
  lib/Common.hpp
  include/boo/audiodev/IAudioSubmix.hpp
  include/boo/audiodev/IAudioVoice.hpp
  include/boo/audiodev/IAudioVoiceEngine.hpp
  include/boo/audiodev/IMIDIPort.hpp
  include/boo/audiodev/IMIDIReader.hpp
  include/boo/audiodev/MIDIDecoder.hpp
  include/boo/audiodev/MIDIEncoder.hpp
  include/boo/BooObject.hpp
  include/boo/System.hpp
)

if (NOT MSVC)
  target_compile_options(boo PRIVATE -Wno-narrowing)
endif()

find_package(IPP)
if (IPP_FOUND)
  target_compile_definitions(boo PUBLIC -DINTEL_IPP=1)
  target_include_directories(boo PUBLIC ${IPP_INCLUDE_DIRS})
  target_link_libraries(boo PUBLIC ${IPP_LIBRARIES})
  message(STATUS "Building with IPP support")
else()
  message(WARNING "IPP not found; skipping support")
endif ()

set(_EXTRA_INCLUDES ${CMAKE_CURRENT_SOURCE_DIR}/include ${CMAKE_CURRENT_SOURCE_DIR})
target_include_directories(boo PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${CMAKE_CURRENT_SOURCE_DIR}/include
)

set(AudioMatrix_SRC lib/audiodev/AudioMatrix.cpp)
if(CMAKE_SYSTEM_PROCESSOR STREQUAL x86_64
    OR CMAKE_SYSTEM_PROCESSOR STREQUAL AMD64
    OR CMAKE_SYSTEM_PROCESSOR STREQUAL arm64
    OR CMAKE_SYSTEM_PROCESSOR STREQUAL ARM64)
  set(AudioMatrix_SRC lib/audiodev/AudioMatrixSSE.cpp)
endif()

if(WINDOWS_STORE)
  target_sources(boo PRIVATE
    ${AudioMatrix_SRC}
    lib/audiodev/WASAPI.cpp
    include/boo/UWPViewProvider.hpp
  )

  target_compile_definitions(boo PUBLIC
    -DUNICODE
    -D_UNICODE
  )

  target_link_libraries(boo PUBLIC
    Setupapi
    Shlwapi
    Winmm
  )
elseif(WIN32)
  find_file(TE_VIRTUAL_MIDI_H teVirtualMIDI.h PATHS
            "$ENV{PROGRAMFILES\(X86\)}/Tobias Erichsen/teVirtualMIDISDK/C-Binding")
  find_file(TE_VIRTUAL_MIDI_H teVirtualMIDI.h
    PATHS
      "$ENV{PROGRAMFILES\(X86\)}/Tobias Erichsen/teVirtualMIDISDK/C-Binding"
  )
  if (NO AND TE_VIRTUAL_MIDI_H)
    message(STATUS "Enabling teVirtualMIDI")
    get_filename_component(TE_VIRTUAL_MIDI_DIR ${TE_VIRTUAL_MIDI_H} DIRECTORY)
    target_include_directories(boo PRIVATE ${TE_VIRTUAL_MIDI_DIR})
    add_definitions("-DTE_VIRTUAL_MIDI=1")
  endif()

  target_sources(boo PRIVATE
    ${AudioMatrix_SRC}
    lib/audiodev/WASAPI.cpp
  )

  target_compile_definitions(boo PUBLIC
    -DUNICODE
    -D_UNICODE
  )

  target_link_libraries(boo PUBLIC
    Hid
    Imm32
    Setupapi
    Shlwapi
    Winmm
    Winusb opengl32
    Xinput
  )
elseif(APPLE)
  target_sources(boo PRIVATE
    lib/audiodev/AQS.cpp
    ${AudioMatrix_SRC}
    lib/CFPointer.hpp
  )

  set_source_files_properties(
    PROPERTIES COMPILE_FLAGS -fobjc-arc
  )

  find_library(APPKIT_LIBRARY AppKit)
  unset(BOO_HAS_METAL CACHE)
  if (NOT CMAKE_OSX_DEPLOYMENT_TARGET OR CMAKE_OSX_DEPLOYMENT_TARGET VERSION_GREATER 10.11)
    set(BOO_HAS_METAL ON CACHE BOOL "Metal is available in this OS X version" FORCE)
    find_library(METAL_LIBRARY Metal)
    target_compile_definitions(boo PUBLIC -DBOO_HAS_METAL=1)
  else()
    set(METAL_LIBRARY "")
  endif()
  find_library(AUDIOTOOLBOX_LIBRARY AudioToolbox)
  find_library(COREAUDIO_LIBRARY CoreAudio)
  find_library(COREMIDI_LIBRARY CoreMIDI)

  target_link_libraries(boo PUBLIC
    ${APPKIT_LIBRARY}
    ${AUDIOTOOLBOX_LIBRARY}
    ${COREAUDIO_LIBRARY}
    ${COREMIDI_LIBRARY}
    ${COREVIDEO_LIBRARY}
    ${METAL_LIBRARY}
  )
else(NOT GEKKO)
  target_sources(boo PRIVATE
    lib/audiodev/LinuxMidi.hpp
  )

  find_path(PULSEAUDIO_INCLUDE_DIR
    NAMES pulse/pulseaudio.h
  )
  if(PULSEAUDIO_INCLUDE_DIR-NOTFOUND)
    message(FATAL_ERROR "Unix build of boo requires pulseaudio")
  endif()

  target_sources(boo PRIVATE lib/audiodev/PulseAudio.cpp)
  target_link_libraries(boo PUBLIC pulse)

  target_link_libraries(boo
    PUBLIC
      asound
      pthread
  )

  if(NOT ${CMAKE_SYSTEM_NAME} MATCHES "FreeBSD")
    target_sources(boo PRIVATE
      ${AudioMatrix_SRC}
    )
    target_link_libraries(boo
      PUBLIC
        dl
    )
  else()
    target_sources(boo PRIVATE
      ${AudioMatrix_SRC}
    )
    target_link_libraries(boo
      PUBLIC
        execinfo
    )
  endif()

endif()

if(NOT NX)
  target_link_libraries(boo
    PUBLIC
      soxr
  )
endif()

target_link_libraries(boo PUBLIC logvisor OptickCore)
target_include_directories(boo
  PUBLIC
    include

  PRIVATE
    soxr/src
    ${CMAKE_CURRENT_SOURCE_DIR}
)
