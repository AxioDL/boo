CONFIG -= Qt
CONFIG += app c++11

unix:CONFIG += link_pkgconfig
unix:PKGCONFIG += x11

include(libBoo.pri)
include(test/test.pri)
