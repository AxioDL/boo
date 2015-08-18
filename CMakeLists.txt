cmake_minimum_required(VERSION 3.0)
project(libBoo)

if (NOT MSVC)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11")
endif()

if(WIN32)
  list(APPEND PLAT_SRCS 
       lib/win/ApplicationWin32.cpp
       lib/win/WindowWin32.cpp
       lib/win/GraphicsContextWin32.cpp
       lib/inputdev/HIDListenerWinUSB.cpp
       lib/inputdev/HIDDeviceWinUSB.cpp)
elseif(APPLE)
  list(APPEND PLAT_SRCS 
       lib/mac/ApplicationCocoa.mm
       lib/mac/WindowCocoa.mm
       lib/mac/GLViewCocoa.mm
       lib/mac/GraphicsContextCocoa.cpp
       lib/inputdev/HIDListenerIOKit.cpp
       lib/inputdev/HIDDeviceIOKit.cpp)
else()
  list(APPEND PLAT_SRCS 
       lib/x11/ApplicationUnix.cpp
       lib/x11/ApplicationXCB.hpp
       lib/x11/ApplicationWayland.hpp
       lib/x11/WindowXCB.cpp
       lib/x11/WindowWayland.cpp
       lib/x11/GraphicsContextXCB.cpp
       lib/x11/GraphicsContextWayland.cpp
       lib/inputdev/HIDListenerUdev.cpp
       lib/inputdev/HIDDeviceUdev.cpp)

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

  if(DBUS_INCLUDE_DIR-NOTFOUND)
    message(FATAL_ERROR "Unix build of libBoo requires dbus")
  endif()

  include_directories(${DBUS_INCLUDE_DIR} ${DBUS_ARCH_INCLUDE_DIR})
  list(APPEND BOO_SYS_LIBS xcb xcb-glx xcb-xinput xcb-xkb xcb-keysyms xkbcommon xkbcommon-x11 ${DBUS_LIBRARY} udev pthread)

endif()

set(BOO_SYS_LIBS ${BOO_SYS_LIBS} CACHE PATH "Boo System Libraries" FORCE)

include_directories(include)

add_library(Boo
            lib/inputdev/CafeProPad.cpp include/boo/inputdev/CafeProPad.hpp
            lib/inputdev/RevolutionPad.cpp include/boo/inputdev/RevolutionPad.hpp
            lib/inputdev/DolphinSmashAdapter.cpp include/boo/inputdev/DolphinSmashAdapter.hpp
            lib/inputdev/DualshockPad.cpp include/boo/inputdev/DualshockPad.hpp
            lib/inputdev/GenericPad.cpp include/boo/inputdev/GenericPad.hpp
            lib/inputdev/DeviceBase.cpp include/boo/inputdev/DeviceBase.hpp
            lib/inputdev/DeviceSignature.cpp include/boo/inputdev/DeviceSignature.hpp
            include/boo/inputdev/IHIDListener.hpp
            lib/inputdev/IHIDDevice.hpp
            include/boo/IGraphicsContext.hpp
            include/boo/IWindow.hpp
            include/boo/IApplication.hpp
            include/boo/boo.hpp
            InputDeviceClasses.cpp
            ${PLAT_SRCS})

add_subdirectory(test)
