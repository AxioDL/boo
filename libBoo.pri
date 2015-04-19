!contains(CONFIG,c++11):CONFIG += C++11

HEADERS += \
    $$PWD/include/boo.hpp \
    $$PWD/include/IGraphicsContext.hpp \
    $$PWD/include/ISurface.hpp \
    $$PWD/include/IRetraceWaiter.hpp

unix:!macx:HEADERS += \
    $$PWD/include/x11/CGLXContext.hpp

macx:HEADERS += \
    $$PWD/include/mac/CCGLContext.hpp

win32:HEADERS += \
    $$PWD/include/win/CWGLContext.hpp

SOURCES += \
    $$PWD/src/CSurface.cpp \
    $$PWD/src/CRetraceWaiter.cpp

unix:!macx:SOURCES += \
    $$PWD/src/x11/CGLXContext.cpp

macx:SOURCES += \
    $$PWD/src/mac/CCGLContext.cpp

macx:OBJECTIVE_SOURCES += \
    $$PWD/src/mac/CCGLCocoaView.mm

win32:SOURCES += \
    $$PWD/src/CWGLContext.cpp

INCLUDEPATH += $$PWD/include
