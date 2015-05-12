CONFIG -= Qt
QT      =
LIBS   -= -lQtGui -lQtCore
#CONFIG += console
#QMAKE_CXXFLAGS -= -std=c++0x
#CONFIG += c++11
unix:QMAKE_CXXFLAGS += -std=c++11 -stdlib=libc++
unix:LIBS += -std=c++11 -stdlib=libc++ -lc++abi -lxcb \
             -lxcb-glx -lxcb-xkb -lxcb-xinput -lxcb-keysyms \
             -lxkbcommon -lxkbcommon-x11

win32:LIBS += Setupapi.lib winusb.lib User32.lib /SUBSYSTEM:Windows

#unix:!macx:CONFIG += link_pkgconfig
#unix:!macx:PKGCONFIG += x11

include(libBoo.pri)
include(test/test.pri)
