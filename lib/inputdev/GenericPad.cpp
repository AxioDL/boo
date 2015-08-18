#include "boo/inputdev/GenericPad.hpp"
#include "boo/inputdev/DeviceToken.hpp"

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
