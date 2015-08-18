#include "inputdev/GenericPad.hpp"
#include "inputdev/DeviceToken.hpp"

namespace boo
{

GenericPad::GenericPad(DeviceToken* token)
 : DeviceBase(token)
{

}

GenericPad::~GenericPad()
{

}

void GenericPad::deviceDisconnected()
{

}

}
