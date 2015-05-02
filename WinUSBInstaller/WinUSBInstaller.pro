CONFIG += console
CONFIG -= app_bundle
CONFIG -= qt

!win32 {
    error(this project is designed for windows only)
}

INCLUDEPATH += $$PWD/../extern/libwdi
LIBS += \
    Shell32.lib \
    Ole32.lib \
    Setupapi.lib \
    Advapi32.lib \
    User32.lib \
    $$PWD/../extern/libwdi/x64/Debug/lib/libwdi.lib

SOURCES += main.c


