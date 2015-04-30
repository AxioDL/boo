CONFIG -= Qt
#QMAKE_CXXFLAGS -= -std=c++0x
CONFIG += c++11
QMAKE_CXXFLAGS += -stdlib=libc++
LIBS += -std=c++11 -stdlib=libc++ -lc++abi

#unix:!macx:CONFIG += link_pkgconfig
#unix:!macx:PKGCONFIG += x11

include(libBoo.pri)
include(test/test.pri)
