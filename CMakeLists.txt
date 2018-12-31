cmake_minimum_required(VERSION 3.10 FATAL_ERROR) # because of c++17
project(boo)
cmake_policy(SET CMP0074 NEW)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if (NOT MSVC)
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-narrowing")
endif()

if (NOT TARGET logvisor)
  add_subdirectory(logvisor)
endif()

add_subdirectory(xxhash)

set(CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}")
find_package(IPP)
if (IPP_FOUND)
  list(APPEND _BOO_SYS_DEFINES -DINTEL_IPP=1)
  include_directories(${IPP_INCLUDE_DIRS})
  list(APPEND _BOO_SYS_INCLUDES ${IPP_INCLUDE_DIRS})
  list(APPEND _BOO_SYS_LIBS ${IPP_LIBRARIES})
  message(STATUS "Building with IPP support")
else()
  message(WARNING "IPP not found; skipping support")
endif ()

add_subdirectory(soxr/src)

set(BOO_INCLUDE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/include CACHE PATH "boo include path" FORCE)

include_directories(include ${CMAKE_CURRENT_SOURCE_DIR} ${LOGVISOR_INCLUDE_DIR})

add_subdirectory(lib/graphicsdev/nx)
if(TARGET nx_compiler)
  list(APPEND _BOO_SYS_DEFINES -DHECL_NOUVEAU_NX=1)
endif()

if(NOT GEKKO AND NOT CAFE AND NOT WINDOWS_STORE AND NOT NX AND NOT APPLE)
list(APPEND PLAT_SRCS
    lib/graphicsdev/GL.cpp
    lib/graphicsdev/glew.c)
list(APPEND _BOO_SYS_DEFINES -DBOO_HAS_GL=1 -DGLEW_NO_GLU=1)

list(APPEND PLAT_HDRS
     include/boo/graphicsdev/GLSLMacros.hpp
     include/boo/graphicsdev/GL.hpp
     include/boo/graphicsdev/Vulkan.hpp
     include/boo/graphicsdev/VulkanDispatchTable.hpp)

add_library(glew lib/graphicsdev/glew.c)
endif()

if(WINDOWS_STORE)
  list(APPEND PLAT_SRCS
       lib/win/ApplicationUWP.cpp
       lib/win/WindowUWP.cpp
       lib/win/WinCommon.hpp
       lib/win/UWPCommon.hpp
       lib/inputdev/HIDListenerUWP.cpp
       lib/inputdev/HIDDeviceUWP.cpp
       lib/graphicsdev/D3D11.cpp
       lib/graphicsdev/D3D12.cpp
       lib/audiodev/WASAPI.cpp
       lib/audiodev/AudioMatrixSSE.cpp)

  list(APPEND PLAT_HDRS
        include/boo/UWPViewProvider.hpp
        include/boo/graphicsdev/D3D.hpp)

  list(APPEND _BOO_SYS_DEFINES -DUNICODE -D_UNICODE)

  list(APPEND _BOO_SYS_LIBS Winusb opengl32 Setupapi Imm32 Winmm Shlwapi Hid Xinput)

elseif(WIN32)
  unset(VULKAN_SDK_DIRS CACHE)
  get_filename_component(VULKAN_SDK_DIRS "[HKEY_LOCAL_MACHINE\\SOFTWARE\\LunarG\\VulkanSDK;VK_SDK_PATHs]" ABSOLUTE CACHE)
  if (NOT ${VULKAN_SDK_DIRS} STREQUAL "/registry")
    message(STATUS "Enabling Vulkan support")
    list(GET VULKAN_SDK_DIRS 0 VULKAN_SDK_DIR)
    include_directories("${VULKAN_SDK_DIR}/Include")
    list(APPEND _BOO_SYS_DEFINES -DBOO_HAS_VULKAN=1 -DVK_USE_PLATFORM_WIN32_KHR=1)
    list(APPEND _BOO_SYS_INCLUDES "${VULKAN_SDK_DIR}/Include")
    list(APPEND PLAT_SRCS lib/graphicsdev/Vulkan.cpp
         lib/graphicsdev/VulkanDispatchTable.cpp)
  endif()

  find_file(TE_VIRTUAL_MIDI_H teVirtualMIDI.h PATHS
            "$ENV{PROGRAMFILES\(X86\)}/Tobias Erichsen/teVirtualMIDISDK/C-Binding")
  if (NO AND TE_VIRTUAL_MIDI_H)
    message(STATUS "Enabling teVirtualMIDI")
    get_filename_component(TE_VIRTUAL_MIDI_DIR ${TE_VIRTUAL_MIDI_H} DIRECTORY)
    include_directories(${TE_VIRTUAL_MIDI_DIR})
    add_definitions("-DTE_VIRTUAL_MIDI=1")
  endif()

  list(APPEND PLAT_SRCS
       lib/win/ApplicationWin32.cpp
       lib/win/WindowWin32.cpp
       lib/win/WinCommon.hpp
       lib/win/Win32Common.hpp
       lib/inputdev/HIDListenerWinUSB.cpp
       lib/inputdev/HIDDeviceWinUSB.cpp
       lib/graphicsdev/D3D11.cpp
       lib/audiodev/WASAPI.cpp
       lib/audiodev/AudioMatrixSSE.cpp)

  list(APPEND PLAT_HDRS
        include/boo/graphicsdev/D3D.hpp)

  list(APPEND _BOO_SYS_DEFINES -DUNICODE -D_UNICODE)

  list(APPEND _BOO_SYS_LIBS Winusb opengl32 Setupapi Imm32 Winmm Shlwapi Hid Xinput)

elseif(APPLE)
  list(APPEND PLAT_SRCS
       lib/mac/ApplicationCocoa.mm
       lib/mac/WindowCocoa.mm
       lib/mac/CocoaCommon.hpp
       lib/inputdev/HIDListenerIOKit.cpp
       lib/inputdev/HIDDeviceIOKit.cpp
       lib/graphicsdev/Metal.mm
       lib/audiodev/AQS.cpp
       lib/audiodev/AudioMatrixSSE.cpp)
  set_source_files_properties(lib/mac/ApplicationCocoa.mm
                              lib/mac/WindowCocoa.mm
                              lib/graphicsdev/Metal.mm
                              PROPERTIES COMPILE_FLAGS -fobjc-arc)

  list(APPEND PLAT_HDRS
       lib/CFPointer.hpp
       include/boo/graphicsdev/Metal.hpp
       lib/inputdev/IOKitPointer.hpp)

  find_library(APPKIT_LIBRARY AppKit)
  find_library(IOKIT_LIBRARY IOKit)
  unset(BOO_HAS_METAL CACHE)
  if (NOT CMAKE_OSX_DEPLOYMENT_TARGET OR CMAKE_OSX_DEPLOYMENT_TARGET VERSION_GREATER 10.11)
    set(BOO_HAS_METAL ON CACHE BOOL "Metal is available in this OS X version" FORCE)
    find_library(METAL_LIBRARY Metal)
    list(APPEND _BOO_SYS_DEFINES -DBOO_HAS_METAL=1)
  else()
    set(METAL_LIBRARY "")
  endif()
  find_library(QUARTZCORE_LIBRARY QuartzCore)
  find_library(COREVIDEO_LIBRARY CoreVideo)
  find_library(AUDIOTOOLBOX_LIBRARY AudioToolbox)
  find_library(COREAUDIO_LIBRARY CoreAudio)
  find_library(COREMIDI_LIBRARY CoreMIDI)
  list(APPEND _BOO_SYS_LIBS ${APPKIT_LIBRARY} ${IOKIT_LIBRARY} ${METAL_LIBRARY}
                            ${QUARTZCORE_LIBRARY} ${COREVIDEO_LIBRARY} ${AUDIOTOOLBOX_LIBRARY}
                            ${COREAUDIO_LIBRARY} ${COREMIDI_LIBRARY})

  add_library(glew lib/graphicsdev/glew.c)
elseif(NX)
  list(APPEND _BOO_SYS_DEFINES -DBOO_HAS_NX=1)
  list(APPEND PLAT_SRCS
       lib/nx/ApplicationNX.cpp
       lib/nx/WindowNX.cpp
       lib/audiodev/AudioMatrix.cpp
       lib/inputdev/HIDListenerNX.cpp
       lib/inputdev/HIDDeviceNX.cpp)
  list(APPEND _BOO_SYS_LIBS nx_runtime)

else(NOT GEKKO)
  list(APPEND PLAT_SRCS
       lib/x11/XlibCommon.hpp
       lib/x11/ApplicationUnix.cpp
       lib/x11/ApplicationXlib.hpp
       lib/x11/ApplicationWayland.hpp
       lib/x11/WindowXlib.cpp
       lib/x11/WindowWayland.cpp
       lib/graphicsdev/GL.cpp
       lib/graphicsdev/GLX.cpp
       lib/graphicsdev/glew.c
       lib/audiodev/LinuxMidi.hpp)

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
    dbus-1.0/)

  find_path(DBUS_ARCH_INCLUDE_DIR
    NAMES
    dbus/dbus-arch-deps.h
    HINTS
    ${dbus_pkg_INCLUDE_DIRS}
    PATHS
    # TODO use CMAKE_SYSTEM_PROCESSOR or similar?
    /usr/lib/dbus-1.0/include
    /usr/local/lib/dbus-1.0/include
    /usr/lib/x86_64-linux-gnu/dbus-1.0/include/
    PATH_SUFFIXES
    dbus-1.0/include/)

  find_library(DBUS_LIBRARY
    NAMES
    dbus dbus-1
    HINTS
    ${dbus_pkg_LIBRARY_DIRS}
    PATH_SUFFIXES
    lib
    lib32
    lib64)

  find_path(PULSEAUDIO_INCLUDE_DIR
            NAMES pulse/pulseaudio.h)
  if(PULSEAUDIO_INCLUDE_DIR-NOTFOUND)
    message(FATAL_ERROR "Unix build of boo requires pulseaudio")
  endif()

  list(APPEND PLAT_SRCS lib/audiodev/PulseAudio.cpp)
  list(APPEND _BOO_SYS_LIBS pulse)

  if(DBUS_INCLUDE_DIR-NOTFOUND)
    message(FATAL_ERROR "Unix build of boo requires dbus")
  endif()

  include_directories(${DBUS_INCLUDE_DIR} ${DBUS_ARCH_INCLUDE_DIR})
  list(APPEND _BOO_SYS_LIBS X11 Xi Xrandr GL asound ${DBUS_LIBRARY} pthread)

  if(NOT ${CMAKE_SYSTEM_NAME} MATCHES "FreeBSD")
    find_path(VULKAN_INCLUDE_DIR
              NAMES vulkan/vulkan.h)
    if(VULKAN_INCLUDE_DIR)
      message(STATUS "Enabling Vulkan support")
      list(APPEND PLAT_SRCS
          lib/graphicsdev/Vulkan.cpp
          lib/graphicsdev/VulkanDispatchTable.cpp)
      list(APPEND _BOO_SYS_DEFINES -DBOO_HAS_VULKAN=1 -DVK_USE_PLATFORM_XCB_KHR=1)
    endif()
    list(APPEND PLAT_SRCS
         lib/inputdev/HIDListenerUdev.cpp
         lib/inputdev/HIDDeviceUdev.cpp
         lib/audiodev/AudioMatrixSSE.cpp)
    list(APPEND _BOO_SYS_LIBS xcb X11-xcb dl udev)
  else()
    list(APPEND PLAT_SRCS
        lib/inputdev/HIDListenerBSD.cpp
        lib/inputdev/HIDDeviceBSD.cpp
        lib/audiodev/AudioMatrixSSE.cpp)
    list(APPEND _BOO_SYS_LIBS execinfo)
  endif()

endif()

if(NOT NX)
  # For some reason, clang takes forever if glew.c is not built with -Os
  if(CMAKE_C_COMPILER_ID STREQUAL "Clang" OR CMAKE_C_COMPILER_ID STREQUAL "AppleClang")
    set_source_files_properties(lib/graphicsdev/glew.c PROPERTIES COMPILE_FLAGS -Os)
  endif()

  # Empty link args for boo's use
  function(glslang_set_link_args TARGET)
  endfunction(glslang_set_link_args)

  set(ENABLE_SPVREMAPPER On)
  add_definitions("-DENABLE_OPT=0")
  add_subdirectory(glslang/glslang)
  add_subdirectory(glslang/OGLCompilersDLL)
  add_subdirectory(glslang/SPIRV)
  add_subdirectory(glslang/StandAlone)

  target_include_directories(glslang-default-resource-limits
      PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/glslang
  )

  list(APPEND _BOO_SYS_LIBS glslang soxr xxhash OSDependent OGLCompiler SPIRV glslang-default-resource-limits)
endif()

set(BOO_SYS_LIBS ${_BOO_SYS_LIBS} CACHE PATH "boo system libraries" FORCE)
set(BOO_SYS_DEFINES ${_BOO_SYS_DEFINES} CACHE STRING "boo system defines" FORCE)
set(BOO_SYS_INCLUDES ${_BOO_SYS_INCLUDES} CACHE PATH "boo system includes" FORCE)

add_definitions(${_BOO_SYS_DEFINES})
include_directories(include glslang soxr/src)

add_library(boo
            lib/inputdev/DeviceBase.cpp include/boo/inputdev/DeviceBase.hpp
            lib/inputdev/CafeProPad.cpp include/boo/inputdev/CafeProPad.hpp
            lib/inputdev/RevolutionPad.cpp include/boo/inputdev/RevolutionPad.hpp
            lib/inputdev/DolphinSmashAdapter.cpp include/boo/inputdev/DolphinSmashAdapter.hpp
            lib/inputdev/NintendoPowerA.cpp include/boo/inputdev/NintendoPowerA.hpp
            lib/inputdev/DualshockPad.cpp include/boo/inputdev/DualshockPad.hpp
            include/boo/inputdev/XInputPad.hpp
            lib/inputdev/GenericPad.cpp include/boo/inputdev/GenericPad.hpp
            lib/inputdev/DeviceSignature.cpp include/boo/inputdev/DeviceSignature.hpp
            lib/inputdev/DeviceFinder.cpp include/boo/inputdev/DeviceFinder.hpp
            lib/inputdev/HIDParser.cpp include/boo/inputdev/HIDParser.hpp
            lib/inputdev/IHIDDevice.hpp
            lib/audiodev/Common.hpp
            lib/audiodev/WAVOut.cpp
            lib/audiodev/AudioMatrix.hpp
            lib/audiodev/AudioVoiceEngine.hpp
            lib/audiodev/AudioVoiceEngine.cpp
            lib/audiodev/AudioVoice.hpp
            lib/audiodev/AudioVoice.cpp
            lib/audiodev/AudioSubmix.hpp
            lib/audiodev/AudioSubmix.cpp
            lib/audiodev/LtRtProcessing.hpp
            lib/audiodev/LtRtProcessing.cpp
            lib/audiodev/MIDIEncoder.cpp
            lib/audiodev/MIDIDecoder.cpp
            lib/audiodev/MIDICommon.hpp
            lib/audiodev/MIDICommon.cpp
            lib/Common.hpp
            include/boo/inputdev/IHIDListener.hpp
            include/boo/IGraphicsContext.hpp
            include/boo/graphicsdev/IGraphicsDataFactory.hpp
            include/boo/graphicsdev/IGraphicsCommandQueue.hpp
            lib/graphicsdev/Common.hpp
            lib/graphicsdev/Common.cpp
            include/boo/audiodev/IAudioSubmix.hpp
            include/boo/audiodev/IAudioVoice.hpp
            include/boo/audiodev/IMIDIPort.hpp
            include/boo/audiodev/IMIDIReader.hpp
            include/boo/audiodev/MIDIEncoder.hpp
            include/boo/audiodev/MIDIDecoder.hpp
            include/boo/audiodev/IAudioVoiceEngine.hpp
            include/boo/IWindow.hpp
            include/boo/IApplication.hpp
            include/boo/ThreadLocalPtr.hpp
            include/boo/DeferredWindowEvents.hpp
            include/boo/System.hpp
            include/boo/BooObject.hpp
            include/boo/boo.hpp
            InputDeviceClasses.cpp
            ${PLAT_SRCS}
            ${PLAT_HDRS})
if(COMMAND add_sanitizers)
  add_sanitizers(boo)
endif()

add_subdirectory(test)

if(WINDOWS_STORE)
  set_property(TARGET boo booTest PROPERTY VS_WINRT_COMPONENT TRUE)
endif()
