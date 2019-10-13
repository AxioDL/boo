#include "lib/inputdev/IHIDDevice.hpp"

#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <thread>

#include "boo/inputdev/DeviceToken.hpp"
#include "boo/inputdev/DeviceBase.hpp"
#include "boo/inputdev/HIDParser.hpp"

#include <fcntl.h>
#include <libudev.h>
#include <linux/usb/ch9.h>
#include <linux/usbdevice_fs.h>
#include <linux/input.h>
#include <linux/hidraw.h>
#include <sys/ioctl.h>
#include <unistd.h>

namespace boo {

udev* GetUdev();

/*
 * Reference: http://tali.admingilde.org/linux-docbook/usb/ch07s06.html
 */

class HIDDeviceUdev final : public IHIDDevice {
  DeviceToken& m_token;
  std::shared_ptr<DeviceBase> m_devImp;

  int m_devFd = 0;
  unsigned m_usbIntfInPipe = 0;
  unsigned m_usbIntfOutPipe = 0;
  bool m_runningTransferLoop = false;

  std::string_view m_devPath;
  std::mutex m_initMutex;
  std::condition_variable m_initCond;
  std::thread m_thread;

  bool _sendUSBInterruptTransfer(const uint8_t* data, size_t length) override {
    if (m_devFd) {
      usbdevfs_bulktransfer xfer = {m_usbIntfOutPipe | USB_DIR_OUT, (unsigned)length, 30, (void*)data};
      int ret = ioctl(m_devFd, USBDEVFS_BULK, &xfer);
      if (ret != (int)length)
        return false;
      return true;
    }
    return false;
  }

  size_t _receiveUSBInterruptTransfer(uint8_t* data, size_t length) override {
    if (m_devFd) {
      usbdevfs_bulktransfer xfer = {m_usbIntfInPipe | USB_DIR_IN, (unsigned)length, 30, data};
      return ioctl(m_devFd, USBDEVFS_BULK, &xfer);
    }
    return 0;
  }

  static void _threadProcUSBLL(std::shared_ptr<HIDDeviceUdev> device) {
    int i;
    std::unique_lock<std::mutex> lk(device->m_initMutex);
    udev_device* udevDev = udev_device_new_from_syspath(GetUdev(), device->m_devPath.data());

    /* Get device file */
    const char* dp = udev_device_get_devnode(udevDev);
    int fd = open(dp, O_RDWR);
    if (fd < 0) {
      device->m_devImp->deviceError(fmt("Unable to open {}@{}: {}\n"),
        device->m_token.getProductName(), dp, strerror(errno));
      lk.unlock();
      device->m_initCond.notify_one();
      udev_device_unref(udevDev);
      return;
    }
    device->m_devFd = fd;
    usb_device_descriptor devDesc = {};
    read(fd, &devDesc, 1);
    read(fd, &devDesc.bDescriptorType, devDesc.bLength - 1);
    if (devDesc.bNumConfigurations) {
      usb_config_descriptor confDesc = {};
      read(fd, &confDesc, 1);
      read(fd, &confDesc.bDescriptorType, confDesc.bLength - 1);
      if (confDesc.bNumInterfaces) {
        usb_interface_descriptor intfDesc = {};
        read(fd, &intfDesc, 1);
        read(fd, &intfDesc.bDescriptorType, intfDesc.bLength - 1);
        for (i = 0; i < intfDesc.bNumEndpoints + 1; ++i) {
          usb_endpoint_descriptor endpDesc = {};
          read(fd, &endpDesc, 1);
          read(fd, &endpDesc.bDescriptorType, endpDesc.bLength - 1);
          if ((endpDesc.bmAttributes & USB_ENDPOINT_XFERTYPE_MASK) == USB_ENDPOINT_XFER_INT) {
            if ((endpDesc.bEndpointAddress & USB_ENDPOINT_DIR_MASK) == USB_DIR_IN)
              device->m_usbIntfInPipe = endpDesc.bEndpointAddress & USB_ENDPOINT_NUMBER_MASK;
            else if ((endpDesc.bEndpointAddress & USB_ENDPOINT_DIR_MASK) == USB_DIR_OUT)
              device->m_usbIntfOutPipe = endpDesc.bEndpointAddress & USB_ENDPOINT_NUMBER_MASK;
          }
        }
      }
    }

    /* Request that kernel disconnects existing driver */
    usbdevfs_ioctl disconnectReq = {0, USBDEVFS_DISCONNECT, nullptr};
    ioctl(fd, USBDEVFS_IOCTL, &disconnectReq);

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
    close(fd);
    device->m_devFd = 0;
    udev_device_unref(udevDev);
  }

  static void _threadProcBTLL(std::shared_ptr<HIDDeviceUdev> device) {
    std::unique_lock<std::mutex> lk(device->m_initMutex);
    udev_device* udevDev = udev_device_new_from_syspath(GetUdev(), device->m_devPath.data());

    /* Return control to main thread */
    device->m_runningTransferLoop = true;
    lk.unlock();
    device->m_initCond.notify_one();

    /* Start transfer loop */
    device->m_devImp->initialCycle();
    while (device->m_runningTransferLoop)
      device->m_devImp->transferCycle();
    device->m_devImp->finalCycle();

    udev_device_unref(udevDev);
  }

  static void _threadProcHID(std::shared_ptr<HIDDeviceUdev> device) {
    std::unique_lock<std::mutex> lk(device->m_initMutex);
    udev_device* udevDev = udev_device_new_from_syspath(GetUdev(), device->m_devPath.data());

    /* Get device file */
    const char* dp = udev_device_get_devnode(udevDev);
    int fd = open(dp, O_RDWR | O_NONBLOCK);
    if (fd < 0) {
      device->m_devImp->deviceError(fmt("Unable to open {}@{}: {}\n"),
                                    device->m_token.getProductName(), dp, strerror(errno));
      lk.unlock();
      device->m_initCond.notify_one();
      udev_device_unref(udevDev);
      return;
    }
    device->m_devFd = fd;

    /* Return control to main thread */
    device->m_runningTransferLoop = true;
    lk.unlock();
    device->m_initCond.notify_one();

    /* Report descriptor size */
    int reportDescSize;
    if (ioctl(fd, HIDIOCGRDESCSIZE, &reportDescSize) == -1) {
      device->m_devImp->deviceError(fmt("Unable to ioctl(HIDIOCGRDESCSIZE) {}@{}: {}\n"),
                                    device->m_token.getProductName(), dp, strerror(errno));
      close(fd);
      return;
    }

    /* Get report descriptor */
    hidraw_report_descriptor reportDesc;
    reportDesc.size = reportDescSize;
    if (ioctl(fd, HIDIOCGRDESC, &reportDesc) == -1) {
      device->m_devImp->deviceError(fmt("Unable to ioctl(HIDIOCGRDESC) {}@{}: {}\n"),
                                    device->m_token.getProductName(), dp, strerror(errno));
      close(fd);
      return;
    }
    size_t readSz = HIDParser::CalculateMaxInputReportSize(reportDesc.value, reportDesc.size);
    std::unique_ptr<uint8_t[]> readBuf(new uint8_t[readSz]);

    /* Start transfer loop */
    device->m_devImp->initialCycle();
    while (device->m_runningTransferLoop) {
      fd_set readset;
      FD_ZERO(&readset);
      FD_SET(fd, &readset);
      struct timeval timeout = {0, 10000};
      if (select(fd + 1, &readset, nullptr, nullptr, &timeout) > 0) {
        while (true) {
          ssize_t sz = read(fd, readBuf.get(), readSz);
          if (sz < 0)
            break;
          device->m_devImp->receivedHIDReport(readBuf.get(), sz, HIDReportType::Input, readBuf[0]);
        }
      }
      if (device->m_runningTransferLoop)
        device->m_devImp->transferCycle();
    }
    device->m_devImp->finalCycle();

    /* Cleanup */
    close(fd);
    device->m_devFd = 0;
    udev_device_unref(udevDev);
  }

  void _deviceDisconnected() override { m_runningTransferLoop = false; }

  std::vector<uint8_t> _getReportDescriptor() override {
    /* Report descriptor size */
    int reportDescSize;
    if (ioctl(m_devFd, HIDIOCGRDESCSIZE, &reportDescSize) == -1)
      return {};

    /* Get report descriptor */
    hidraw_report_descriptor reportDesc;
    reportDesc.size = reportDescSize;
    if (ioctl(m_devFd, HIDIOCGRDESC, &reportDesc) == -1)
      return {};
    std::vector<uint8_t> ret(reportDesc.size, '\0');
    memmove(ret.data(), reportDesc.value, reportDesc.size);
    return ret;
  }

  bool _sendHIDReport(const uint8_t* data, size_t length, HIDReportType tp, uint32_t message) override {
    if (m_devFd) {
      if (tp == HIDReportType::Feature) {
        int ret = ioctl(m_devFd, HIDIOCSFEATURE(length), data);
        if (ret < 0)
          return false;
        return true;
      } else if (tp == HIDReportType::Output) {
        ssize_t ret = write(m_devFd, data, length);
        if (ret < 0)
          return false;
        return true;
      }
    }
    return false;
  }

  size_t _receiveHIDReport(uint8_t* data, size_t length, HIDReportType tp, uint32_t message) override {
    if (m_devFd) {
      if (tp == HIDReportType::Feature) {
        data[0] = message;
        int ret = ioctl(m_devFd, HIDIOCGFEATURE(length), data);
        if (ret < 0)
          return 0;
        return length;
      }
    }
    return 0;
  }

public:
  HIDDeviceUdev(DeviceToken& token, const std::shared_ptr<DeviceBase>& devImp)
  : m_token(token), m_devImp(devImp), m_devPath(token.getDevicePath()) {}

  void _startThread() override {
    std::unique_lock<std::mutex> lk(m_initMutex);
    DeviceType dType = m_token.getDeviceType();
    if (dType == DeviceType::USB)
      m_thread = std::thread(_threadProcUSBLL, std::static_pointer_cast<HIDDeviceUdev>(shared_from_this()));
    else if (dType == DeviceType::Bluetooth)
      m_thread = std::thread(_threadProcBTLL, std::static_pointer_cast<HIDDeviceUdev>(shared_from_this()));
    else if (dType == DeviceType::HID)
      m_thread = std::thread(_threadProcHID, std::static_pointer_cast<HIDDeviceUdev>(shared_from_this()));
    else {
      fmt::print(stderr, fmt("invalid token supplied to device constructor"));
      abort();
    }
    m_initCond.wait(lk);
  }

  ~HIDDeviceUdev() override {
    m_runningTransferLoop = false;
    if (m_thread.joinable())
      m_thread.detach();
  }
};

std::shared_ptr<IHIDDevice> IHIDDeviceNew(DeviceToken& token, const std::shared_ptr<DeviceBase>& devImp) {
  return std::make_shared<HIDDeviceUdev>(token, devImp);
}

} // namespace boo
