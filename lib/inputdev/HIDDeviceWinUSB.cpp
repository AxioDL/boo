#define _CRT_SECURE_NO_WARNINGS 1 /* STFU MSVC */
#include "IHIDDevice.hpp"
#include "boo/inputdev/DeviceToken.hpp"
#include "boo/inputdev/DeviceBase.hpp"
#include <thread>
#include <mutex>
#include <condition_variable>
#include <string.h>
#include <stdio.h>
#include <algorithm>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN 1
#endif
#include <windows.h>
#include <winusb.h>
#include <usb100.h>
#include <Winusbio.h>
#include <hidsdi.h>

#undef min
#undef max

namespace boo
{

class HIDDeviceWinUSB final : public IHIDDevice
{
    DeviceToken& m_token;
    std::shared_ptr<DeviceBase> m_devImp;

    HANDLE m_devHandle = 0;
    HANDLE m_hidHandle = 0;
    WINUSB_INTERFACE_HANDLE m_usbHandle = nullptr;
    unsigned m_usbIntfInPipe = 0;
    unsigned m_usbIntfOutPipe = 0;
    bool m_runningTransferLoop = false;

    std::string_view m_devPath;
    std::mutex m_initMutex;
    std::condition_variable m_initCond;
    std::thread m_thread;

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

    static void _threadProcUSBLL(std::shared_ptr<HIDDeviceWinUSB> device)
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
            device->m_devImp->deviceError(errStr);
            lk.unlock();
            device->m_initCond.notify_one();
            return;
        }

        if (!WinUsb_Initialize(device->m_devHandle, &device->m_usbHandle))
        {
            _snprintf(errStr, 256, "Unable to open %s@%s: %d\n",
                      device->m_token.getProductName().c_str(),
                      device->m_devPath.c_str(), GetLastError());
            device->m_devImp->deviceError(errStr);
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
            device->m_devImp->deviceError(errStr);
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
        device->m_devImp->initialCycle();
        while (device->m_runningTransferLoop)
            device->m_devImp->transferCycle();
        device->m_devImp->finalCycle();

        /* Cleanup */
        WinUsb_Free(device->m_usbHandle);
        CloseHandle(device->m_devHandle);
        device->m_devHandle = 0;
    }

    static void _threadProcBTLL(std::shared_ptr<HIDDeviceWinUSB> device)
    {
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

    size_t m_minFeatureSz = 0;
    size_t m_minInputSz = 0;
    size_t m_minOutputSz = 0;

    PHIDP_PREPARSED_DATA m_preparsedData = nullptr;

    static void _threadProcHID(std::shared_ptr<HIDDeviceWinUSB> device)
    {
        char errStr[256];
        std::unique_lock<std::mutex> lk(device->m_initMutex);

        /* POSIX.. who needs it?? -MS */
        device->m_overlapped.hEvent = CreateEvent(nullptr, TRUE, FALSE, nullptr);
        device->m_hidHandle = CreateFileA(device->m_devPath.c_str(),
                                          GENERIC_WRITE | GENERIC_READ,
                                          FILE_SHARE_WRITE | FILE_SHARE_READ,
                                          NULL,
                                          OPEN_EXISTING,
                                          FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OVERLAPPED,
                                          NULL);
        if (INVALID_HANDLE_VALUE == device->m_hidHandle)
        {
            _snprintf(errStr, 256, "Unable to open %s@%s: %d\n",
                      device->m_token.getProductName().c_str(),
                      device->m_devPath.c_str(), GetLastError());
            device->m_devImp->deviceError(errStr);
            lk.unlock();
            device->m_initCond.notify_one();
            return;
        }

        if (!HidD_GetPreparsedData(device->m_hidHandle, &device->m_preparsedData))
        {
            _snprintf(errStr, 256, "Unable get preparsed data of %s@%s: %d\n",
                      device->m_token.getProductName().c_str(),
                      device->m_devPath.c_str(), GetLastError());
            device->m_devImp->deviceError(errStr);
            lk.unlock();
            device->m_initCond.notify_one();
            return;
        }

        HIDP_CAPS caps;
        HidP_GetCaps(device->m_preparsedData, &caps);
        device->m_minFeatureSz = caps.FeatureReportByteLength;
        device->m_minInputSz = caps.InputReportByteLength;
        device->m_minOutputSz = caps.OutputReportByteLength;

        /* Return control to main thread */
        device->m_runningTransferLoop = true;
        lk.unlock();
        device->m_initCond.notify_one();

        /* Allocate read buffer */
        size_t inBufferSz = device->m_minInputSz;
        std::unique_ptr<uint8_t[]> readBuf(new uint8_t[inBufferSz]);

        /* Start transfer loop */
        device->m_devImp->initialCycle();
        while (device->m_runningTransferLoop)
        {
            device->ReadCycle(readBuf.get(), inBufferSz);
            if (device->m_runningTransferLoop)
                device->m_devImp->transferCycle();
        }
        device->m_devImp->finalCycle();

        /* Cleanup */
        CloseHandle(device->m_overlapped.hEvent);
        CloseHandle(device->m_hidHandle);
        HidD_FreePreparsedData(device->m_preparsedData);
        device->m_hidHandle = nullptr;
    }

    void _deviceDisconnected()
    {
        m_runningTransferLoop = false;
    }

    std::vector<uint8_t> m_sendBuf;
    std::vector<uint8_t> m_recvBuf;

    const PHIDP_PREPARSED_DATA _getReportDescriptor()
    {
        return m_preparsedData;
    }

    bool _sendHIDReport(const uint8_t* data, size_t length, HIDReportType tp, uint32_t message)
    {
        size_t maxOut = std::max(m_minFeatureSz, std::max(m_minOutputSz, length));
        if (m_sendBuf.size() < maxOut)
            m_sendBuf.resize(maxOut);
        if (maxOut > length)
            memset(m_sendBuf.data() + length, 0, maxOut - length);
        memmove(m_sendBuf.data(), data, length);

        if (tp == HIDReportType::Output)
        {
            DWORD useLength = DWORD(std::max(length, m_minOutputSz));
            DWORD BytesWritten;
            OVERLAPPED Overlapped;
            ZeroMemory(&Overlapped, sizeof(Overlapped));
            BOOL Result = WriteFile(m_hidHandle, m_sendBuf.data(), useLength, &BytesWritten, &Overlapped);
            if (!Result)
            {
                DWORD Error = GetLastError();

                if (Error == ERROR_INVALID_USER_BUFFER)
                {
                    //std::cout << "Falling back to SetOutputReport" << std::endl;
                    if (!HidD_SetOutputReport(m_hidHandle, (PVOID)m_sendBuf.data(), useLength))
                        return false;
                }

                if (Error != ERROR_IO_PENDING)
                {
                    fprintf(stderr, "Write Failed %08X\n", Error);
                    return false;
                }
            }

            if (!GetOverlappedResult(m_hidHandle, &Overlapped, &BytesWritten, TRUE))
            {
                DWORD Error = GetLastError();
                fprintf(stderr, "Write Failed %08X\n", Error);
                return false;
            }
        }
        else if (tp == HIDReportType::Feature)
        {
            DWORD useLength = DWORD(std::max(length, m_minFeatureSz));
            if (!HidD_SetFeature(m_hidHandle, (PVOID)m_sendBuf.data(), useLength))
            {
                int error = GetLastError();
                return false;
            }
        }
        return true;
    }


    size_t _receiveHIDReport(uint8_t* data, size_t length, HIDReportType tp, uint32_t message)
    {
        size_t maxIn = std::max(m_minFeatureSz, std::max(m_minInputSz, length));
        if (m_recvBuf.size() < maxIn)
            m_recvBuf.resize(maxIn);
        memset(m_recvBuf.data(), 0, length);
        m_recvBuf[0] = message;

        if (tp == HIDReportType::Input)
        {
            if (!HidD_GetInputReport(m_hidHandle, m_recvBuf.data(), ULONG(std::max(m_minInputSz, length))))
                return 0;
        }
        else if (tp == HIDReportType::Feature)
        {
            if (!HidD_GetFeature(m_hidHandle, m_recvBuf.data(), ULONG(std::max(m_minFeatureSz, length))))
                return 0;
        }

        memmove(data, m_recvBuf.data(), length);
        return length;
    }

public:

    HIDDeviceWinUSB(DeviceToken& token, const std::shared_ptr<DeviceBase>& devImp)
    : m_token(token),
      m_devImp(devImp),
      m_devPath(token.getDevicePath())
    {
    }

    void _startThread()
    {
        std::unique_lock<std::mutex> lk(m_initMutex);
        DeviceType dType = m_token.getDeviceType();
        if (dType == DeviceType::USB)
            m_thread = std::thread(_threadProcUSBLL, std::static_pointer_cast<HIDDeviceWinUSB>(shared_from_this()));
        else if (dType == DeviceType::Bluetooth)
            m_thread = std::thread(_threadProcBTLL, std::static_pointer_cast<HIDDeviceWinUSB>(shared_from_this()));
        else if (dType == DeviceType::HID)
            m_thread = std::thread(_threadProcHID, std::static_pointer_cast<HIDDeviceWinUSB>(shared_from_this()));
        else
            throw std::runtime_error("invalid token supplied to device constructor");
        m_initCond.wait(lk);
    }

    ~HIDDeviceWinUSB()
    {
        m_runningTransferLoop = false;
        if (m_thread.joinable())
            m_thread.detach();
    }

    OVERLAPPED m_overlapped = {};

    void ReadCycle(uint8_t* inBuffer, size_t inBufferSz)
    {
        ResetEvent(m_overlapped.hEvent);
        ZeroMemory(inBuffer, inBufferSz);
        DWORD BytesRead = 0;
        BOOL Result = ReadFile(m_hidHandle, inBuffer, DWORD(inBufferSz), &BytesRead, &m_overlapped);
        if (!Result)
        {
            DWORD Error = GetLastError();
            if (Error == ERROR_DEVICE_NOT_CONNECTED)
            {
                m_runningTransferLoop = false;
                return;
            }
            else if (Error != ERROR_IO_PENDING)
            {
                fprintf(stderr, "Read Failed: %08X\n", Error);
                return;
            }
            else if (!GetOverlappedResultEx(m_hidHandle, &m_overlapped, &BytesRead, 10, TRUE))
            {
                return;
            }
        }

        m_devImp->receivedHIDReport(inBuffer, BytesRead, HIDReportType::Input, inBuffer[0]);
    }
};

std::shared_ptr<IHIDDevice> IHIDDeviceNew(DeviceToken& token, const std::shared_ptr<DeviceBase>& devImp)
{
    return std::make_shared<HIDDeviceWinUSB>(token, devImp);
}

}
