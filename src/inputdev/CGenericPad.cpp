#include "inputdev/CGenericPad.hpp"
#include "inputdev/CDeviceToken.hpp"

namespace boo
{

CGenericPad::CGenericPad(CDeviceToken* token)
 : CDeviceBase(token)
{

}

CGenericPad::~CGenericPad()
{

}

void CGenericPad::deviceDisconnected()
{

}

}
