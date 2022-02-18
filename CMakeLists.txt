cmake_minimum_required(VERSION 3.10 FATAL_ERROR) # because of c++17
project(boo)
cmake_policy(SET CMP0074 NEW)

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

set(CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}")

if (NOT TARGET logvisor)
  add_subdirectory(logvisor)
endif()
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
#  lib/graphicsdev/Common.cpp
#  lib/graphicsdev/Common.hpp
  lib/inputdev/DeviceBase.cpp include/boo/inputdev/DeviceBase.hpp
  lib/inputdev/CafeProPad.cpp include/boo/inputdev/CafeProPad.hpp
  lib/inputdev/RevolutionPad.cpp include/boo/inputdev/RevolutionPad.hpp
  lib/inputdev/DolphinSmashAdapter.cpp include/boo/inputdev/DolphinSmashAdapter.hpp
  lib/inputdev/NintendoPowerA.cpp include/boo/inputdev/NintendoPowerA.hpp
  lib/inputdev/DualshockPad.cpp include/boo/inputdev/DualshockPad.hpp
  lib/inputdev/GenericPad.cpp include/boo/inputdev/GenericPad.hpp
  lib/inputdev/DeviceSignature.cpp include/boo/inputdev/DeviceSignature.hpp
  lib/inputdev/DeviceFinder.cpp include/boo/inputdev/DeviceFinder.hpp
  lib/inputdev/HIDParser.cpp include/boo/inputdev/HIDParser.hpp
  lib/inputdev/IHIDDevice.hpp
#  include/boo/IGraphicsContext.hpp
  include/boo/audiodev/IAudioSubmix.hpp
  include/boo/audiodev/IAudioVoice.hpp
  include/boo/audiodev/IAudioVoiceEngine.hpp
  include/boo/audiodev/IMIDIPort.hpp
  include/boo/audiodev/IMIDIReader.hpp
  include/boo/audiodev/MIDIDecoder.hpp
  include/boo/audiodev/MIDIEncoder.hpp
#  include/boo/graphicsdev/IGraphicsDataFactory.hpp
#  include/boo/graphicsdev/IGraphicsCommandQueue.hpp
  include/boo/inputdev/IHIDListener.hpp
  include/boo/inputdev/XInputPad.hpp
  include/boo/boo.hpp
  include/boo/BooObject.hpp
  include/boo/DeferredWindowEvents.hpp
  include/boo/IApplication.hpp
  include/boo/IWindow.hpp
  include/boo/System.hpp
  include/boo/ThreadLocalPtr.hpp
  InputDeviceClasses.cpp
)

if (NOT MSVC)
  target_compile_options(boo PRIVATE -Wno-narrowing)
endif()

option(BOO_GRAPHICS_DEBUG_GROUPS "Enable Debug Groups for labeling graphics passes within backend API." OFF)
if (BOO_GRAPHICS_DEBUG_GROUPS)
  message(STATUS "Enabling graphics debug groups")
  target_compile_definitions(boo PUBLIC -DBOO_GRAPHICS_DEBUG_GROUPS=1)
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

#if(NOT GEKKO AND NOT CAFE AND NOT WINDOWS_STORE AND NOT NX)
#  add_library(glew lib/graphicsdev/glew.c)
#  # For some reason, clang takes forever if glew.c is not built with -Os
#  if(CMAKE_C_COMPILER_ID STREQUAL "Clang" OR CMAKE_C_COMPILER_ID STREQUAL "AppleClang")
#    set_source_files_properties(lib/graphicsdev/glew.c PROPERTIES COMPILE_FLAGS -Os)
#  endif()
#  target_include_directories(glew PUBLIC include/boo/graphicsdev)
#  target_compile_definitions(glew PUBLIC -DGLEW_NO_GLU=1)
#endif()
#
#if(NOT GEKKO AND NOT CAFE AND NOT WINDOWS_STORE AND NOT NX AND NOT APPLE)
#  target_sources(boo PRIVATE lib/graphicsdev/GL.cpp)
#  target_compile_definitions(boo PUBLIC -DBOO_HAS_GL=1)
#  target_link_libraries(boo PUBLIC glew)
#
#  target_sources(boo PRIVATE
#    include/boo/graphicsdev/GL.hpp
#    include/boo/graphicsdev/GLSLMacros.hpp
#    include/boo/graphicsdev/Vulkan.hpp
#    include/boo/graphicsdev/VulkanDispatchTable.hpp
#  )
#endif()

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
    lib/inputdev/HIDDeviceUWP.cpp
    lib/inputdev/HIDListenerUWP.cpp
#    lib/graphicsdev/D3D11.cpp
#    lib/graphicsdev/D3D12.cpp
#    lib/win/ApplicationUWP.cpp
#    lib/win/UWPCommon.hpp
#    lib/win/WinCommon.hpp
#    lib/win/WindowUWP.cpp

    include/boo/UWPViewProvider.hpp
#    include/boo/graphicsdev/D3D.hpp
  )

  target_compile_definitions(boo PUBLIC
    -DUNICODE
    -D_UNICODE
  )

  target_link_libraries(boo PUBLIC
    Hid
    Imm32
    opengl32
    Setupapi
    Shlwapi
    Winmm
    Winusb
    Xinput
  )
elseif(WIN32)
#  unset(VULKAN_SDK_DIRS CACHE)
#  get_filename_component(VULKAN_SDK_DIRS "[HKEY_LOCAL_MACHINE\\SOFTWARE\\LunarG\\VulkanSDK;VK_SDK_PATHs]" ABSOLUTE CACHE)
#  if (NOT "${VULKAN_SDK_DIRS}" STREQUAL "/registry")
#    message(STATUS "Enabling Vulkan support")
#    list(GET VULKAN_SDK_DIRS 0 VULKAN_SDK_DIR)
#    target_include_directories(boo PUBLIC "${VULKAN_SDK_DIR}/Include")
#    target_compile_definitions(boo PUBLIC
#      -DBOO_HAS_VULKAN=1
#      -DVK_USE_PLATFORM_WIN32_KHR=1
#    )
#    target_sources(boo PRIVATE
#      lib/graphicsdev/Vulkan.cpp
#      lib/graphicsdev/VulkanDispatchTable.cpp
#    )
#    set(BOO_ENABLE_VULKAN_IN_OPTICK TRUE)
#  endif()

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
#    lib/graphicsdev/D3D11.cpp
    lib/inputdev/HIDListenerWinUSB.cpp
    lib/inputdev/HIDDeviceWinUSB.cpp
    lib/win/ApplicationWin32.cpp
    lib/win/WindowWin32.cpp
    lib/win/WinCommon.hpp
    lib/win/Win32Common.hpp

#    include/boo/graphicsdev/D3D.hpp
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
    lib/inputdev/HIDListenerIOKit.cpp
    lib/inputdev/HIDDeviceIOKit.cpp
#    lib/mac/ApplicationCocoa.mm
#    lib/mac/WindowCocoa.mm
#    lib/mac/CocoaCommon.hpp
#    lib/graphicsdev/Metal.mm

    lib/CFPointer.hpp
    lib/inputdev/IOKitPointer.hpp
#    include/boo/graphicsdev/Metal.hpp
  )

  set_source_files_properties(
#    lib/mac/ApplicationCocoa.mm
#    lib/mac/WindowCocoa.mm
#    lib/graphicsdev/Metal.mm
    PROPERTIES COMPILE_FLAGS -fobjc-arc
  )

  find_library(APPKIT_LIBRARY AppKit)
  find_library(IOKIT_LIBRARY IOKit)
  unset(BOO_HAS_METAL CACHE)
  if (NOT CMAKE_OSX_DEPLOYMENT_TARGET OR CMAKE_OSX_DEPLOYMENT_TARGET VERSION_GREATER 10.11)
    set(BOO_HAS_METAL ON CACHE BOOL "Metal is available in this OS X version" FORCE)
    find_library(METAL_LIBRARY Metal)
    target_compile_definitions(boo PUBLIC -DBOO_HAS_METAL=1)
  else()
    set(METAL_LIBRARY "")
  endif()
  find_library(QUARTZCORE_LIBRARY QuartzCore)
  find_library(COREVIDEO_LIBRARY CoreVideo)
  find_library(AUDIOTOOLBOX_LIBRARY AudioToolbox)
  find_library(COREAUDIO_LIBRARY CoreAudio)
  find_library(COREMIDI_LIBRARY CoreMIDI)

  target_link_libraries(boo PUBLIC
    ${APPKIT_LIBRARY}
    ${AUDIOTOOLBOX_LIBRARY}
    ${COREAUDIO_LIBRARY}
    ${COREMIDI_LIBRARY}
    ${COREVIDEO_LIBRARY}
    ${IOKIT_LIBRARY}
    ${METAL_LIBRARY}
    ${QUARTZCORE_LIBRARY}
  )
else(NOT GEKKO)
  target_sources(boo PRIVATE
    lib/audiodev/LinuxMidi.hpp
#    lib/graphicsdev/GL.cpp
#    lib/graphicsdev/GLX.cpp
#    lib/x11/XlibCommon.hpp
#    lib/x11/ApplicationUnix.cpp
#    lib/x11/ApplicationWayland.hpp
#    lib/x11/ApplicationXlib.hpp
#    lib/x11/WindowWayland.cpp
#    lib/x11/WindowXlib.cpp
  )

  find_package(PkgConfig)
  if(PKG_CONFIG_FOUND)
    pkg_check_modules(dbus_pkg QUIET libdbus dbus-1)
  endif()

  find_path(DBUS_INCLUDE_DIR
    NAMES
      dbus/dbus.h

    HINTS
      ${dbus_pkg_INCLUDE_DIRS}

    PATH_SUFFIXES
      include/
      include/dbus-1.0/
      dbus-1.0/
  )

  find_path(DBUS_ARCH_INCLUDE_DIR
    NAMES
      dbus/dbus-arch-deps.h

    HINTS
      ${dbus_pkg_INCLUDE_DIRS}

    PATHS
      # TODO use CMAKE_SYSTEM_PROCESSOR or similar?
      /usr/lib/dbus-1.0/include
      /usr/lib64/dbus-1.0/include
      /usr/local/lib/dbus-1.0/include
      /usr/lib/x86_64-linux-gnu/dbus-1.0/include
      /usr/lib/aarch64-linux-gnu/dbus-1.0/include

    PATH_SUFFIXES
      dbus-1.0/include/
  )

  find_library(DBUS_LIBRARY
    NAMES
      dbus
      dbus-1

    HINTS
      ${dbus_pkg_LIBRARY_DIRS}

    PATH_SUFFIXES
      lib
      lib32
      lib64
  )

  find_path(PULSEAUDIO_INCLUDE_DIR
    NAMES pulse/pulseaudio.h
  )
  if(PULSEAUDIO_INCLUDE_DIR-NOTFOUND)
    message(FATAL_ERROR "Unix build of boo requires pulseaudio")
  endif()

  target_sources(boo PRIVATE lib/audiodev/PulseAudio.cpp)
  target_link_libraries(boo PUBLIC pulse)

  if(DBUS_INCLUDE_DIR-NOTFOUND)
    message(FATAL_ERROR "Unix build of boo requires dbus")
  endif()

  target_include_directories(boo PRIVATE
    ${DBUS_ARCH_INCLUDE_DIR}
    ${DBUS_INCLUDE_DIR}
  )
  target_link_libraries(boo
    PUBLIC
      asound
      ${DBUS_LIBRARY}
      GL
      pthread
      X11
      Xi
      Xrandr
  )

  if(NOT ${CMAKE_SYSTEM_NAME} MATCHES "FreeBSD")
#    find_path(VULKAN_INCLUDE_DIR
#      NAMES vulkan/vulkan.h
#    )
#    if(VULKAN_INCLUDE_DIR)
#      message(STATUS "Enabling Vulkan support")
#      target_sources(boo PRIVATE
#        lib/graphicsdev/Vulkan.cpp
#        lib/graphicsdev/VulkanDispatchTable.cpp
#      )
#      target_compile_definitions(boo
#        PUBLIC
#          -DBOO_HAS_VULKAN=1
#          -DVK_USE_PLATFORM_XCB_KHR=1
#      )
#      set(BOO_ENABLE_VULKAN_IN_OPTICK TRUE)
#    endif()
    target_sources(boo PRIVATE
      ${AudioMatrix_SRC}
      lib/inputdev/HIDDeviceUdev.cpp
      lib/inputdev/HIDListenerUdev.cpp
    )
    target_link_libraries(boo
      PUBLIC
        dl
        xcb
        X11-xcb
        udev
    )
  else()
    target_sources(boo PRIVATE
      ${AudioMatrix_SRC}
      lib/inputdev/HIDDeviceBSD.cpp
      lib/inputdev/HIDListenerBSD.cpp
    )
    target_link_libraries(boo
      PUBLIC
        execinfo
    )
  endif()

endif()

if(NOT NX)
  # Empty link args for boo's use
  function(glslang_set_link_args TARGET)
  endfunction(glslang_set_link_args)

  # Disable PCH for now
  function(glslang_pch SRCS PCHCPP)
  endfunction(glslang_pch)

  set(ENABLE_SPVREMAPPER On)
  add_definitions("-DENABLE_OPT=0")
  add_subdirectory(glslang/glslang)
  add_subdirectory(glslang/OGLCompilersDLL)
  add_subdirectory(glslang/SPIRV)
  add_subdirectory(glslang/StandAlone)

  if (NOT MSVC)
    target_compile_options(glslang PRIVATE -Wno-implicit-fallthrough -Wno-strict-aliasing)
    target_compile_options(SPIRV PRIVATE -Wno-implicit-fallthrough -Wno-strict-aliasing)
  endif()

  target_include_directories(glslang-default-resource-limits
      PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/glslang
  )

  target_link_libraries(boo
    PUBLIC
      glslang
      glslang-default-resource-limits
      OGLCompiler
      OSDependent
      soxr
      SPIRV
  )
endif()

# Include Optick and change the default value of some options
option(OPTICK_ENABLED "Enable profiling with Optick" OFF)
set(OPTICK_USE_VULKAN ${BOO_ENABLE_VULKAN_IN_OPTICK} CACHE BOOL "Built-in support for Vulkan" FORCE)
set(OPTICK_INSTALL_TARGETS OFF CACHE BOOL "Should optick be installed? Set to OFF if you use add_subdirectory to include Optick." FORCE)
add_subdirectory(optick)

target_link_libraries(boo PUBLIC logvisor OptickCore)
target_include_directories(boo
  PUBLIC
    include

  PRIVATE
    glslang
    soxr/src
    ${CMAKE_CURRENT_SOURCE_DIR}
)

if(COMMAND add_sanitizers)
  add_sanitizers(boo)
endif()

add_subdirectory(test)

if(WINDOWS_STORE)
  set_property(TARGET boo booTest PROPERTY VS_WINRT_COMPONENT TRUE)
endif()
