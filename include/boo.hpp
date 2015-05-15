#ifndef BOO_HPP
#define BOO_HPP

#if defined(_WIN32)
#include "win/CWGLContext.hpp"
namespace boo {typedef CWGLContext CGraphicsContext;}

#elif defined(__APPLE__)
#include "mac/CCGLContext.hpp"
namespace boo {typedef CCGLContext CGraphicsContext;}

#elif defined(__GNUC__) || defined(__clang__)
#include "x11/CGLXContext.hpp"
namespace boo {typedef CGLXContext CGraphicsContext;}

#endif

#include "IGraphicsContext.hpp"
#include "inputdev/CDeviceFinder.hpp"
#include "inputdev/CDolphinSmashAdapter.hpp"
#include "inputdev/CDualshockPad.hpp"

#endif // BOO_HPP
