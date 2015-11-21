#include "boo/inputdev/IHIDListener.hpp"
#include "boo/inputdev/DeviceFinder.hpp"
#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/hid/IOHIDLib.h>
#include <IOKit/IOKitLib.h>
#include <IOKit/usb/IOUSBLib.h>
#include <IOKit/IOCFPlugIn.h>

namespace boo
{

/*
 * Reference: http://oroboro.com/usb-serial-number-osx/
 */

static bool getUSBStringDescriptor(IOUSBDeviceInterface182** usbDevice, UInt8 idx, char* out)
{
    UInt16 buffer[128];
    
    // wow... we're actually forced to make hard coded bus requests. Its like
    // hard disk programming in the 80's!
    IOUSBDevRequest request;
    
    request.bmRequestType = USBmakebmRequestType(kUSBIn,
                                                 kUSBStandard,
                                                 kUSBDevice);
    request.bRequest = kUSBRqGetDescriptor;
    request.wValue = (kUSBStringDesc << 8) | idx;
    request.wIndex = 0x409; // english
    request.wLength = sizeof(buffer);
    request.pData = buffer;
    
    kern_return_t err = (*usbDevice)->DeviceRequest(usbDevice, &request);
    if (err != 0)
    {
        // the request failed... fairly uncommon for the USB disk driver, but not
        // so uncommon for other devices. This can also be less reliable if your
        // disk is mounted through an external USB hub. At this level we actually
        // have to worry about hardware issues like this.
        return false;
    }
    
    // we're mallocing this string just as an example. But you probably will want
    // to do something smarter, like pre-allocated buffers in the info class, or
    // use a string class.
    unsigned count = (request.wLenDone - 1) / 2;
    unsigned i;
    for (i=0 ; i<count ; ++i)
        out[i] = buffer[i+1];
    out[i] = '\0';
    
    return true;
}

class HIDListenerIOKit : public IHIDListener
{
    DeviceFinder& m_finder;
    
    CFRunLoopRef m_listenerRunLoop;
    IONotificationPortRef m_llPort;
    io_iterator_t m_llAddNotif, m_llRemoveNotif;
    bool m_scanningEnabled;
    
    static void devicesConnectedUSBLL(HIDListenerIOKit* listener,
                                      io_iterator_t      iterator)
    {
        io_object_t obj;
        while ((obj = IOIteratorNext(iterator)))
        {
            io_string_t devPath;
            if (IORegistryEntryGetPath(obj, kIOServicePlane, devPath) != 0)
                continue;

            if (!listener->m_scanningEnabled ||
                listener->m_finder._hasToken(devPath))
            {
                IOObjectRelease(obj);
                continue;
            }
            
            IOCFPlugInInterface** devServ;
            SInt32 score;
            IOReturn err;
            err = IOCreatePlugInInterfaceForService(obj, kIOUSBDeviceUserClientTypeID, kIOCFPlugInInterfaceID, &devServ, &score);
            if (err != kIOReturnSuccess)
            {
                fprintf(stderr, "unable to open IOKit plugin interface\n");
                return;
            }
            
            IOUSBDeviceInterface182 **dev;
            err = (*devServ)->QueryInterface(devServ,
                                             CFUUIDGetUUIDBytes(kIOUSBDeviceInterfaceID182),
                                             (LPVOID*)&dev);
            if (err != kIOReturnSuccess)
            {
                fprintf(stderr, "unable to open IOKit device interface\n");
                return;
            }
            
            UInt16 vid, pid;
            (*dev)->GetDeviceVendor(dev, &vid);
            (*dev)->GetDeviceProduct(dev, &pid);

            UInt8 vstridx, pstridx;
            (*dev)->USBGetManufacturerStringIndex(dev, &vstridx);
            (*dev)->USBGetProductStringIndex(dev, &pstridx);
            
            char vstr[128] = {0};
            char pstr[128] = {0};
            getUSBStringDescriptor(dev, vstridx, vstr);
            getUSBStringDescriptor(dev, pstridx, pstr);

            if (!listener->m_finder._insertToken(DeviceToken(DeviceToken::DeviceType::USB,
                                                             vid, pid, vstr, pstr, devPath)))
            {
                /* Matched-insertion failed; see if generic HID interface is available */
                /* TODO: Do */
            }

            //printf("ADDED %08X %s\n", obj, devPath);
            (*dev)->Release(dev);
            IODestroyPlugInInterface(devServ);
            IOObjectRelease(obj);
        }
        
    }
    
    static void devicesDisconnectedUSBLL(HIDListenerIOKit* listener,
                                         io_iterator_t      iterator)
    {
        if (CFRunLoopGetCurrent() != listener->m_listenerRunLoop)
        {
            CFRunLoopPerformBlock(listener->m_listenerRunLoop, kCFRunLoopDefaultMode, ^{
                devicesDisconnectedUSBLL(listener, iterator);
            });
            CFRunLoopWakeUp(listener->m_listenerRunLoop);
            return;
        }
        io_object_t obj;
        while ((obj = IOIteratorNext(iterator)))
        {
            io_string_t devPath;
            if (IORegistryEntryGetPath(obj, kIOServicePlane, devPath) != 0)
                continue;
            listener->m_finder._removeToken(devPath);
            //printf("REMOVED %08X %s\n", obj, devPath);
            IOObjectRelease(obj);
        }
    }

public:
    HIDListenerIOKit(DeviceFinder& finder)
    : m_finder(finder)
    {
        
        /* Register Low-Level USB Matcher */
        m_listenerRunLoop = CFRunLoopGetCurrent();
        m_llPort = IONotificationPortCreate(kIOMasterPortDefault);
        CFRunLoopSourceRef rlSrc = IONotificationPortGetRunLoopSource(m_llPort);
        CFRunLoopAddSource(m_listenerRunLoop, rlSrc, kCFRunLoopDefaultMode);
        
        CFMutableDictionaryRef matchDict = IOServiceMatching(kIOUSBDeviceClassName);
        CFRetain(matchDict);
        
        m_scanningEnabled = true;
        kern_return_t llRet =
        IOServiceAddMatchingNotification(m_llPort, kIOMatchedNotification, matchDict,
                                         (IOServiceMatchingCallback)devicesConnectedUSBLL, this, &m_llAddNotif);
        if (llRet == kIOReturnSuccess)
            devicesConnectedUSBLL(this, m_llAddNotif);
        
        llRet =
        IOServiceAddMatchingNotification(m_llPort, kIOTerminatedNotification, matchDict,
                                         (IOServiceMatchingCallback)devicesDisconnectedUSBLL, this, &m_llRemoveNotif);
        if (llRet == kIOReturnSuccess)
            devicesDisconnectedUSBLL(this, m_llRemoveNotif);
        
        m_scanningEnabled = false;
        
    }
    
    ~HIDListenerIOKit()
    {
        //CFRunLoopRemoveSource(m_listenerRunLoop, IONotificationPortGetRunLoopSource(m_llPort), kCFRunLoopDefaultMode);
        IOObjectRelease(m_llAddNotif);
        IOObjectRelease(m_llRemoveNotif);
        IONotificationPortDestroy(m_llPort);
    }
    
    /* Automatic device scanning */
    bool startScanning()
    {
        m_scanningEnabled = true;
        return true;
    }
    bool stopScanning()
    {
        m_scanningEnabled = false;
        return true;
    }
    
    /* Manual device scanning */
    bool scanNow()
    {
        io_iterator_t iter;
        if (IOServiceGetMatchingServices(kIOMasterPortDefault,
                                         IOServiceMatching(kIOUSBDeviceClassName), &iter) == kIOReturnSuccess)
        {
            devicesConnectedUSBLL(this, iter);
            IOObjectRelease(iter);
        }
        return true;
    }
    
};

IHIDListener* IHIDListenerNew(DeviceFinder& finder)
{
    return new HIDListenerIOKit(finder);
}

}
