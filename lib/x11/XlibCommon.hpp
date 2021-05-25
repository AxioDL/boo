#pragma once

#include <X11/Xlib.h>

namespace boo {

struct XlibCursors {
  Cursor m_pointer;
  Cursor m_weArrow;
  Cursor m_nsArrow;
  Cursor m_ibeam;
  Cursor m_crosshairs;
  Cursor m_wait;
  Cursor m_nwseResize;
  Cursor m_neswResize;
  Cursor m_hand;
  Cursor m_notAllowed;
};
extern XlibCursors X_CURSORS;

} // namespace boo
