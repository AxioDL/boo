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
    $$PWD/include/inputdev/CDeviceFinder.hpp \
    $$PWD/include/inputdev/CDeviceToken.hpp \
    $$PWD/include/inputdev/CDeviceBase.hpp \
    $$PWD/include/inputdev/IHIDListener.hpp \
    $$PWD/src/inputdev/IHIDDevice.hpp \
    $$PWD/include/inputdev/SDeviceSignature.hpp

SOURCES += \
    $$PWD/InputDeviceClasses.cpp \
    $$PWD/src/CSurface.cpp \
    $$PWD/src/CRetraceWaiter.cpp \
    $$PWD/src/CInputRelay.cpp \
    $$PWD/src/CInputDeferredRelay.cpp \
    $$PWD/src/inputdev/CDolphinSmashAdapter.cpp \
    $$PWD/src/inputdev/CRevolutionPad.cpp \
    $$PWD/src/inputdev/CCafeProPad.cpp \
    $$PWD/src/inputdev/CDualshockPad.cpp \
    $$PWD/src/inputdev/CGenericPad.cpp \
    $$PWD/src/inputdev/CDeviceBase.cpp \
    $$PWD/src/inputdev/SDeviceSignature.cpp

unix:!macx {
    HEADERS += \
        $$PWD/include/x11/CGLXContext.hpp

    SOURCES += \
        $$PWD/src/x11/CGLXContext.cpp
}

linux {
    SOURCES += \
        $$PWD/src/inputdev/CHIDListenerUdev.cpp \
        $$PWD/src/inputdev/CHIDDeviceUdev.cpp

    LIBS += -ludev
}

macx {
    HEADERS += \
        $$PWD/include/mac/CCGLContext.hpp

    SOURCES += \
        $$PWD/src/mac/CCGLContext.cpp \
        $$PWD/src/inputdev/CHIDDeviceIOKit.cpp \
        $$PWD/src/inputdev/CHIDListenerIOKit.cpp

    OBJECTIVE_SOURCES += \
        $$PWD/src/mac/CCGLCocoaView.mm
}

win32 {
    HEADERS += \
        $$PWD/include/win/CWGLContext.hpp
    SOURCES += \
        $$PWD/src/win/CWGLContext.cpp \
        $$PWD/src/inputdev/CHIDDeviceWin32.cpp \
        $$PWD/src/inputdev/CHIDListenerWin32.cpp
}

INCLUDEPATH += $$PWD/include

