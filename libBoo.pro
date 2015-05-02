CONFIG -= Qt
CONFIG += console
#QMAKE_CXXFLAGS -= -std=c++0x
#CONFIG += c++11
unix:QMAKE_CXXFLAGS += -stdlib=libc++
unix:LIBS += -std=c++11 -stdlib=libc++ -lc++abi

win32:INCLUDEPATH += $$PWD/extern/libwdi
win32:LIBS += \
    Shell32.lib \
    Ole32.lib \
    Setupapi.lib \
    Advapi32.lib \
    User32.lib \
    $$PWD/extern/libwdi/x64/Debug/lib/libwdi.lib

#unix:!macx:CONFIG += link_pkgconfig
#unix:!macx:PKGCONFIG += x11

include(libBoo.pri)
include(test/test.pri)
