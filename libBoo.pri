!contains(CONFIG,c++11):CONFIG += C++11

HEADERS += \
    $$PWD/include/boo.hpp \
    $$PWD/include/IContext.hpp \
    $$PWD/include/CCGLContext.hpp \
    $$PWD/include/CGLXContext.hpp \
    $$PWD/include/ISurface.hpp

SOURCES += \
    $$PWD/src/CCGLContext.cpp \
    $$PWD/src/CGLXContext.cpp

INCLUDEPATH += $$PWD/include
