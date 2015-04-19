#ifndef BOO_HPP
#define BOO_HPP

#include "IGraphicsContext.hpp"

#if defined(_WIN32)
#error "No support for WGL"
#elif defined(__APPLE__)
#include "mac/CCGLContext.hpp"
typedef CCGLContext CGraphicsContext;
#elif defined(__GNUC__) || defined(__clang__)
#include "x11/CGLXContext.hpp"
typedef CGLXContext CGraphicsContext;
#endif

#endif // BOO_HPP
