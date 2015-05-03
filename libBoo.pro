CONFIG -= Qt
CONFIG += console
#QMAKE_CXXFLAGS -= -std=c++0x
#CONFIG += c++11
unix:QMAKE_CXXFLAGS += -stdlib=libc++
unix:LIBS += -std=c++11 -stdlib=libc++ -lc++abi

win32:LIBS += Setupapi.lib winusb.lib User32.lib

#unix:!macx:CONFIG += link_pkgconfig
#unix:!macx:PKGCONFIG += x11

include(libBoo.pri)
include(test/test.pri)
