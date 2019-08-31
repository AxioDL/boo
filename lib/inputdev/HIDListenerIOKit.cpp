#include "boo/inputdev/IHIDListener.hpp"
#include "boo/inputdev/DeviceFinder.hpp"
#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/hid/IOHIDLib.h>
#include <IOKit/hid/IOHIDDevicePlugin.h>
#include <IOKit/hid/IOHIDKeys.h>
#include <IOKit/IOKitLib.h>
#include <IOKit/usb/IOUSBLib.h>
#include <IOKit/IOCFPlugIn.h>
#include <sys/utsname.h>
#include "IOKitPointer.hpp"
#include "../CFPointer.hpp"

namespace boo {

/*
 * Reference: http://oroboro.com/usb-serial-number-osx/
 */

static bool getUSBStringDescriptor(const IUnknownPointer<IOUSBDeviceInterface182>& usbDevice, UInt8 idx, char* out) {
  UInt16 buffer[128];

  // wow... we're actually forced to make hard coded bus requests. Its like
  // hard disk programming in the 80's!
  IOUSBDevRequest request;

  request.bmRequestType = USBmakebmRequestType(kUSBIn, kUSBStandard, kUSBDevice);
  request.bRequest = kUSBRqGetDescriptor;
  request.wValue = (kUSBStringDesc << 8) | idx;
  request.wIndex = 0x409; // english
  request.wLength = sizeof(buffer);
  request.pData = buffer;

  kern_return_t err = usbDevice->DeviceRequest(usbDevice.storage(), &request);
  if (err != 0) {
    // the request failed... fairly uncommon for the USB disk driver, but not
    // so uncommon for other devices. This can also be less reliable if your
    // disk is mounted through an external USB hub. At this level we actually
    // have to worry about hardware issues like this.
    return false;
  }

  // we're mallocing this string just as an example. But you probably will want
  // to do something smarter, like pre-allocated buffers in the info class, or
  // use a string class.
  if (request.wLenDone == 0)
    return false;

  unsigned count = (request.wLenDone - 1) / 2;
  unsigned i;
  for (i = 0; i < count; ++i)
    out[i] = buffer[i + 1];
  out[i] = '\0';

  return true;
}

class HIDListenerIOKit : public IHIDListener {
  DeviceFinder& m_finder;

  CFRunLoopRef m_listenerRunLoop;
  IONotificationPortRef m_llPort;
  IOObjectPointer<io_iterator_t> m_llAddNotif, m_llRemoveNotif;
  IOObjectPointer<io_iterator_t> m_hidAddNotif, m_hidRemoveNotif;
  const char* m_usbClass;
  bool m_scanningEnabled;

  static void devicesConnectedUSBLL(HIDListenerIOKit* listener, io_iterator_t iterator) {
    while (IOObjectPointer<io_service_t> obj = IOIteratorNext(iterator)) {
      io_string_t devPath;
      if (IORegistryEntryGetPath(obj.get(), kIOServicePlane, devPath) != 0)
        continue;

      if (!listener->m_scanningEnabled || listener->m_finder._hasToken(devPath))
        continue;

      UInt16 vid, pid;
      char vstr[128] = {0};
      char pstr[128] = {0};
      {
        IOCFPluginPointer devServ;
        SInt32 score;
        IOReturn err;
        err = IOCreatePlugInInterfaceForService(obj.get(), kIOUSBDeviceUserClientTypeID, kIOCFPlugInInterfaceID,
                                                &devServ, &score);
        if (err != kIOReturnSuccess) {
          fmt::print(stderr, fmt("unable to open IOKit plugin interface\n"));
          continue;
        }

        IUnknownPointer<IOUSBDeviceInterface182> dev;
        err = devServ.As(&dev, kIOUSBDeviceInterfaceID182);
        if (err != kIOReturnSuccess) {
          fmt::print(stderr, fmt("unable to open IOKit device interface\n"));
          continue;
        }

        dev->GetDeviceVendor(dev.storage(), &vid);
        dev->GetDeviceProduct(dev.storage(), &pid);

        UInt8 vstridx, pstridx;
        dev->USBGetManufacturerStringIndex(dev.storage(), &vstridx);
        dev->USBGetProductStringIndex(dev.storage(), &pstridx);

        getUSBStringDescriptor(dev, vstridx, vstr);
        getUSBStringDescriptor(dev, pstridx, pstr);
      }

      listener->m_finder._insertToken(std::make_unique<DeviceToken>(DeviceType::USB, vid, pid, vstr, pstr, devPath));

      // fmt::print(fmt("ADDED {:08X} {}\n"), obj.get(), devPath);
    }
  }

  static void devicesDisconnectedUSBLL(HIDListenerIOKit* listener, io_iterator_t iterator) {
    if (CFRunLoopGetCurrent() != listener->m_listenerRunLoop) {
      CFRunLoopPerformBlock(listener->m_listenerRunLoop, kCFRunLoopDefaultMode,
                            ^{ devicesDisconnectedUSBLL(listener, iterator); });
      CFRunLoopWakeUp(listener->m_listenerRunLoop);
      return;
    }
    while (IOObjectPointer<io_service_t> obj = IOIteratorNext(iterator)) {
      io_string_t devPath;
      if (IORegistryEntryGetPath(obj.get(), kIOServicePlane, devPath) != 0)
        continue;
      listener->m_finder._removeToken(devPath);
      // fmt::print(fmt("REMOVED {:08X} {}\n"), obj.get(), devPath);
    }
  }

  static void devicesConnectedHID(HIDListenerIOKit* listener, io_iterator_t iterator) {
    while (IOObjectPointer<io_service_t> obj = IOIteratorNext(iterator)) {
      io_string_t devPath;
      if (IORegistryEntryGetPath(obj.get(), kIOServicePlane, devPath) != 0)
        continue;

      if (!listener->m_scanningEnabled || listener->m_finder._hasToken(devPath))
        continue;

      unsigned vidv, pidv;
      char vstr[128] = {0};
      char pstr[128] = {0};
      {
        IOCFPluginPointer devServ;
        SInt32 score;
        IOReturn err;
        err =
            IOCreatePlugInInterfaceForService(obj.get(), kIOHIDDeviceTypeID, kIOCFPlugInInterfaceID, &devServ, &score);
        if (err != kIOReturnSuccess) {
          fmt::print(stderr, fmt("unable to open IOKit plugin interface\n"));
          continue;
        }

        IUnknownPointer<IOHIDDeviceDeviceInterface> dev;
        err = devServ.As(&dev, kIOHIDDeviceDeviceInterfaceID);
        if (err != kIOReturnSuccess) {
          fmt::print(stderr, fmt("unable to open IOKit device interface\n"));
          continue;
        }

        /* Game controllers only */
        CFPointer<CFNumberRef> usagePage;
        dev->getProperty(dev.storage(), CFSTR(kIOHIDPrimaryUsagePageKey), (CFTypeRef*)&usagePage);
        CFPointer<CFNumberRef> usage;
        dev->getProperty(dev.storage(), CFSTR(kIOHIDPrimaryUsageKey), (CFTypeRef*)&usage);
        int usagePageV, usageV;
        CFNumberGetValue(usagePage.get(), kCFNumberIntType, &usagePageV);
        CFNumberGetValue(usage.get(), kCFNumberIntType, &usageV);
        if (usagePageV == kHIDPage_GenericDesktop) {
          if (usageV != kHIDUsage_GD_Joystick && usageV != kHIDUsage_GD_GamePad)
            continue;
        } else {
          continue;
        }

        CFPointer<CFNumberRef> vid, pid;
        dev->getProperty(dev.storage(), CFSTR(kIOHIDVendorIDKey), (CFTypeRef*)&vid);
        dev->getProperty(dev.storage(), CFSTR(kIOHIDProductIDKey), (CFTypeRef*)&pid);
        CFNumberGetValue(vid.get(), kCFNumberIntType, &vidv);
        CFNumberGetValue(pid.get(), kCFNumberIntType, &pidv);

        CFPointer<CFStringRef> vstridx, pstridx;
        dev->getProperty(dev.storage(), CFSTR(kIOHIDManufacturerKey), (CFTypeRef*)&vstridx);
        dev->getProperty(dev.storage(), CFSTR(kIOHIDProductKey), (CFTypeRef*)&pstridx);

        if (vstridx)
          CFStringGetCString(vstridx.get(), vstr, 128, kCFStringEncodingUTF8);
        if (pstridx)
          CFStringGetCString(pstridx.get(), pstr, 128, kCFStringEncodingUTF8);
      }

      listener->m_finder._insertToken(std::make_unique<DeviceToken>(DeviceType::HID, vidv, pidv, vstr, pstr, devPath));

      // fmt::print(fmt("ADDED {:08X} {}\n"), obj, devPath);
    }
  }

  static void devicesDisconnectedHID(HIDListenerIOKit* listener, io_iterator_t iterator) {
    if (CFRunLoopGetCurrent() != listener->m_listenerRunLoop) {
      CFRunLoopPerformBlock(listener->m_listenerRunLoop, kCFRunLoopDefaultMode,
                            ^{ devicesDisconnectedHID(listener, iterator); });
      CFRunLoopWakeUp(listener->m_listenerRunLoop);
      return;
    }
    while (IOObjectPointer<io_service_t> obj = IOIteratorNext(iterator)) {
      io_string_t devPath;
      if (IORegistryEntryGetPath(obj.get(), kIOServicePlane, devPath) != 0)
        continue;
      listener->m_finder._removeToken(devPath);
      // fmt::print(fmt("REMOVED {:08X} {}\n"), obj, devPath);
    }
  }

public:
  HIDListenerIOKit(DeviceFinder& finder) : m_finder(finder) {
    struct utsname kernInfo;
    uname(&kernInfo);
    int release = atoi(kernInfo.release);
    m_usbClass = release >= 15 ? "IOUSBHostDevice" : kIOUSBDeviceClassName;

    m_listenerRunLoop = CFRunLoopGetMain();
    m_llPort = IONotificationPortCreate(kIOMasterPortDefault);
    CFRunLoopSourceRef rlSrc = IONotificationPortGetRunLoopSource(m_llPort);
    CFRunLoopAddSource(m_listenerRunLoop, rlSrc, kCFRunLoopDefaultMode);
    m_scanningEnabled = true;

    /* Register HID Matcher */
    {
      CFMutableDictionaryRef matchDict = IOServiceMatching("IOHIDDevice");
      CFRetain(matchDict);

      kern_return_t hidRet =
          IOServiceAddMatchingNotification(m_llPort, kIOMatchedNotification, matchDict,
                                           (IOServiceMatchingCallback)devicesConnectedHID, this, &m_hidAddNotif);
      if (hidRet == kIOReturnSuccess)
        devicesConnectedHID(this, m_hidAddNotif.get());

      hidRet =
          IOServiceAddMatchingNotification(m_llPort, kIOTerminatedNotification, matchDict,
                                           (IOServiceMatchingCallback)devicesDisconnectedHID, this, &m_hidRemoveNotif);
      if (hidRet == kIOReturnSuccess)
        devicesDisconnectedHID(this, m_hidRemoveNotif.get());
    }

    /* Register Low-Level USB Matcher */
    {
      CFMutableDictionaryRef matchDict = IOServiceMatching(m_usbClass);
      CFRetain(matchDict);

      kern_return_t llRet =
          IOServiceAddMatchingNotification(m_llPort, kIOMatchedNotification, matchDict,
                                           (IOServiceMatchingCallback)devicesConnectedUSBLL, this, &m_llAddNotif);
      if (llRet == kIOReturnSuccess)
        devicesConnectedUSBLL(this, m_llAddNotif.get());

      llRet =
          IOServiceAddMatchingNotification(m_llPort, kIOTerminatedNotification, matchDict,
                                           (IOServiceMatchingCallback)devicesDisconnectedUSBLL, this, &m_llRemoveNotif);
      if (llRet == kIOReturnSuccess)
        devicesDisconnectedUSBLL(this, m_llRemoveNotif.get());
    }

    m_scanningEnabled = false;
  }

  ~HIDListenerIOKit() override {
    // CFRunLoopRemoveSource(m_listenerRunLoop, IONotificationPortGetRunLoopSource(m_llPort), kCFRunLoopDefaultMode);
    IONotificationPortDestroy(m_llPort);
  }

  /* Automatic device scanning */
  bool startScanning() override {
    m_scanningEnabled = true;
    return true;
  }
  bool stopScanning() override {
    m_scanningEnabled = false;
    return true;
  }

  /* Manual device scanning */
  bool scanNow() override {
    IOObjectPointer<io_iterator_t> iter;
    if (IOServiceGetMatchingServices(kIOMasterPortDefault, IOServiceMatching(m_usbClass), &iter) == kIOReturnSuccess) {
      devicesConnectedUSBLL(this, iter.get());
    }
    return true;
  }
};

std::unique_ptr<IHIDListener> IHIDListenerNew(DeviceFinder& finder) {
  return std::make_unique<HIDListenerIOKit>(finder);
}

} // namespace boo
