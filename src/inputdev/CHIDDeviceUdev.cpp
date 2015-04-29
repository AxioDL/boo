#include "IHIDDevice.hpp"
#include "inputdev/CDeviceToken.hpp"
#include "inputdev/CDeviceBase.hpp"
#include <thread>
#include <mutex>
#include <condition_variable>

#include <libudev.h>
#include <stropts.h>
#include <linux/usb/ch9.h>
#include <linux/usbdevice_fs.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <string.h>

namespace boo
{

udev* BooGetUdev();

#define MAX_REPORT_SIZE 65536

/*
 * Reference: http://tali.admingilde.org/linux-docbook/usb/ch07s06.html
 */

class CHIDDeviceUdev final : public IHIDDevice
{
    CDeviceToken& m_token;
    CDeviceBase& m_devImp;

    int m_devFd = 0;
    unsigned m_usbIntfInPipe = 0;
    unsigned m_usbIntfOutPipe = 0;
    bool m_runningTransferLoop = false;
    
    const std::string& m_devPath;
    std::mutex m_initMutex;
    std::condition_variable m_initCond;
    std::thread* m_thread;
    
    bool _sendInterruptTransfer(uint8_t pipe, const uint8_t* data, size_t length)
    {
        if (m_devFd)
        {
            usbdevfs_bulktransfer xfer =
            {
                m_usbIntfOutPipe | USB_DIR_OUT,
                (unsigned)length,
                0,
                (void*)data
            };
            int ret = ioctl(m_devFd, USBDEVFS_BULK, &xfer);
            if (ret != length)
                return false;
            return true;
        }
        return false;
    }
    
    size_t _receiveInterruptTransfer(uint8_t pipe, uint8_t* data, size_t length)
    {
        if (m_devFd)
        {
            usbdevfs_bulktransfer xfer =
            {
                m_usbIntfInPipe | USB_DIR_IN,
                (unsigned)length,
                0,
                data
            };
            return ioctl(m_devFd, USBDEVFS_BULK, &xfer);
        }
        return 0;
    }
    
    static void _threadProcLL(CHIDDeviceUdev* device)
    {
        unsigned i;
        std::unique_lock<std::mutex> lk(device->m_initMutex);
        udev_device* hidDev = udev_device_new_from_syspath(BooGetUdev(), device->m_devPath.c_str());

        /* Get the HID element's parent (USB interrupt transfer-interface) */
        udev_device* usbDev = udev_device_get_parent_with_subsystem_devtype(hidDev, "usb", "usb_device");
        const char* dp = udev_device_get_devnode(usbDev);
        device->m_devFd = open(dp, O_RDONLY);
        usb_device_descriptor devDesc = {0};
        read(device->m_devFd, &devDesc, 1);
        read(device->m_devFd, &devDesc.bDescriptorType, devDesc.bLength-1);
        if (devDesc.bNumConfigurations)
        {
            usb_config_descriptor confDesc = {0};
            read(device->m_devFd, &confDesc, 1);
            read(device->m_devFd, &confDesc.bDescriptorType, confDesc.bLength-1);
            if (confDesc.bNumInterfaces)
            {
                usb_interface_descriptor intfDesc = {0};
                read(device->m_devFd, &intfDesc, 1);
                read(device->m_devFd, &intfDesc.bDescriptorType, intfDesc.bLength-1);
                for (i=0 ; i<intfDesc.bNumEndpoints+1 ; ++i)
                {
                    usb_endpoint_descriptor endpDesc = {0};
                    read(device->m_devFd, &endpDesc, 1);
                    read(device->m_devFd, &endpDesc.bDescriptorType, endpDesc.bLength-1);
                    if ((endpDesc.bmAttributes & USB_ENDPOINT_XFERTYPE_MASK) == USB_ENDPOINT_XFER_INT)
                    {
                        if ((endpDesc.bEndpointAddress & USB_ENDPOINT_DIR_MASK) == USB_DIR_IN)
                            device->m_usbIntfInPipe = endpDesc.bEndpointAddress & USB_ENDPOINT_NUMBER_MASK;
                        else if ((endpDesc.bEndpointAddress & USB_ENDPOINT_DIR_MASK) == USB_DIR_OUT)
                            device->m_usbIntfOutPipe = endpDesc.bEndpointAddress & USB_ENDPOINT_NUMBER_MASK;
                    }
                }
            }
        }

        /* Request that kernel disconnects existing driver */
        usbdevfs_ioctl disconnectReq = {
            0,
            USBDEVFS_DISCONNECT,
            NULL
        };
        ioctl(device->m_devFd, USBDEVFS_IOCTL, &disconnectReq);
        
        /* Return control to main thread */
        device->m_runningTransferLoop = true;
        lk.unlock();
        device->m_initCond.notify_one();
        
        /* Start transfer loop */
        while (device->m_runningTransferLoop)
            device->m_devImp.transferCycle();
        device->m_devImp.finalCycle();

        /* Cleanup */
        close(device->m_devFd);
        device->m_devFd = NULL;
        udev_device_unref(hidDev);
        
    }
    
    static void _threadProcHL(CHIDDeviceUdev* device)
    {

    }
    
    void _deviceDisconnected()
    {
        m_runningTransferLoop = false;
    }
    
    bool _sendReport(const uint8_t* data, size_t length)
    {
        return false;
    }
    
public:
    
    CHIDDeviceUdev(CDeviceToken& token, CDeviceBase& devImp, bool lowLevel)
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
    
    ~CHIDDeviceUdev()
    {
        m_runningTransferLoop = false;
        m_thread->detach();
        delete m_thread;
    }
    

};

IHIDDevice* IHIDDeviceNew(CDeviceToken& token, CDeviceBase& devImp, bool lowLevel)
{
    return new CHIDDeviceUdev(token, devImp, lowLevel);
}

}
