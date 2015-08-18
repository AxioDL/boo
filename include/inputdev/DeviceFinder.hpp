#ifndef CDEVICEFINDER_HPP
#define CDEVICEFINDER_HPP

#include <unordered_set>
#include <typeindex>
#include <mutex>
#include "DeviceToken.hpp"
#include "IHIDListener.hpp"
#include "DeviceSignature.hpp"
#include <string.h>
#include <stdio.h>

#ifdef _WIN32
#define _WIN32_LEAN_AND_MEAN 1
#include <windows.h>
#include <Dbt.h>
#endif

namespace boo
{

static class CDeviceFinder* skDevFinder = NULL;

class CDeviceFinder
{
public:
    friend class CHIDListenerIOKit;
    friend class CHIDListenerUdev;
    friend class CHIDListenerWinUSB;
    static inline CDeviceFinder* instance() {return skDevFinder;}
    
private:
    
    /* Types this finder is interested in (immutable) */
    SDeviceSignature::TDeviceSignatureSet m_types;
    
    /* Platform-specific USB event registration
     * (for auto-scanning, NULL if not registered) */
    IHIDListener* m_listener;
    
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
    inline bool _insertToken(CDeviceToken&& token)
    {
        if (SDeviceSignature::DeviceMatchToken(token, m_types)) {
            m_tokensLock.lock();
            TInsertedDeviceToken inseredTok =
            m_tokens.insert(std::make_pair(token.getDevicePath(), std::move(token)));
            m_tokensLock.unlock();
            deviceConnected(inseredTok.first->second);
            return true;
        }
        return false;
    }
    inline void _removeToken(const std::string& path)
    {
        auto preCheck = m_tokens.find(path);
        if (preCheck != m_tokens.end())
        {
            CDeviceToken& tok = preCheck->second;
            CDeviceBase* dev = tok.m_connectedDev;
            tok._deviceClose();
            deviceDisconnected(tok, dev);
            m_tokensLock.lock();
            m_tokens.erase(preCheck);
            m_tokensLock.unlock();
        }
    }
    
public:
    
    class CDeviceTokensHandle
    {
        CDeviceFinder& m_finder;
    public:
        inline CDeviceTokensHandle(CDeviceFinder& finder) : m_finder(finder)
        {m_finder.m_tokensLock.lock();}
        inline ~CDeviceTokensHandle() {m_finder.m_tokensLock.unlock();}
        inline TDeviceTokens::iterator begin() {return m_finder.m_tokens.begin();}
        inline TDeviceTokens::iterator end() {return m_finder.m_tokens.end();}
    };
    
    /* Application must specify its interested device-types */
    CDeviceFinder(std::unordered_set<std::type_index> types)
    : m_listener(NULL)
    {
        if (skDevFinder)
        {
            fprintf(stderr, "only one instance of CDeviceFinder may be constructed");
            abort();
        }
        skDevFinder = this;
        for (const std::type_index& typeIdx : types)
        {
            const SDeviceSignature* sigIter = BOO_DEVICE_SIGS;
            while (sigIter->m_name)
            {
                if (sigIter->m_typeIdx == typeIdx)
                    m_types.push_back(sigIter);
                ++sigIter;
            }
        }
    }
    ~CDeviceFinder()
    {
        if (m_listener)
            m_listener->stopScanning();
        delete m_listener;
        skDevFinder = NULL;
    }
    
    /* Get interested device-type mask */
    inline const SDeviceSignature::TDeviceSignatureSet& getTypes() const {return m_types;}
    
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
    
    virtual void deviceConnected(CDeviceToken&) {}
    virtual void deviceDisconnected(CDeviceToken&, CDeviceBase*) {}

#if _WIN32
    /* Windows-specific WM_DEVICECHANGED handler */
    static LRESULT winDevChangedHandler(WPARAM wParam, LPARAM lParam)
    {
        PDEV_BROADCAST_HDR dbh = (PDEV_BROADCAST_HDR)lParam;
        PDEV_BROADCAST_DEVICEINTERFACE dbhi = (PDEV_BROADCAST_DEVICEINTERFACE)lParam;
        CDeviceFinder* finder = instance();
        if (!finder)
            return 0;

        if (wParam == DBT_DEVICEARRIVAL)
        {
            if (dbh->dbch_devicetype == DBT_DEVTYP_DEVICEINTERFACE)
            {
#ifdef UNICODE
                char devPath[1024];
                wcstombs(devPath, dbhi->dbcc_name, 1024);
                finder->m_listener->_extDevConnect(devPath);
#else
                finder->m_listener->_extDevConnect(dbhi->dbcc_name);
#endif
            }
        }
        else if (wParam == DBT_DEVICEREMOVECOMPLETE)
        {
            if (dbh->dbch_devicetype == DBT_DEVTYP_DEVICEINTERFACE)
            {
#ifdef UNICODE
                char devPath[1024];
                wcstombs(devPath, dbhi->dbcc_name, 1024);
                finder->m_listener->_extDevDisconnect(devPath);
#else
                finder->m_listener->_extDevDisconnect(dbhi->dbcc_name);
#endif
            }
        }

        return 0;
    }
#endif
    
};

}

#endif // CDEVICEFINDER_HPP
