#ifndef BOO_HPP
#define BOO_HPP

#if defined(_WIN32)
#error "No support for WGL"

#elif defined(__APPLE__)
#include "mac/CCGLContext.hpp"
namespace boo {typedef CCGLContext CGraphicsContext;}


#elif defined(__GNUC__) || defined(__clang__)
#include "x11/CGLXContext.hpp"
namespace boo {typedef boo::CGLXContext CGraphicsContext;}

#endif

#include "IGraphicsContext.hpp"
#include "inputdev/CDeviceFinder.hpp"

#endif // BOO_HPP
