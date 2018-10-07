#pragma once

#include <X11/Xlib.h>

namespace boo
{

struct XlibCursors
{
    Cursor m_pointer;
    Cursor m_hArrow;
    Cursor m_vArrow;
    Cursor m_ibeam;
    Cursor m_crosshairs;
    Cursor m_wait;
};
extern XlibCursors X_CURSORS;

}

