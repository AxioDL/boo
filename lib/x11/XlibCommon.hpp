#ifndef BOO_XLIBCOMMON_HPP
#define BOO_XLIBCOMMON_HPP

#include <X11/Xlib.h>

namespace boo
{

struct XlibCursors
{
    Cursor m_pointer;
    Cursor m_hArrow;
    Cursor m_vArrow;
    Cursor m_ibeam;
    Cursor m_wait;
};
extern XlibCursors X_CURSORS;

}

#endif // BOO_XLIBCOMMON_HPP
