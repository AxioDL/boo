#define _CRT_SECURE_NO_WARNINGS 1 /* STFU MSVC */
#include "IHIDDevice.hpp"
#include "boo/inputdev/DeviceToken.hpp"
#include "boo/inputdev/DeviceBase.hpp"
#include <thread>
#include <mutex>
#include <condition_variable>
#include <string.h>
#include <stdio.h>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN 1
#endif
#include <windows.h>
#include <winusb.h>
#include <usb100.h>
#include <Winusbio.h>

namespace boo
{

class HIDDeviceWinUSB final : public IHIDDevice
{
    DeviceToken& m_token;
    DeviceBase& m_devImp;

    HANDLE m_devHandle = 0;
    WINUSB_INTERFACE_HANDLE m_usbHandle = NULL;
    unsigned m_usbIntfInPipe = 0;
    unsigned m_usbIntfOutPipe = 0;
    bool m_runningTransferLoop = false;

    const std::string& m_devPath;
    std::mutex m_initMutex;
    std::condition_variable m_initCond;
    std::thread* m_thread;

    bool _sendUSBInterruptTransfer(const uint8_t* data, size_t length)
    {
        if (m_usbHandle)
        {
            ULONG lengthTransferred = 0;
            if (!WinUsb_WritePipe(m_usbHandle, m_usbIntfOutPipe, (PUCHAR)data,
                                  (ULONG)length, &lengthTransferred, NULL) ||
                lengthTransferred != length)
                return false;
            return true;
        }
        return false;
    }

    size_t _receiveUSBInterruptTransfer(uint8_t* data, size_t length)
    {
        if (m_usbHandle)
        {
            ULONG lengthTransferred = 0;
            if (!WinUsb_ReadPipe(m_usbHandle, m_usbIntfInPipe, (PUCHAR)data,
                                 (ULONG)length, &lengthTransferred, NULL))
                return 0;
            return lengthTransferred;
        }
        return 0;
    }

    static void _threadProcUSBLL(HIDDeviceWinUSB* device)
    {
        unsigned i;
        char errStr[256];
        std::unique_lock<std::mutex> lk(device->m_initMutex);

        /* POSIX.. who needs it?? -MS */
        device->m_devHandle = CreateFileA(device->m_devPath.c_str(),
                                          GENERIC_WRITE | GENERIC_READ,
                                          FILE_SHARE_WRITE | FILE_SHARE_READ,
                                          NULL,
                                          OPEN_EXISTING,
                                          FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OVERLAPPED,
                                          NULL);
        if (INVALID_HANDLE_VALUE == device->m_devHandle)
        {
            _snprintf(errStr, 256, "Unable to open %s@%s: %d\n",
                      device->m_token.getProductName().c_str(),
                      device->m_devPath.c_str(), GetLastError());
            device->m_devImp.deviceError(errStr);
            lk.unlock();
            device->m_initCond.notify_one();
            return;
        }

        if (!WinUsb_Initialize(device->m_devHandle, &device->m_usbHandle))
        {
            _snprintf(errStr, 256, "Unable to open %s@%s: %d\n",
                      device->m_token.getProductName().c_str(),
                      device->m_devPath.c_str(), GetLastError());
            device->m_devImp.deviceError(errStr);
            lk.unlock();
            device->m_initCond.notify_one();
            CloseHandle(device->m_devHandle);
            return;
        }

        /* Enumerate device pipes */
        USB_INTERFACE_DESCRIPTOR ifDesc = {0};
        if (!WinUsb_QueryInterfaceSettings(device->m_usbHandle, 0, &ifDesc))
        {
            _snprintf(errStr, 256, "Unable to open %s@%s: %d\n",
                      device->m_token.getProductName().c_str(),
                      device->m_devPath.c_str(), GetLastError());
            device->m_devImp.deviceError(errStr);
            lk.unlock();
            device->m_initCond.notify_one();
            CloseHandle(device->m_devHandle);
            return;
        }
        for (i=0 ; i<ifDesc.bNumEndpoints ; ++i)
        {
            WINUSB_PIPE_INFORMATION pipeDesc;
            WinUsb_QueryPipe(device->m_usbHandle, 0, i, &pipeDesc);
            if (pipeDesc.PipeType == UsbdPipeTypeInterrupt)
            {
                if (USB_ENDPOINT_DIRECTION_IN(pipeDesc.PipeId))
                    device->m_usbIntfInPipe = pipeDesc.PipeId;
                else
                    device->m_usbIntfOutPipe = pipeDesc.PipeId;
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
        WinUsb_Free(device->m_usbHandle);
        CloseHandle(device->m_devHandle);
        device->m_devHandle = 0;

    }

    static void _threadProcBTLL(HIDDeviceWinUSB* device)
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

    static void _threadProcHID(HIDDeviceWinUSB* device)
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

    HIDDeviceWinUSB(DeviceToken& token, DeviceBase& devImp)
    : m_token(token),
      m_devImp(devImp),
      m_devPath(token.getDevicePath())
    {
        devImp.m_hidDev = this;
        std::unique_lock<std::mutex> lk(m_initMutex);
        DeviceToken::TDeviceType dType = token.getDeviceType();
        if (dType == DeviceToken::DEVTYPE_USB)
            m_thread = new std::thread(_threadProcUSBLL, this);
        else if (dType == DeviceToken::DEVTYPE_BLUETOOTH)
            m_thread = new std::thread(_threadProcBTLL, this);
        else if (dType == DeviceToken::DEVTYPE_GENERICHID)
            m_thread = new std::thread(_threadProcHID, this);
        else
            throw std::runtime_error("invalid token supplied to device constructor");
        m_initCond.wait(lk);
    }

    ~HIDDeviceWinUSB()
    {
        m_runningTransferLoop = false;
        m_thread->join();
        delete m_thread;
    }


};

IHIDDevice* IHIDDeviceNew(DeviceToken& token, DeviceBase& devImp)
{
    return new HIDDeviceWinUSB(token, devImp);
}

}
