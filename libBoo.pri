!contains(CONFIG,c++11):CONFIG += C++11

HEADERS += \
    $$PWD/include/boo.hpp \
    $$PWD/include/IGraphicsContext.hpp \
    $$PWD/include/ISurface.hpp \
    $$PWD/include/CSurface.hpp \
    $$PWD/include/IRetraceWaiter.hpp \
    $$PWD/include/IInputWaiter.hpp \
    $$PWD/include/CInputRelay.hpp \
    $$PWD/include/CInputDeferredRelay.hpp \
    $$PWD/include/inputdev/CDolphinSmashAdapter.hpp \
    $$PWD/include/inputdev/CRevolutionPad.hpp \
    $$PWD/include/inputdev/CCafeProPad.hpp \
    $$PWD/include/inputdev/CDualshockPad.hpp \
    $$PWD/include/inputdev/CGenericPad.hpp \
    $$PWD/include/inputdev/CDeviceFinder.hpp

unix:!macx:HEADERS += \
    $$PWD/include/x11/CGLXContext.hpp

macx:HEADERS += \
    $$PWD/include/mac/CCGLContext.hpp

win32:HEADERS += \
    $$PWD/include/win/CWGLContext.hpp

SOURCES += \
    $$PWD/src/CSurface.cpp \
    $$PWD/src/CRetraceWaiter.cpp \
    $$PWD/src/CInputRelay.cpp \
    $$PWD/src/CInputDeferredRelay.cpp \
    $$PWD/src/inputdev/CDolphinSmashAdapter.cpp \
    $$PWD/src/inputdev/CRevolutionPad.cpp \
    $$PWD/src/inputdev/CCafeProPad.cpp \
    $$PWD/src/inputdev/CDualshockPad.cpp \
    $$PWD/src/inputdev/CGenericPad.cpp \
    $$PWD/src/inputdev/CDeviceFinder.cpp

unix:!macx:SOURCES += \
    $$PWD/src/x11/CGLXContext.cpp \
    $$PWD/src/inputdev/CHIDDeviceUdev.cpp

macx:SOURCES += \
    $$PWD/src/mac/CCGLContext.cpp

macx:OBJECTIVE_SOURCES += \
    $$PWD/src/mac/CCGLCocoaView.mm \
    $$PWD/src/inputdev/CHIDDeviceIOKit.mm

win32:SOURCES += \
    $$PWD/src/win/CWGLContext.cpp \
    $$PWD/src/inputdev/CHIDDeviceWin32.cpp

INCLUDEPATH += $$PWD/include
