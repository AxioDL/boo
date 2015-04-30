#ifndef CDEVICEFINDER_HPP
#define CDEVICEFINDER_HPP

#include <set>
#include <mutex>
#include <stdexcept>
#include "CDeviceToken.hpp"
#include "IHIDListener.hpp"
#include "SDeviceSignature.hpp"
#include <string.h>

namespace boo
{

static class CDeviceFinder* skDevFinder = NULL;

class CDeviceFinder
{
public:
    friend class CHIDListenerIOKit;
    friend class CHIDListenerUdev;
    friend class CHIDListenerWin32;
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
    inline void _insertToken(CDeviceToken&& token)
    {
        if (SDeviceSignature::DeviceMatchToken(token, m_types)) {
            m_tokensLock.lock();
            TInsertedDeviceToken inseredTok =
            m_tokens.insert(std::make_pair(token.getDevicePath(), std::move(token)));
            m_tokensLock.unlock();
            deviceConnected(inseredTok.first->second);
        }
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
    CDeviceFinder(std::vector<const char*> types)
    : m_listener(NULL)
    {
        if (skDevFinder)
            throw std::runtime_error("only one instance of CDeviceFinder may be constructed");
        skDevFinder = this;
        for (const char* typeName : types)
        {
            const SDeviceSignature* sigIter = BOO_DEVICE_SIGS;
            while (sigIter->m_name)
            {
                if (!strcmp(sigIter->m_name, typeName))
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
    
};

}

#endif // CDEVICEFINDER_HPP
