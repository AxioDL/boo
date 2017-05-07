#include "IHIDDevice.hpp"
#include "boo/inputdev/DeviceToken.hpp"
#include "boo/inputdev/DeviceBase.hpp"
#include <IOKit/hid/IOHIDLib.h>
#include <IOKit/hid/IOHIDDevicePlugin.h>
#include <IOKit/hid/IOHIDDevice.h>
#include <IOKit/usb/IOUSBLib.h>
#include <IOKit/IOCFPlugIn.h>
#include "IOKitPointer.hpp"
#include <thread>

namespace boo
{

class HIDDeviceIOKit : public IHIDDevice
{
    DeviceToken& m_token;
    DeviceBase& m_devImp;

    IUnknownPointer<IOUSBInterfaceInterface> m_usbIntf;
    uint8_t m_usbIntfInPipe = 0;
    uint8_t m_usbIntfOutPipe = 0;
    IUnknownPointer<IOHIDDeviceDeviceInterface> m_hidIntf;
    bool m_runningTransferLoop = false;

    const std::string& m_devPath;
    std::mutex m_initMutex;
    std::condition_variable m_initCond;
    std::thread m_thread;

    bool _sendUSBInterruptTransfer(const uint8_t* data, size_t length)
    {
        if (m_usbIntf)
        {
            IOReturn res = m_usbIntf->WritePipe(m_usbIntf.storage(), m_usbIntfOutPipe, (void*)data, length);
            return res == kIOReturnSuccess;
        }
        return false;
    }

    size_t _receiveUSBInterruptTransfer(uint8_t* data, size_t length)
    {
        if (m_usbIntf)
        {
            UInt32 readSize = length;
            IOReturn res = m_usbIntf->ReadPipe(m_usbIntf.storage(), m_usbIntfInPipe, data, &readSize);
            if (res != kIOReturnSuccess)
                return 0;
            return readSize;
        }
        return 0;
    }

    bool _sendHIDReport(const uint8_t* data, size_t length, HIDReportType tp, uint32_t message)
    {
        if (m_hidIntf)
        {
            IOReturn res = m_hidIntf->setReport(m_hidIntf.storage(), IOHIDReportType(tp), message, data, length,
                                                1000, nullptr, nullptr, 0);
            return res == kIOReturnSuccess;
        }
        return false;
    }

    size_t _receiveHIDReport(uint8_t* data, size_t length, HIDReportType tp, uint32_t message)
    {
        if (m_hidIntf)
        {
            CFIndex readSize = length;
            IOReturn res = m_hidIntf->getReport(m_hidIntf.storage(), IOHIDReportType(tp), message, data, &readSize,
                                                1000, nullptr, nullptr, 0);
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
        IOObjectPointer<io_iterator_t> devIter;
        IOObjectPointer<io_registry_entry_t> devEntry = IORegistryEntryFromPath(kIOMasterPortDefault, device->m_devPath.c_str());
        IORegistryEntryGetChildIterator(devEntry, kIOServicePlane, &devIter);
        IOObjectPointer<io_object_t> interfaceEntry;
        while (IOObjectPointer<io_service_t> obj = IOIteratorNext(devIter))
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
        IOCFPluginPointer   iodev;
        SInt32              score;
        IOReturn            err;
        err = IOCreatePlugInInterfaceForService(interfaceEntry,
                                                kIOUSBInterfaceUserClientTypeID,
                                                kIOCFPlugInInterfaceID,
                                                &iodev,
                                                &score);
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
        IUnknownPointer<IOUSBInterfaceInterface> intf;
        err = iodev.As(&intf, kIOUSBInterfaceInterfaceID);
        if (err)
        {
            snprintf(errStr, 256, "Unable to open %s@%s\n",
                     device->m_token.getProductName().c_str(), device->m_devPath.c_str());
            device->m_devImp.deviceError(errStr);
            lk.unlock();
            device->m_initCond.notify_one();
            return;
        }

        /* Obtain exclusive lock on device */
        device->m_usbIntf = intf;
        err = intf->USBInterfaceOpen(intf.storage());
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
            return;
        }

        /* Determine pipe indices for interrupt I/O */
        UInt8 numEndpoints = 0;
        err = intf->GetNumEndpoints(intf.storage(), &numEndpoints);
        for (int i=1 ; i<numEndpoints+1 ; ++i)
        {
            UInt8 dir, num, tType, interval;
            UInt16 mPacketSz;
            err = intf->GetPipeProperties(intf.storage(), i, &dir, &num, &tType, &mPacketSz, &interval);
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
        intf->USBInterfaceClose(intf.storage());
        device->m_usbIntf = nullptr;
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

    static void _hidReportCb(void * _Nullable        context,
                             IOReturn,
                             void * _Nullable,
                             IOHIDReportType         type,
                             uint32_t                reportID,
                             uint8_t *               report,
                             CFIndex                 reportLength)
    {
        reinterpret_cast<DeviceBase*>(context)->receivedHIDReport(report, reportLength, HIDReportType(type), reportID);
    }

    static void _threadProcHID(HIDDeviceIOKit* device)
    {
        char thrName[128];
        snprintf(thrName, 128, "%s Transfer Thread", device->m_token.getProductName().c_str());
        pthread_setname_np(thrName);
        char errStr[256];
        std::unique_lock<std::mutex> lk(device->m_initMutex);

        /* Get the HID element's object (HID device interface) */
        IOObjectPointer<io_service_t> interfaceEntry = IORegistryEntryFromPath(kIOMasterPortDefault, device->m_devPath.c_str());
        if (!IOObjectConformsTo(interfaceEntry.get(), "IOHIDDevice"))
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
        IOCFPluginPointer   iodev;
        SInt32              score;
        IOReturn            err;
        err = IOCreatePlugInInterfaceForService(interfaceEntry.get(),
                                                kIOHIDDeviceTypeID,
                                                kIOCFPlugInInterfaceID,
                                                &iodev,
                                                &score);
        if (err)
        {
            snprintf(errStr, 256, "Unable to open %s@%s\n",
                     device->m_token.getProductName().c_str(), device->m_devPath.c_str());
            device->m_devImp.deviceError(errStr);
            lk.unlock();
            device->m_initCond.notify_one();
            return;
        }

        /* HID interface function-pointer table */
        IUnknownPointer<IOHIDDeviceDeviceInterface> intf;
        err = iodev.As(&intf, kIOHIDDeviceDeviceInterfaceID);
        if (err)
        {
            snprintf(errStr, 256, "Unable to open %s@%s\n",
                     device->m_token.getProductName().c_str(), device->m_devPath.c_str());
            device->m_devImp.deviceError(errStr);
            lk.unlock();
            device->m_initCond.notify_one();
            return;
        }

        /* Open device */
        device->m_hidIntf = intf;
        err = intf->open(intf.storage(), kIOHIDOptionsTypeNone);
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
            return;
        }

        /* Register input buffer */
        std::unique_ptr<uint8_t[]> buffer;
        if (size_t bufSize = device->m_devImp.getInputBufferSize())
        {
            buffer = std::unique_ptr<uint8_t[]>(new uint8_t[bufSize]);
            CFTypeRef source;
            device->m_hidIntf->getAsyncEventSource(device->m_hidIntf.storage(), &source);
            device->m_hidIntf->setInputReportCallback(device->m_hidIntf.storage(), buffer.get(),
                                                      bufSize, _hidReportCb, &device->m_devImp, 0);
            CFRunLoopRef rl = CFRunLoopGetCurrent();
            CFRunLoopAddSource(rl, CFRunLoopSourceRef(source), kCFRunLoopDefaultMode);
            CFRunLoopWakeUp(rl);
        }

        /* Return control to main thread */
        device->m_runningTransferLoop = true;
        lk.unlock();
        device->m_initCond.notify_one();

        /* Start transfer loop */
        device->m_devImp.initialCycle();
        while (device->m_runningTransferLoop)
        {
            CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0, false);
            device->m_devImp.transferCycle();
        }
        device->m_devImp.finalCycle();

        /* Cleanup */
        intf->close(intf.storage(), kIOHIDOptionsTypeNone);
        device->m_hidIntf = nullptr;
    }

    void _deviceDisconnected()
    {
        m_runningTransferLoop = false;
    }

public:

    HIDDeviceIOKit(DeviceToken& token, DeviceBase& devImp)
    : m_token(token),
      m_devImp(devImp),
      m_devPath(token.getDevicePath())
    {
        std::unique_lock<std::mutex> lk(m_initMutex);
        DeviceType dType = token.getDeviceType();
        if (dType == DeviceType::USB)
            m_thread = std::thread(_threadProcUSBLL, this);
        else if (dType == DeviceType::Bluetooth)
            m_thread = std::thread(_threadProcBTLL, this);
        else if (dType == DeviceType::HID)
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

std::unique_ptr<IHIDDevice> IHIDDeviceNew(DeviceToken& token, DeviceBase& devImp)
{
    return std::make_unique<HIDDeviceIOKit>(token, devImp);
}

}
