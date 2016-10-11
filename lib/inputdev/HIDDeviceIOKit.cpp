#include "IHIDDevice.hpp"
#include "boo/inputdev/DeviceToken.hpp"
#include "boo/inputdev/DeviceBase.hpp"
#include <IOKit/hid/IOHIDLib.h>
#include <IOKit/usb/IOUSBLib.h>
#include <IOKit/IOCFPlugIn.h>
#include <thread>

namespace boo
{

class HIDDeviceIOKit : public IHIDDevice
{
    DeviceToken& m_token;
    DeviceBase& m_devImp;

    IOUSBInterfaceInterface** m_usbIntf = NULL;
    uint8_t m_usbIntfInPipe = 0;
    uint8_t m_usbIntfOutPipe = 0;
    bool m_runningTransferLoop = false;

    const std::string& m_devPath;
    std::mutex m_initMutex;
    std::condition_variable m_initCond;
    std::thread m_thread;

    bool _sendUSBInterruptTransfer(const uint8_t* data, size_t length)
    {
        if (m_usbIntf)
        {
            IOReturn res = (*m_usbIntf)->WritePipe(m_usbIntf, m_usbIntfOutPipe, (void*)data, length);
            return res == kIOReturnSuccess;
        }
        return false;
    }

    size_t _receiveUSBInterruptTransfer(uint8_t* data, size_t length)
    {
        if (m_usbIntf)
        {
            UInt32 readSize = length;
            IOReturn res = (*m_usbIntf)->ReadPipe(m_usbIntf, m_usbIntfInPipe, data, &readSize);
            if (res != kIOReturnSuccess)
                return 0;
            return readSize;
        }
        return 0;
    }

    static void _threadProcUSBLL(HIDDeviceIOKit* device)
    {
        char thrName[128];
        snprintf(thrName, 128, "%s Transfer Thread", device->m_token.getProductName().c_str());
        pthread_setname_np(thrName);
        char errStr[256];
        std::unique_lock<std::mutex> lk(device->m_initMutex);

        /* Get the HID element's parent (USB interrupt transfer-interface) */
        io_iterator_t devIter;
        io_registry_entry_t devEntry = IORegistryEntryFromPath(kIOMasterPortDefault, device->m_devPath.c_str());
        IORegistryEntryGetChildIterator(devEntry, kIOServicePlane, &devIter);
        io_object_t obj, interfaceEntry = 0;
        while ((obj = IOIteratorNext(devIter)))
        {
            if (IOObjectConformsTo(obj, kIOUSBInterfaceClassName))
                interfaceEntry = obj;
            else
                IOObjectRelease(obj);
        }
        if (!interfaceEntry)
        {
            snprintf(errStr, 256, "Unable to find interface for %s@%s\n",
                     device->m_token.getProductName().c_str(),
                     device->m_devPath.c_str());
            device->m_devImp.deviceError(errStr);
            lk.unlock();
            device->m_initCond.notify_one();
            return;
        }

        /* IOKit Plugin COM interface (WTF Apple???) */
        IOCFPlugInInterface	**iodev;
        SInt32              score;
        IOReturn            err;
        err = IOCreatePlugInInterfaceForService(interfaceEntry,
                                                kIOUSBInterfaceUserClientTypeID,
                                                kIOCFPlugInInterfaceID,
                                                &iodev,
                                                &score);
        IOObjectRelease(interfaceEntry);
        if (err)
        {
            snprintf(errStr, 256, "Unable to open %s@%s\n",
                     device->m_token.getProductName().c_str(), device->m_devPath.c_str());
            device->m_devImp.deviceError(errStr);
            lk.unlock();
            device->m_initCond.notify_one();
            return;
        }

        /* USB interface function-pointer table */
        IOUSBInterfaceInterface** intf = NULL;
        err = (*iodev)->QueryInterface(iodev,
                                       CFUUIDGetUUIDBytes(kIOUSBInterfaceInterfaceID),
                                       (LPVOID*)&intf);
        if (err)
        {
            snprintf(errStr, 256, "Unable to open %s@%s\n",
                     device->m_token.getProductName().c_str(), device->m_devPath.c_str());
            device->m_devImp.deviceError(errStr);
            lk.unlock();
            device->m_initCond.notify_one();
            IODestroyPlugInInterface(iodev);
            return;
        }

        /* Obtain exclusive lock on device */
        device->m_usbIntf = intf;
        err = (*intf)->USBInterfaceOpen(intf);
        if (err != kIOReturnSuccess)
        {
            if (err == kIOReturnExclusiveAccess)
            {
                snprintf(errStr, 256, "Unable to open %s@%s: someone else using it\n",
                         device->m_token.getProductName().c_str(), device->m_devPath.c_str());
                device->m_devImp.deviceError(errStr);
            }
            else
            {
                snprintf(errStr, 256, "Unable to open %s@%s\n",
                         device->m_token.getProductName().c_str(), device->m_devPath.c_str());
                device->m_devImp.deviceError(errStr);
            }
            lk.unlock();
            device->m_initCond.notify_one();
            (*intf)->Release(intf);
            IODestroyPlugInInterface(iodev);
            return;
        }

        /* Determine pipe indices for interrupt I/O */
        UInt8 numEndpoints = 0;
        err = (*intf)->GetNumEndpoints(intf, &numEndpoints);
        for (int i=1 ; i<numEndpoints+1 ; ++i)
        {
            UInt8 dir, num, tType, interval;
            UInt16 mPacketSz;
            err = (*intf)->GetPipeProperties(intf, i, &dir, &num, &tType, &mPacketSz, &interval);
            if (tType == kUSBInterrupt)
            {
                if (dir == kUSBIn)
                    device->m_usbIntfInPipe = num;
                else if (dir == kUSBOut)
                    device->m_usbIntfOutPipe = num;
            }
        }

        /* Return control to main thread */
        device->m_runningTransferLoop = true;
        lk.unlock();
        device->m_initCond.notify_one();

        /* Start transfer loop */
        device->m_devImp.initialCycle();
        while (device->m_runningTransferLoop)
            device->m_devImp.transferCycle();
        device->m_devImp.finalCycle();

        /* Cleanup */
        (*intf)->USBInterfaceClose(intf);
        (*intf)->Release(intf);
        IODestroyPlugInInterface(iodev);
        device->m_usbIntf = NULL;

    }

    static void _threadProcBTLL(HIDDeviceIOKit* device)
    {
        std::unique_lock<std::mutex> lk(device->m_initMutex);

        /* Return control to main thread */
        device->m_runningTransferLoop = true;
        lk.unlock();
        device->m_initCond.notify_one();

        /* Start transfer loop */
        device->m_devImp.initialCycle();
        while (device->m_runningTransferLoop)
            device->m_devImp.transferCycle();
        device->m_devImp.finalCycle();

    }

    static void _threadProcHID(HIDDeviceIOKit* device)
    {
        std::unique_lock<std::mutex> lk(device->m_initMutex);

        /* Return control to main thread */
        device->m_runningTransferLoop = true;
        lk.unlock();
        device->m_initCond.notify_one();

        /* Start transfer loop */
        device->m_devImp.initialCycle();
        while (device->m_runningTransferLoop)
            device->m_devImp.transferCycle();
        device->m_devImp.finalCycle();
    }

    void _deviceDisconnected()
    {
        m_runningTransferLoop = false;
    }

    bool _sendHIDReport(const uint8_t* data, size_t length, uint16_t message)
    {
        return false;
    }

public:

    HIDDeviceIOKit(DeviceToken& token, DeviceBase& devImp)
    : m_token(token),
      m_devImp(devImp),
      m_devPath(token.getDevicePath())
    {
        devImp.m_hidDev = this;
        std::unique_lock<std::mutex> lk(m_initMutex);
        DeviceToken::DeviceType dType = token.getDeviceType();
        if (dType == DeviceToken::DeviceType::USB)
            m_thread = std::thread(_threadProcUSBLL, this);
        else if (dType == DeviceToken::DeviceType::Bluetooth)
            m_thread = std::thread(_threadProcBTLL, this);
        else if (dType == DeviceToken::DeviceType::GenericHID)
            m_thread = std::thread(_threadProcHID, this);
        else
        {
            fprintf(stderr, "invalid token supplied to device constructor\n");
            return;
        }
        m_initCond.wait(lk);
    }

    ~HIDDeviceIOKit()
    {
        m_runningTransferLoop = false;
        if (m_thread.joinable())
            m_thread.join();
    }


};

IHIDDevice* IHIDDeviceNew(DeviceToken& token, DeviceBase& devImp)
{
    return new HIDDeviceIOKit(token, devImp);
}

}
