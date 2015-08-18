#ifndef CGENERICPAD_HPP
#define CGENERICPAD_HPP

#include "DeviceBase.hpp"

namespace boo
{

class GenericPad final : public DeviceBase
{
public:
    GenericPad(DeviceToken* token);
    ~GenericPad();

    void deviceDisconnected();
};

}

#endif // CGENERICPAD_HPP
