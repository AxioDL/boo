CONFIG -= Qt
#QMAKE_CXXFLAGS -= -std=c++0x
QMAKE_CXXFLAGS += -std=c++11

#unix:!macx:CONFIG += link_pkgconfig
#unix:!macx:PKGCONFIG += x11

include(libBoo.pri)
include(test/test.pri)
