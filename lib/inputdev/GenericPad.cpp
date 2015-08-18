#include "inputdev/GenericPad.hpp"
#include "inputdev/DeviceToken.hpp"

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
