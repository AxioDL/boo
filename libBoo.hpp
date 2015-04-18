#ifndef LIBBOO_HPP
#define LIBBOO_HPP

#include "IContext.hpp"

#if defined(_WIN32)
#error "No support for WGL"
#elif defined(__APPLE__)
#error "No support for Apple GL"
#elif __linux__
#include "CGLXContext.hpp"
typedef CGLXContext CContext;
#endif

#endif // LIBBOO_HPP

