CONFIG -= Qt
QT      =
LIBS   -= -lQtGui -lQtCore

unix:QMAKE_CXXFLAGS += -std=c++11 -stdlib=libc++
unix:!macx:LIBS += -std=c++11 -stdlib=libc++ -lc++abi
unix:!macx:CONFIG += link_pkgconfig
unix:!macx:PKGCONFIG += xcb xcb-glx xcb-xinput xcb-xkb xcb-keysyms xkbcommon xkbcommon-x11 dbus-1

win32:LIBS += Setupapi.lib winusb.lib User32.lib /SUBSYSTEM:Windows

include(libBoo.pri)
include(test/test.pri)
