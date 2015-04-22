#include "inputdev/CDolphinSmashAdapter.hpp"
#include <stdio.h>

CDolphinSmashAdapter::CDolphinSmashAdapter(CDeviceToken* token, IHIDDevice* hidDev)
: CDeviceBase(token, hidDev)
{
    printf("I've been plugged!!\n");
}

void CDolphinSmashAdapter::deviceDisconnected()
{
    printf("I've been unplugged!!\n");
}
