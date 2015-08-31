#ifndef BOO_SYSTEM_HPP
#define BOO_SYSTEM_HPP

#include <string>

namespace boo
{

#ifdef _WIN32
    using SystemString = std::wstring;
    using SystemChar = wchar_t;
#   ifndef _S
#   define _S(val) L ## val
#   endif
#else
    using SystemString = std::string;
    using SystemChar = char;
#   ifndef _S
#   define _S(val) val
#   endif
#endif

}

#endif