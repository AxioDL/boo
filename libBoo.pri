HEADERS += \
    $$PWD/include/boo.hpp \
    $$PWD/include/IApplication.hpp \
    $$PWD/include/windowsys/IWindow.hpp \
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
    $$PWD/include/inputdev/SDeviceSignature.hpp \
    $$PWD/include/windowsys/IGFXCommandBuffer.hpp \
    $$PWD/include/graphicsys/IGFXCommandBuffer.hpp \
    $$PWD/include/graphicsys/IGFXContext.hpp \
    $$PWD/include/graphicsys/IGFXPipelineState.hpp \
    $$PWD/include/graphicsys/IGFXTransformSet.hpp \
    $$PWD/include/graphicsys/hecl/CHECLLexer.hpp \
    $$PWD/src/graphicsys/hecl/IHECLBackend.hpp \
    $$PWD/src/graphicsys/hecl/HECLExpressions.hpp \
    $$PWD/include/graphicsys/CGFXVertexLayoutBase.hpp \
    $$PWD/include/graphicsys/hecl/HECLExpressions.hpp \
    $$PWD/include/graphicsys/hecl/IHECLBackend.hpp

SOURCES += \
    $$PWD/InputDeviceClasses.cpp \
    $$PWD/src/inputdev/CDolphinSmashAdapter.cpp \
    $$PWD/src/inputdev/CRevolutionPad.cpp \
    $$PWD/src/inputdev/CCafeProPad.cpp \
    $$PWD/src/inputdev/CDualshockPad.cpp \
    $$PWD/src/inputdev/CGenericPad.cpp \
    $$PWD/src/inputdev/CDeviceBase.cpp \
    $$PWD/src/inputdev/SDeviceSignature.cpp \
    $$PWD/src/graphicsys/hecl/CHECLBackendGLSL.cpp \
    $$PWD/src/graphicsys/hecl/CHECLBackendHLSL.cpp \
    $$PWD/src/graphicsys/hecl/CHECLBackendMetal.cpp \
    $$PWD/src/graphicsys/hecl/CHECLLexer.cpp \
    $$PWD/src/graphicsys/hecl/CHECLBackendOutline.cpp \
    $$PWD/src/graphicsys/hecl/CHECLBackendTEV.cpp

unix:!macx {
    HEADERS += \
        $$PWD/src/CApplicationXCB.hpp \
        $$PWD/src/CApplicationWayland.hpp
    SOURCES += \
        $$PWD/src/CApplicationUnix.cpp \
        $$PWD/src/windowsys/CWindowXCB.cpp \
        $$PWD/src/windowsys/CWindowWayland.cpp \
        $$PWD/src/graphicsys/CGraphicsContextXCB.cpp \
        $$PWD/src/graphicsys/CGraphicsContextWayland.cpp
}

linux {
    SOURCES += \
        $$PWD/src/inputdev/CHIDListenerUdev.cpp \
        $$PWD/src/inputdev/CHIDDeviceUdev.cpp
    LIBS += -ludev
}

macx {
    SOURCES += \
        $$PWD/src/inputdev/CHIDDeviceIOKit.cpp \
        $$PWD/src/inputdev/CHIDListenerIOKit.cpp
    OBJECTIVE_SOURCES += \
        $$PWD/src/CApplicationCocoa.mm \
        $$PWD/src/windowsys/CWindowCocoa.mm \
        $$PWD/src/windowsys/CGraphicsContextCocoa.mm
    LIBS += -framework AppKit
}

win32 {
    SOURCES += \
        $$PWD/src/CApplicationWin32.cpp \
        $$PWD/src/inputdev/CHIDListenerWinUSB.cpp \
        $$PWD/src/inputdev/CHIDDeviceWinUSB.cpp \
        $$PWD/src/windowsys/CWindowWin32.cpp \
        $$PWD/src/graphicsys/CGraphicsContextWin32.cpp
}

INCLUDEPATH += $$PWD/include

