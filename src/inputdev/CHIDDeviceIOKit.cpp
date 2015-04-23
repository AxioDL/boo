#include "IHIDDevice.hpp"
#include "inputdev/CDeviceToken.hpp"
#include "inputdev/CDeviceBase.hpp"
#include <IOKit/hid/IOHIDLib.h>
#include <thread>

#define MAX_REPORT_SIZE 65536

class CHIDDeviceIOKit final : public IHIDDevice
{
    CDeviceToken* m_token;
    IOHIDDeviceRef m_dev;
    std::mutex m_initMutex;
    std::condition_variable m_initCond;
    std::thread* m_thread;
    CFRunLoopRef m_runLoop;
    CDeviceBase* m_devImp;

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
    static void _threadProc(CHIDDeviceIOKit* device)
    {
        char thrName[128];
        snprintf(thrName, 128, "%s Device Thread", device->m_token->getProductName().c_str());
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
        IOHIDDeviceRegisterInputReportCallback(device->m_dev, inputBuf, MAX_REPORT_SIZE,
                                               (IOHIDReportCallback)_inputReport, device);
        IOHIDDeviceRegisterRemovalCallback(device->m_dev, (IOHIDCallback)_disconnect, device);
        IOHIDDeviceScheduleWithRunLoop(device->m_dev, device->m_runLoop, kCFRunLoopDefaultMode);
        IOHIDDeviceOpen(device->m_dev, kIOHIDOptionsTypeNone);
        CFRunLoopRun();
        if (device->m_runLoop)
            IOHIDDeviceClose(device->m_dev, kIOHIDOptionsTypeNone);
    }
    
    void _deviceDisconnected()
    {
        CFRunLoopRef rl = m_runLoop;
        m_runLoop = NULL;
        if (rl)
            CFRunLoopStop(rl);
    }
    
    void _setDeviceImp(CDeviceBase* dev)
    {
        m_devImp = dev;
    }
    
public:
    
    CHIDDeviceIOKit(CDeviceToken* token)
    : m_token(token),
      m_dev(token->getDeviceHandle())
    {
        std::unique_lock<std::mutex> lk(m_initMutex);
        m_thread = new std::thread(_threadProc, this);
        m_initCond.wait(lk);
    }
    
    ~CHIDDeviceIOKit()
    {
        if (m_runLoop)
            CFRunLoopStop(m_runLoop);
        m_thread->detach();
        delete m_thread;
    }
    

};

IHIDDevice* IHIDDeviceNew(CDeviceToken* token)
{
    return new CHIDDeviceIOKit(token);
}
