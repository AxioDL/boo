#include "inputdev/CDolphinSmashAdapter.hpp"
#include <stdio.h>

CDolphinSmashAdapter::CDolphinSmashAdapter(CDeviceToken* token, IHIDDevice* hidDev)
: CDeviceBase(token, hidDev)
{
    
}

CDolphinSmashAdapter::~CDolphinSmashAdapter()
{
    
}

void CDolphinSmashAdapter::deviceDisconnected()
{
    
}
