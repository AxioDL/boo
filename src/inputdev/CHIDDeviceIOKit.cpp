#include "IHIDDevice.hpp"
#include "inputdev/CDeviceToken.hpp"
#include "inputdev/CDeviceBase.hpp"
#include <IOKit/hid/IOHIDLib.h>
#include <IOKit/usb/IOUSBLib.h>
#include <IOKit/IOCFPlugIn.h>
#include <thread>

#define MAX_REPORT_SIZE 65536

namespace boo
{

class CHIDDeviceIOKit final : public IHIDDevice
{
    CDeviceToken& m_token;
    CDeviceBase& m_devImp;

    IOUSBInterfaceInterface** m_usbIntf = NULL;
    uint8_t m_usbIntfInPipe = 0;
    uint8_t m_usbIntfOutPipe = 0;
    bool m_runningTransferLoop = false;
    
    const std::string& m_devPath;
    std::mutex m_initMutex;
    std::condition_variable m_initCond;
    std::thread* m_thread;
    CFRunLoopRef m_runLoop = NULL;
    
    bool _sendUSBInterruptTransfer(uint8_t pipe, const uint8_t* data, size_t length)
    {
        if (m_usbIntf)
        {
            IOReturn res = (*m_usbIntf)->WritePipe(m_usbIntf, m_usbIntfOutPipe, (void*)data, length);
            return res == kIOReturnSuccess;
        }
        return false;
    }
    
    size_t _receiveUSBInterruptTransfer(uint8_t pipe, uint8_t* data, size_t length)
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
    
    static void _threadProcLL(CHIDDeviceIOKit* device)
    {
        char thrName[128];
        snprintf(thrName, 128, "%s Transfer Thread", device->m_token.getProductName().c_str());
        pthread_setname_np(thrName);
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
            throw std::runtime_error("unable to find device interface");
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
            throw std::runtime_error("unable to obtain IOKit plugin service");
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
            throw std::runtime_error("unable to query IOKit USB interface");
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
                throw std::runtime_error("unable to open IOKit USB interface; someone else using it");
            else
                throw std::runtime_error("unable to open IOKit USB interface");
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
        while (device->m_runningTransferLoop)
            device->m_devImp.transferCycle();
        device->m_devImp.finalCycle();

        /* Cleanup */
        (*intf)->USBInterfaceClose(intf);
        (*intf)->Release(intf);
        IODestroyPlugInInterface(iodev);
        device->m_usbIntf = NULL;
        
    }
    

    static void _inputReport(CHIDDeviceIOKit*        device,
                             IOReturn                result,
                             void*                   sender,
                             IOHIDReportType         type,
                             uint32_t                reportID,
                             uint8_t*                report,
                             CFIndex                 reportLength)
    {
        
    }
    static void _disconnect(CHIDDeviceIOKit*         device,
                            IOReturn                 result,
                            IOHIDDeviceRef           sender)
    {
        device->_deviceDisconnected();
    }
    
    static void _threadProcHL(CHIDDeviceIOKit* device)
    {
        char thrName[128];
        snprintf(thrName, 128, "%s HID Thread", device->m_token.getProductName().c_str());
        pthread_setname_np(thrName);
        __block std::unique_lock<std::mutex> lk(device->m_initMutex);
        device->m_runLoop = CFRunLoopGetCurrent();
        CFRunLoopAddObserver(device->m_runLoop,
        CFRunLoopObserverCreateWithHandler(kCFAllocatorDefault, kCFRunLoopEntry, false, 0,
                                           ^(CFRunLoopObserverRef, CFRunLoopActivity) {
                                               lk.unlock();
                                               device->m_initCond.notify_one();
        }), kCFRunLoopCommonModes);
        
        uint8_t* inputBuf = new uint8_t[MAX_REPORT_SIZE];
        io_registry_entry_t devServ = IORegistryEntryFromPath(kIOMasterPortDefault, device->m_devPath.c_str());
        IOHIDDeviceRef dev = IOHIDDeviceCreate(kCFAllocatorDefault, devServ);
        IOHIDDeviceRegisterInputReportCallback(dev, inputBuf, MAX_REPORT_SIZE,
                                               (IOHIDReportCallback)_inputReport, device);
        IOHIDDeviceRegisterRemovalCallback(dev, (IOHIDCallback)_disconnect, device);
        IOHIDDeviceScheduleWithRunLoop(dev, device->m_runLoop, kCFRunLoopDefaultMode);
        IOHIDDeviceOpen(dev, kIOHIDOptionsTypeNone);
        CFRunLoopRun();
        if (device->m_runLoop)
            IOHIDDeviceClose(dev, kIOHIDOptionsTypeNone);
        CFRelease(dev);
    }
    
    void _deviceDisconnected()
    {
        CFRunLoopRef rl = m_runLoop;
        m_runLoop = NULL;
        if (rl)
            CFRunLoopStop(rl);
        m_runningTransferLoop = false;
    }
    
    bool _sendHIDReport(const uint8_t* data, size_t length)
    {
        if (m_runLoop)
        {
            
        }
        return false;
    }
    
public:
    
    CHIDDeviceIOKit(CDeviceToken& token, CDeviceBase& devImp)
    : m_token(token),
      m_devImp(devImp),
      m_devPath(token.getDevicePath())
    {
        devImp.m_hidDev = this;
        std::unique_lock<std::mutex> lk(m_initMutex);
        if (lowLevel)
            m_thread = new std::thread(_threadProcLL, this);
        else
            m_thread = new std::thread(_threadProcHL, this);
        m_initCond.wait(lk);
    }
    
    ~CHIDDeviceIOKit()
    {
        if (m_runLoop)
            CFRunLoopStop(m_runLoop);
        m_runningTransferLoop = false;
        m_thread->detach();
        delete m_thread;
    }
    

};

IHIDDevice* IHIDDeviceNew(CDeviceToken& token, CDeviceBase& devImp)
{
    return new CHIDDeviceIOKit(token, devImp);
}

}
