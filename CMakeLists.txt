cmake_minimum_required(VERSION 3.0)
project(libBoo)

if(WIN32)
  list(APPEND PLAT_SRCS 
       lib/win/WGLContext.cpp include/win/WGLContext.hpp
       lib/inputdev/HIDListenerWinUSB.cpp
       lib/inputdev/HIDDeviceWinUSB.cpp)
elseif(APPLE)
  list(APPEND PLAT_SRCS 
       lib/mac/CGLCocoaView.mm
       lib/mac/CGLContext.cpp include/mac/CGLContext.hpp
       lib/inputdev/HIDListenerIOKit.cpp
       lib/inputdev/HIDDeviceIOKit.cpp)
else()
  list(APPEND PLAT_SRCS 
       lib/x11/GLXContext.cpp include/x11/GLXContext.hpp
       lib/inputdev/HIDListenerUdev.cpp
       lib/inputdev/HIDDeviceUdev.cpp)
endif()

include_directories(include)

add_library(Boo
            lib/inputdev/CafeProPad.cpp include/inputdev/CafeProPad.hpp
            lib/inputdev/RevolutionPad.cpp include/inputdev/RevolutionPad.hpp
            lib/inputdev/DolphinSmashAdapter.cpp include/inputdev/DolphinSmashAdapter.hpp
            lib/inputdev/DualshockPad.cpp include/inputdev/DualshockPad.hpp
            lib/inputdev/GenericPad.cpp include/inputdev/GenericPad.hpp
            lib/inputdev/DeviceBase.cpp include/inputdev/DeviceBase.hpp
            lib/inputdev/DeviceSignature.cpp include/inputdev/DeviceSignature.hpp
            include/inputdev/IHIDListener.hpp
            lib/inputdev/IHIDDevice.hpp
            lib/InputRelay.cpp include/InputRelay.hpp
            lib/InputDeferredRelay.cpp include/InputDeferredRelay.hpp
            lib/RetraceWaiter.cpp include/IRetraceWaiter.hpp
            lib/Surface.cpp include/Surface.hpp include/ISurface.hpp
            include/IInputWaiter.hpp
            include/IGraphicsContext.hpp
            include/boo.hpp
            ${PLAT_SRCS})