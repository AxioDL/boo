#define _CRT_SECURE_NO_WARNINGS 1 /* STFU MSVC */
#include "IHIDDevice.hpp"
#include "inputdev/CDeviceToken.hpp"
#include "inputdev/CDeviceBase.hpp"
#include <thread>
#include <mutex>
#include <condition_variable>
#include <string.h>
#include <stdio.h>

#define _WIN32_LEAN_AND_MEAN 1
#include <windows.h>
#include <winusb.h>

namespace boo
{

class CHIDDeviceWinUSB final : public IHIDDevice
{
    CDeviceToken& m_token;
    CDeviceBase& m_devImp;

    HANDLE m_devHandle = 0;
    WINUSB_INTERFACE_HANDLE m_usbHandle = NULL;
    unsigned m_usbIntfInPipe = 0;
    unsigned m_usbIntfOutPipe = 0;
    bool m_runningTransferLoop = false;

    const std::string& m_devPath;
    std::mutex m_initMutex;
    std::condition_variable m_initCond;
    std::thread* m_thread;

    bool _sendUSBInterruptTransfer(uint8_t pipe, const uint8_t* data, size_t length)
    {
        if (m_usbHandle)
        {
            ULONG lengthTransferred = 0;
            if (!WinUsb_WritePipe(m_usbHandle, m_usbIntfOutPipe, (PUCHAR)data,
                                  (ULONG)length, &lengthTransferred, NULL)
                || lengthTransferred != length)
                return false;
            return true;
        }
        return false;
    }

    size_t _receiveUSBInterruptTransfer(uint8_t pipe, uint8_t* data, size_t length)
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

    static void _threadProcUSBLL(CHIDDeviceWinUSB* device)
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
        if (INVALID_HANDLE_VALUE == device->m_devHandle) {
            _snprintf(errStr, 256, "Unable to open %s@%s: %d\n",
                      device->m_token.getProductName().c_str(),
                      device->m_devPath, GetLastError());
            device->m_devImp.deviceError(errStr);
            lk.unlock();
            device->m_initCond.notify_one();
            return;
        }

        if (!WinUsb_Initialize(device->m_devHandle, &device->m_usbHandle)) {
            _snprintf(errStr, 256, "Unable to open %s@%s: %d\n",
                      device->m_token.getProductName().c_str(),
                      device->m_devPath, GetLastError());
            device->m_devImp.deviceError(errStr);
            lk.unlock();
            device->m_initCond.notify_one();
            CloseHandle(device->m_devHandle);
            return;
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

    static void _threadProcBTLL(CHIDDeviceWinUSB* device)
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

    static void _threadProcHID(CHIDDeviceWinUSB* device)
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

    bool _sendHIDReport(const uint8_t* data, size_t length)
    {
        return false;
    }

public:

    CHIDDeviceWinUSB(CDeviceToken& token, CDeviceBase& devImp)
    : m_token(token),
      m_devImp(devImp),
      m_devPath(token.getDevicePath())
    {
        devImp.m_hidDev = this;
        std::unique_lock<std::mutex> lk(m_initMutex);
        CDeviceToken::TDeviceType dType = token.getDeviceType();
        if (dType == CDeviceToken::DEVTYPE_USB)
            m_thread = new std::thread(_threadProcUSBLL, this);
        else if (dType == CDeviceToken::DEVTYPE_BLUETOOTH)
            m_thread = new std::thread(_threadProcBTLL, this);
        else if (dType == CDeviceToken::DEVTYPE_GENERICHID)
            m_thread = new std::thread(_threadProcHID, this);
        else
            throw std::runtime_error("invalid token supplied to device constructor");
        m_initCond.wait(lk);
    }

    ~CHIDDeviceWinUSB()
    {
        m_runningTransferLoop = false;
        m_thread->join();
        delete m_thread;
    }


};

IHIDDevice* IHIDDeviceNew(CDeviceToken& token, CDeviceBase& devImp)
{
    return new CHIDDeviceWinUSB(token, devImp);
}

}
