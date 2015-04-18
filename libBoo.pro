CONFIG -= Qt
CONFIG += app c++11

unix:!macx:CONFIG += link_pkgconfig
unix:!macx:PKGCONFIG += x11

include(libBoo.pri)
include(test/test.pri)
