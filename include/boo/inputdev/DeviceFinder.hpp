#ifndef CDEVICEFINDER_HPP
#define CDEVICEFINDER_HPP

#include <unordered_set>
#include <typeindex>
#include <mutex>
#include "DeviceToken.hpp"
#include "IHIDListener.hpp"
#include "DeviceSignature.hpp"
#include <cstring>
#include <cstdio>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace boo
{

class DeviceFinder
{
public:
    friend class HIDListenerIOKit;
    friend class HIDListenerUdev;
    friend class HIDListenerWinUSB;
    static inline DeviceFinder* instance() {return skDevFinder;}

private:
    static class DeviceFinder* skDevFinder;

    /* Types this finder is interested in (immutable) */
    DeviceSignature::TDeviceSignatureSet m_types;

    /* Platform-specific USB event registration
     * (for auto-scanning, NULL if not registered) */
    std::unique_ptr<IHIDListener> m_listener;

    /* Set of presently-connected device tokens */
    TDeviceTokens m_tokens;
    std::mutex m_tokensLock;

    /* Friend methods for platform-listener to find/insert/remove
     * tokens with type-filtering */
    inline bool _hasToken(const std::string& path)
    {
        auto preCheck = m_tokens.find(path);
        if (preCheck != m_tokens.end())
            return true;
        return false;
    }
    inline bool _insertToken(std::unique_ptr<DeviceToken>&& token)
    {
        if (DeviceSignature::DeviceMatchToken(*token, m_types))
        {
            m_tokensLock.lock();
            TInsertedDeviceToken inseredTok =
            m_tokens.insert(std::make_pair(token->getDevicePath(), std::move(token)));
            m_tokensLock.unlock();
            deviceConnected(*inseredTok.first->second);
            return true;
        }
        return false;
    }
    inline void _removeToken(const std::string& path)
    {
        auto preCheck = m_tokens.find(path);
        if (preCheck != m_tokens.end())
        {
            DeviceToken& tok = *preCheck->second;
            std::shared_ptr<DeviceBase> dev = tok.m_connectedDev;
            tok._deviceClose();
            deviceDisconnected(tok, dev.get());
            m_tokensLock.lock();
            m_tokens.erase(preCheck);
            m_tokensLock.unlock();
        }
    }

public:

    class CDeviceTokensHandle
    {
        DeviceFinder& m_finder;
    public:
        inline CDeviceTokensHandle(DeviceFinder& finder) : m_finder(finder)
        {m_finder.m_tokensLock.lock();}
        inline ~CDeviceTokensHandle() {m_finder.m_tokensLock.unlock();}
        inline TDeviceTokens::iterator begin() {return m_finder.m_tokens.begin();}
        inline TDeviceTokens::iterator end() {return m_finder.m_tokens.end();}
    };

    /* Application must specify its interested device-types */
    DeviceFinder(std::unordered_set<std::type_index> types)
    {
        if (skDevFinder)
        {
            fprintf(stderr, "only one instance of CDeviceFinder may be constructed");
            abort();
        }
        skDevFinder = this;
        for (const std::type_index& typeIdx : types)
        {
            const DeviceSignature* sigIter = BOO_DEVICE_SIGS;
            while (sigIter->m_name)
            {
                if (sigIter->m_typeIdx == typeIdx)
                    m_types.push_back(sigIter);
                ++sigIter;
            }
        }
    }
    virtual ~DeviceFinder()
    {
        if (m_listener)
            m_listener->stopScanning();
        skDevFinder = NULL;
    }

    /* Get interested device-type mask */
    inline const DeviceSignature::TDeviceSignatureSet& getTypes() const {return m_types;}

    /* Iterable set of tokens */
    inline CDeviceTokensHandle getTokens() {return CDeviceTokensHandle(*this);}

    /* Automatic device scanning */
    inline bool startScanning()
    {
        if (!m_listener)
            m_listener = IHIDListenerNew(*this);
        if (m_listener)
            return m_listener->startScanning();
        return false;
    }
    inline bool stopScanning()
    {
        if (!m_listener)
            m_listener = IHIDListenerNew(*this);
        if (m_listener)
            return m_listener->stopScanning();
        return false;
    }

    /* Manual device scanning */
    inline bool scanNow()
    {
        if (!m_listener)
            m_listener = IHIDListenerNew(*this);
        if (m_listener)
            return m_listener->scanNow();
        return false;
    }

    virtual void deviceConnected(DeviceToken&) {}
    virtual void deviceDisconnected(DeviceToken&, DeviceBase*) {}

#if _WIN32
    /* Windows-specific WM_DEVICECHANGED handler */
    static LRESULT winDevChangedHandler(WPARAM wParam, LPARAM lParam);
#endif

};

}

#endif // CDEVICEFINDER_HPP
