CONFIG -= Qt
CONFIG += console
#QMAKE_CXXFLAGS -= -std=c++0x
#CONFIG += c++11
mac:QMAKE_CXXFLAGS += -std=c++11 -stdlib=libc++
mac:LIBS += -std=c++11 -lc++abi
unix:!mac:QMAKE_CXXFLAGS += -std=c++11
unix:!mac:LIBS += -std=c++11 -lc++abi

win32:LIBS += Setupapi.lib winusb.lib User32.lib /SUBSYSTEM:Windows

#unix:!macx:CONFIG += link_pkgconfig
#unix:!macx:PKGCONFIG += x11

include(libBoo.pri)
include(test/test.pri)
