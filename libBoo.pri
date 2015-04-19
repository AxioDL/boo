!contains(CONFIG,c++11):CONFIG += C++11

HEADERS += \
    $$PWD/include/boo.hpp \
    $$PWD/include/IGraphicsContext.hpp \
    $$PWD/include/ISurface.hpp \
    $$PWD/include/IRetraceWaiter.hpp

unix:!macx:HEADERS += \
    $$PWD/include/CGLXContext.hpp

mac:HEADERS += \
    $$PWD/include/CCGLContext.hpp

win32:HEADERS += \
    $$PWD/include/CWGLContext.hpp

SOURCES += \
    $$PWD/src/CSurface.cpp \
    $$PWD/src/CCGLContext.cpp \
    $$PWD/src/CRetraceWaiter.cpp

unix:!macx:SOURCES += \
    $$PWD/src/CGLXContext.cpp

mac:OBJECTIVE_SOURCES += \
    $$PWD/src/CCGLCocoaView.mm

win32:SOURCES += \
    $$PWD/src/CWGLContext.cpp

INCLUDEPATH += $$PWD/include
