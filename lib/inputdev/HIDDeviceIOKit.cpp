#include "lib/inputdev/IHIDDevice.hpp"

#include <thread>

#include "lib/inputdev/IOKitPointer.hpp"

#include <IOKit/hid/IOHIDLib.h>
#include <IOKit/hid/IOHIDDevicePlugin.h>
#include <IOKit/usb/IOUSBLib.h>

namespace boo {

class HIDDeviceIOKit : public IHIDDevice {
  DeviceToken& m_token;
  std::shared_ptr<DeviceBase> m_devImp;

  IUnknownPointer<IOUSBInterfaceInterface> m_usbIntf;
  uint8_t m_usbIntfInPipe = 0;
  uint8_t m_usbIntfOutPipe = 0;
  CFPointer<IOHIDDeviceRef> m_hidIntf;
  bool m_runningTransferLoop = false;
  bool m_isBt = false;

  std::string_view m_devPath;
  std::mutex m_initMutex;
  std::condition_variable m_initCond;
  std::thread m_thread;

  bool _sendUSBInterruptTransfer(const uint8_t* data, size_t length) override {
    if (m_usbIntf) {
      IOReturn res = m_usbIntf->WritePipe(m_usbIntf.storage(), m_usbIntfOutPipe, (void*)data, length);
      return res == kIOReturnSuccess;
    }
    return false;
  }

  size_t _receiveUSBInterruptTransfer(uint8_t* data, size_t length) override {
    if (m_usbIntf) {
      UInt32 readSize = length;
      IOReturn res = m_usbIntf->ReadPipe(m_usbIntf.storage(), m_usbIntfInPipe, data, &readSize);
      if (res != kIOReturnSuccess)
        return 0;
      return readSize;
    }
    return 0;
  }

  std::vector<uint8_t> _getReportDescriptor() override {
    if (m_hidIntf) {
      if (CFTypeRef desc = IOHIDDeviceGetProperty(m_hidIntf.get(), CFSTR(kIOHIDReportDescriptorKey))) {
        CFIndex len = CFDataGetLength(CFDataRef(desc));
        std::vector<uint8_t> ret(len, '\0');
        CFDataGetBytes(CFDataRef(desc), CFRangeMake(0, len), &ret[0]);
        return ret;
      }
    }
    return {};
  }

  bool _sendHIDReport(const uint8_t* data, size_t length, HIDReportType tp, uint32_t message) override {
    /* HACK: A bug in IOBluetoothGamepadHIDDriver prevents raw output report transmission
     * USB driver appears to work correctly */
    if (m_hidIntf && !m_isBt) {
      IOReturn res = IOHIDDeviceSetReport(m_hidIntf.get(), IOHIDReportType(tp), message, data, length);
      return res == kIOReturnSuccess;
    }
    return false;
  }

  size_t _receiveHIDReport(uint8_t* data, size_t length, HIDReportType tp, uint32_t message) override {
    if (m_hidIntf) {
      CFIndex readSize = length;
      IOReturn res = IOHIDDeviceGetReport(m_hidIntf.get(), IOHIDReportType(tp), message, data, &readSize);
      if (res != kIOReturnSuccess)
        return 0;
      return readSize;
    }
    return 0;
  }

  static void _threadProcUSBLL(std::shared_ptr<HIDDeviceIOKit> device) {
    pthread_setname_np(fmt::format(fmt("{} Transfer Thread"), device->m_token.getProductName()));
    std::unique_lock<std::mutex> lk(device->m_initMutex);

    /* Get the HID element's parent (USB interrupt transfer-interface) */
    IOObjectPointer<io_iterator_t> devIter;
    IOObjectPointer<io_registry_entry_t> devEntry =
        IORegistryEntryFromPath(kIOMasterPortDefault, device->m_devPath.data());
    IOObjectPointer<io_object_t> interfaceEntry;
    IORegistryEntryGetChildIterator(devEntry.get(), kIOServicePlane, &devIter);
    while (IOObjectPointer<io_service_t> obj = IOIteratorNext(devIter.get())) {
      if (IOObjectConformsTo(obj.get(), kIOUSBInterfaceClassName)) {
        interfaceEntry = obj;
        break;
      }
    }
    if (!interfaceEntry) {
      device->m_devImp->deviceError(fmt::format(fmt("Unable to find interface for {}@{}\n"),
                                                device->m_token.getProductName(), device->m_devPath).c_str());
      lk.unlock();
      device->m_initCond.notify_one();
      return;
    }

    /* IOKit Plugin COM interface (WTF Apple???) */
    IOCFPluginPointer iodev;
    SInt32 score;
    IOReturn err;
    err = IOCreatePlugInInterfaceForService(interfaceEntry.get(), kIOUSBInterfaceUserClientTypeID,
                                            kIOCFPlugInInterfaceID, &iodev, &score);
    if (err) {
      device->m_devImp->deviceError(fmt::format(fmt("Unable to open {}@{}\n"),
                                                device->m_token.getProductName(), device->m_devPath).c_str());
      lk.unlock();
      device->m_initCond.notify_one();
      return;
    }

    /* USB interface function-pointer table */
    IUnknownPointer<IOUSBInterfaceInterface> intf;
    err = iodev.As(&intf, kIOUSBInterfaceInterfaceID);
    if (err) {
      device->m_devImp->deviceError(fmt::format(fmt("Unable to open {}@{}\n"),
                                                device->m_token.getProductName(), device->m_devPath).c_str());
      lk.unlock();
      device->m_initCond.notify_one();
      return;
    }

    /* Obtain exclusive lock on device */
    device->m_usbIntf = intf;
    err = intf->USBInterfaceOpen(intf.storage());
    if (err != kIOReturnSuccess) {
      if (err == kIOReturnExclusiveAccess) {
        device->m_devImp->deviceError(fmt::format(fmt("Unable to open {}@{}: someone else using it\n"),
                                                  device->m_token.getProductName(), device->m_devPath).c_str());
      } else {
        device->m_devImp->deviceError(fmt::format(fmt("Unable to open {}@{}\n"),
                                                  device->m_token.getProductName(), device->m_devPath).c_str());
      }
      lk.unlock();
      device->m_initCond.notify_one();
      return;
    }

    /* Determine pipe indices for interrupt I/O */
    UInt8 numEndpoints = 0;
    err = intf->GetNumEndpoints(intf.storage(), &numEndpoints);
    for (int i = 1; i < numEndpoints + 1; ++i) {
      UInt8 dir, num, tType, interval;
      UInt16 mPacketSz;
      err = intf->GetPipeProperties(intf.storage(), i, &dir, &num, &tType, &mPacketSz, &interval);
      if (tType == kUSBInterrupt) {
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
    device->m_devImp->initialCycle();
    while (device->m_runningTransferLoop)
      device->m_devImp->transferCycle();
    device->m_devImp->finalCycle();

    /* Cleanup */
    intf->USBInterfaceClose(intf.storage());
    device->m_usbIntf = nullptr;
  }

  static void _threadProcBTLL(std::shared_ptr<HIDDeviceIOKit> device) {
    std::unique_lock<std::mutex> lk(device->m_initMutex);

    /* Return control to main thread */
    device->m_runningTransferLoop = true;
    lk.unlock();
    device->m_initCond.notify_one();

    /* Start transfer loop */
    device->m_devImp->initialCycle();
    while (device->m_runningTransferLoop)
      device->m_devImp->transferCycle();
    device->m_devImp->finalCycle();
  }

  static void _hidRemoveCb(void* _Nullable context, IOReturn result, void* _Nullable sender) {
    reinterpret_cast<HIDDeviceIOKit*>(context)->m_runningTransferLoop = false;
  }

  static void _hidReportCb(void* _Nullable context, IOReturn, void* _Nullable, IOHIDReportType type, uint32_t reportID,
                           uint8_t* report, CFIndex reportLength) {
    reinterpret_cast<DeviceBase*>(context)->receivedHIDReport(report, reportLength, HIDReportType(type), reportID);
  }

  static void _threadProcHID(std::shared_ptr<HIDDeviceIOKit> device) {
    pthread_setname_np(fmt::format(fmt("{} Transfer Thread"), device->m_token.getProductName());
    std::unique_lock<std::mutex> lk(device->m_initMutex);

    /* Get the HID element's object (HID device interface) */
    IOObjectPointer<io_service_t> interfaceEntry =
        IORegistryEntryFromPath(kIOMasterPortDefault, device->m_devPath.data());
    if (!IOObjectConformsTo(interfaceEntry.get(), "IOHIDDevice")) {
      device->m_devImp->deviceError(fmt::format(fmt("Unable to find interface for {}@{}\n"),
                                                device->m_token.getProductName(), device->m_devPath);
      lk.unlock();
      device->m_initCond.notify_one();
      return;
    }

    device->m_hidIntf = IOHIDDeviceCreate(nullptr, interfaceEntry.get());
    if (!device->m_hidIntf) {
      device->m_devImp->deviceError(fmt::format(fmt("Unable to open {}@{}\n"),
                                                device->m_token.getProductName(), device->m_devPath).c_str());
      lk.unlock();
      device->m_initCond.notify_one();
      return;
    }

    /* Open device */
    IOReturn err = IOHIDDeviceOpen(device->m_hidIntf.get(), kIOHIDOptionsTypeNone);
    if (err != kIOReturnSuccess) {
      if (err == kIOReturnExclusiveAccess) {
        device->m_devImp->deviceError(fmt::format(fmt("Unable to open {}@{}: someone else using it\n"),
                                                  device->m_token.getProductName(), device->m_devPath).c_str());
      } else {
        device->m_devImp->deviceError(fmt::format(fmt("Unable to open {}@{}\n"),
                                                  device->m_token.getProductName(), device->m_devPath).c_str());
      }
      lk.unlock();
      device->m_initCond.notify_one();
      return;
    }

    /* Register removal callback */
    IOHIDDeviceRegisterRemovalCallback(device->m_hidIntf.get(), _hidRemoveCb, device.get());

    /* Make note if device uses bluetooth driver */
    if (CFTypeRef transport = IOHIDDeviceGetProperty(device->m_hidIntf.get(), CFSTR(kIOHIDTransportKey)))
      device->m_isBt =
          CFStringCompare(CFStringRef(transport), CFSTR(kIOHIDTransportBluetoothValue), 0) == kCFCompareEqualTo;

    /* Register input buffer */
    std::unique_ptr<uint8_t[]> buffer;
    int bufSize = 0;
    if (CFTypeRef maxSize = IOHIDDeviceGetProperty(device->m_hidIntf.get(), CFSTR(kIOHIDMaxInputReportSizeKey)))
      CFNumberGetValue(CFNumberRef(maxSize), kCFNumberIntType, &bufSize);
    if (bufSize) {
      buffer = std::unique_ptr<uint8_t[]>(new uint8_t[bufSize]);
      IOHIDDeviceRegisterInputReportCallback(device->m_hidIntf.get(), buffer.get(), bufSize, _hidReportCb,
                                             device->m_devImp.get());
      IOHIDDeviceScheduleWithRunLoop(device->m_hidIntf.get(), CFRunLoopGetCurrent(), kCFRunLoopDefaultMode);
    }

    /* Return control to main thread */
    device->m_runningTransferLoop = true;
    lk.unlock();
    device->m_initCond.notify_one();

    /* Start transfer loop */
    device->m_devImp->initialCycle();
    while (device->m_runningTransferLoop) {
      CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0.010, true);
      if (device->m_runningTransferLoop)
        device->m_devImp->transferCycle();
    }
    device->m_devImp->finalCycle();

    /* Cleanup */
    IOHIDDeviceClose(device->m_hidIntf.get(), kIOHIDOptionsTypeNone);
    device->m_hidIntf.reset();
  }

  void _deviceDisconnected() override { m_runningTransferLoop = false; }

public:
  HIDDeviceIOKit(DeviceToken& token, const std::shared_ptr<DeviceBase>& devImp)
  : m_token(token), m_devImp(devImp), m_devPath(token.getDevicePath()) {}

  void _startThread() override {
    std::unique_lock<std::mutex> lk(m_initMutex);
    DeviceType dType = m_token.getDeviceType();
    if (dType == DeviceType::USB)
      m_thread = std::thread(_threadProcUSBLL, std::static_pointer_cast<HIDDeviceIOKit>(shared_from_this()));
    else if (dType == DeviceType::Bluetooth)
      m_thread = std::thread(_threadProcBTLL, std::static_pointer_cast<HIDDeviceIOKit>(shared_from_this()));
    else if (dType == DeviceType::HID)
      m_thread = std::thread(_threadProcHID, std::static_pointer_cast<HIDDeviceIOKit>(shared_from_this()));
    else {
      fmt::print(stderr, fmt("invalid token supplied to device constructor\n"));
      return;
    }
    m_initCond.wait(lk);
  }

  ~HIDDeviceIOKit() override {
    m_runningTransferLoop = false;
    if (m_thread.joinable())
      m_thread.detach();
  }
};

std::shared_ptr<IHIDDevice> IHIDDeviceNew(DeviceToken& token, const std::shared_ptr<DeviceBase>& devImp) {
  return std::make_shared<HIDDeviceIOKit>(token, devImp);
}

} // namespace boo
