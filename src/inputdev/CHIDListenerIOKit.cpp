#include "IHIDListener.hpp"
#include "inputdev/CDeviceFinder.hpp"
#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/hid/IOHIDLib.h>
#include <IOKit/IOKitLib.h>
#include <IOKit/usb/IOUSBLib.h>
#include <IOKit/IOCFPlugIn.h>

/* Reference: http://oroboro.com/usb-serial-number-osx/ 
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

class CHIDListenerIOKit final : public IHIDListener
{
    CDeviceFinder& m_finder;
    
    CFRunLoopRef m_listenerRunLoop;
    IOHIDManagerRef m_hidManager;
    IONotificationPortRef m_llPort;
    bool m_scanningEnabled;
    
    static void deviceConnected(CHIDListenerIOKit* listener,
                                IOReturn,
                                void*,
                                IOHIDDeviceRef device)
    {
        if (!listener->m_scanningEnabled)
            return;
        io_string_t devPath;
        if (IORegistryEntryGetPath(IOHIDDeviceGetService(device), kIOServicePlane, devPath) != 0)
            return;
        if (listener->m_finder._hasToken(devPath))
            return;
        CFIndex vid, pid;
        CFNumberGetValue((CFNumberRef)IOHIDDeviceGetProperty(device, CFSTR(kIOHIDVendorIDKey)), kCFNumberCFIndexType, &vid);
        CFNumberGetValue((CFNumberRef)IOHIDDeviceGetProperty(device, CFSTR(kIOHIDProductIDKey)), kCFNumberCFIndexType, &pid);
        CFStringRef manuf = (CFStringRef)IOHIDDeviceGetProperty(device, CFSTR(kIOHIDManufacturerKey));
        CFStringRef product = (CFStringRef)IOHIDDeviceGetProperty(device, CFSTR(kIOHIDProductKey));
        listener->m_finder._insertToken(CDeviceToken(vid, pid,
                                                     CFStringGetCStringPtr(manuf, kCFStringEncodingUTF8),
                                                     CFStringGetCStringPtr(product, kCFStringEncodingUTF8),
                                                     devPath));
    }
    
    static void deviceDisconnected(CHIDListenerIOKit* listener,
                                   IOReturn ret,
                                   void* sender,
                                   IOHIDDeviceRef device)
    {
        if (CFRunLoopGetCurrent() != listener->m_listenerRunLoop)
        {
            CFRunLoopPerformBlock(listener->m_listenerRunLoop, kCFRunLoopDefaultMode, ^{
                deviceDisconnected(listener, ret, sender, device);
            });
            CFRunLoopWakeUp(listener->m_listenerRunLoop);
            return;
        }
        io_string_t devPath;
        if (IORegistryEntryGetPath(IOHIDDeviceGetService(device), kIOServicePlane, devPath) != 0)
            return;
        listener->m_finder._removeToken(devPath);
    }
    
    static void applyDevice(IOHIDDeviceRef device, CHIDListenerIOKit* listener)
    {
        io_string_t devPath;
        if (IORegistryEntryGetPath(IOHIDDeviceGetService(device), kIOServicePlane, devPath) != 0)
            return;
        if (listener->m_finder._hasToken(devPath))
            return;
        CFIndex vid, pid;
        CFNumberGetValue((CFNumberRef)IOHIDDeviceGetProperty(device, CFSTR(kIOHIDVendorIDKey)), kCFNumberCFIndexType, &vid);
        CFNumberGetValue((CFNumberRef)IOHIDDeviceGetProperty(device, CFSTR(kIOHIDProductIDKey)), kCFNumberCFIndexType, &pid);
        CFStringRef manuf = (CFStringRef)IOHIDDeviceGetProperty(device, CFSTR(kIOHIDManufacturerKey));
        CFStringRef product = (CFStringRef)IOHIDDeviceGetProperty(device, CFSTR(kIOHIDProductKey));
        listener->m_finder._insertToken(CDeviceToken(vid, pid,
                                                     CFStringGetCStringPtr(manuf, kCFStringEncodingUTF8),
                                                     CFStringGetCStringPtr(product, kCFStringEncodingUTF8),
                                                     devPath));
    }
    
    static void devicesConnectedLL(CHIDListenerIOKit* listener,
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
                throw std::runtime_error("unable to open IOKit plugin interface");
            
            IOUSBDeviceInterface182 **dev;
            err = (*devServ)->QueryInterface(devServ,
                                             CFUUIDGetUUIDBytes(kIOUSBDeviceInterfaceID182),
                                             (LPVOID*)&dev);
            if (err != kIOReturnSuccess)
                throw std::runtime_error("unable to open IOKit device interface");
            
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

            listener->m_finder._insertToken(CDeviceToken(vid, pid, vstr, pstr, devPath));

            //printf("ADDED %08X %s\n", obj, devPath);
            (*dev)->Release(dev);
            IODestroyPlugInInterface(devServ);
            IOObjectRelease(obj);
        }
        
    }
    
    static void devicesDisconnectedLL(CHIDListenerIOKit* listener,
                                      io_iterator_t      iterator)
    {
        if (CFRunLoopGetCurrent() != listener->m_listenerRunLoop)
        {
            CFRunLoopPerformBlock(listener->m_listenerRunLoop, kCFRunLoopDefaultMode, ^{
                devicesDisconnectedLL(listener, iterator);
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
    CHIDListenerIOKit(CDeviceFinder& finder)
    : m_finder(finder)
    {
        
        /* Register HID Manager */
        m_hidManager = IOHIDManagerCreate(kCFAllocatorDefault, kIOHIDManagerOptionNone);
        IOHIDManagerSetDeviceMatching(m_hidManager, NULL);
        IOHIDManagerRegisterDeviceMatchingCallback(m_hidManager, (IOHIDDeviceCallback)deviceConnected, this);
        IOHIDManagerRegisterDeviceRemovalCallback(m_hidManager, (IOHIDDeviceCallback)deviceDisconnected, this);
        m_listenerRunLoop = CFRunLoopGetCurrent();
        IOHIDManagerScheduleWithRunLoop(m_hidManager, m_listenerRunLoop, kCFRunLoopDefaultMode);
        IOReturn ret = IOHIDManagerOpen(m_hidManager, kIOHIDManagerOptionNone);
        if (ret != kIOReturnSuccess)
            throw std::runtime_error("error establishing IOHIDManager");
        
        /* Initial HID Device Add */
        m_scanningEnabled = true;
        CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0, false);
        
        /* Register Low-Level Matcher */
        m_llPort = IONotificationPortCreate(kIOMasterPortDefault);
        CFRunLoopAddSource(m_listenerRunLoop, IONotificationPortGetRunLoopSource(m_llPort), kCFRunLoopDefaultMode);
        
        CFMutableDictionaryRef matchDict = IOServiceMatching(kIOUSBDeviceClassName);
        CFIndex nintendoVid = VID_NINTENDO;
        CFIndex smashPid = PID_SMASH_ADAPTER;
        CFNumberRef nintendoVidNum = CFNumberCreate(kCFAllocatorDefault, kCFNumberCFIndexType, &nintendoVid);
        CFNumberRef smashPidNum = CFNumberCreate(kCFAllocatorDefault, kCFNumberCFIndexType, &smashPid);
        CFDictionaryAddValue(matchDict, CFSTR(kUSBVendorID), nintendoVidNum);
        CFDictionaryAddValue(matchDict, CFSTR(kUSBProductID), smashPidNum);
        CFRelease(nintendoVidNum);
        CFRelease(smashPidNum);
        CFRetain(matchDict);
        
        io_iterator_t initialIt;
        kern_return_t llRet =
        IOServiceAddMatchingNotification(m_llPort, kIOMatchedNotification, matchDict,
                                         (IOServiceMatchingCallback)devicesConnectedLL, this, &initialIt);
        if (llRet == 0)
            devicesConnectedLL(this, initialIt);
        
        llRet =
        IOServiceAddMatchingNotification(m_llPort, kIOTerminatedNotification, matchDict,
                                         (IOServiceMatchingCallback)devicesDisconnectedLL, this, &initialIt);
        if (llRet == 0)
            devicesDisconnectedLL(this, initialIt);
        
        m_scanningEnabled = false;
        
    }
    
    ~CHIDListenerIOKit()
    {
        IOHIDManagerUnscheduleFromRunLoop(m_hidManager, m_listenerRunLoop, kCFRunLoopDefaultMode);
        IOHIDManagerClose(m_hidManager, kIOHIDManagerOptionNone);
        CFRelease(m_hidManager);
        CFRunLoopRemoveSource(m_listenerRunLoop, IONotificationPortGetRunLoopSource(m_llPort), kCFRunLoopDefaultMode);
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
        CFSetRef devs = IOHIDManagerCopyDevices(m_hidManager);
        m_finder.m_tokensLock.lock();
        CFSetApplyFunction(devs, (CFSetApplierFunction)applyDevice, this);
        m_finder.m_tokensLock.unlock();
        CFRelease(devs);
        return true;
    }
    
};

IHIDListener* IHIDListenerNew(CDeviceFinder& finder)
{
    return new CHIDListenerIOKit(finder);
}

