#ifndef BOO_GRAPHICSDEV_COMMON_HPP
#define BOO_GRAPHICSDEV_COMMON_HPP

/* Private header for managing shader data
 * binding lifetimes through rendering cycle */

#include <atomic>
#include <vector>
#include "boo/graphicsdev/IGraphicsDataFactory.hpp"

namespace boo
{

class IGraphicsDataPriv : public IGraphicsData
{
    std::atomic_int m_refCount = {1};
public:
    void increment() { m_refCount++; }
    void decrement()
    {
        if (m_refCount.fetch_sub(1) == 1)
            delete this;
    }
};

class IShaderDataBindingPriv : public IShaderDataBinding
{
    IGraphicsDataPriv* m_parent;
    std::vector<IGraphicsDataPriv*> m_depDatas;

public:
    IShaderDataBindingPriv(IGraphicsDataPriv* p) : m_parent(p) {}
    class Token
    {
        IGraphicsDataPriv* m_data = nullptr;
    public:
        Token() = default;
        Token(const IShaderDataBindingPriv* p)
        : m_data(p->m_parent) { m_data->increment(); }
        Token& operator=(const Token&) = delete;
        Token(const Token&) = delete;
        Token& operator=(Token&& other)
        { m_data = other.m_data; other.m_data = nullptr; return *this; }
        Token(Token&& other)
        { m_data = other.m_data; other.m_data = nullptr; }
        ~Token() { if (m_data) { m_data->decrement(); } }
    };

    Token lock() const { return Token(this); }
    ~IShaderDataBindingPriv()
    {
        for (IGraphicsDataPriv* dep : m_depDatas)
            dep->decrement();
    }
    
protected:
    void addDepData(IGraphicsData* data)
    {
        IGraphicsDataPriv* d = static_cast<IGraphicsDataPriv*>(data);
        if (d != m_parent)
        {
            m_depDatas.push_back(d);
            d->increment();
        }
    }
};

template <class FactoryImpl, class ShaderImpl>
class IShareableShader
{
    std::atomic_int m_refCount = {0};
    FactoryImpl& m_factory;
    uint64_t m_srckey, m_binKey;
public:
    IShareableShader(FactoryImpl& factory, uint64_t srcKey, uint64_t binKey)
    : m_factory(factory), m_srckey(srcKey), m_binKey(binKey) {}
    void increment() { m_refCount++; }
    void decrement()
    {
        if (m_refCount.fetch_sub(1) == 1)
            m_factory._unregisterShareableShader(m_srckey, m_binKey);
    }

    class Token
    {
        IShareableShader<FactoryImpl, ShaderImpl>* m_parent = nullptr;
    public:
        Token() = default;
        Token(IShareableShader* p)
        : m_parent(p)
        { m_parent->increment(); }
        Token& operator=(const Token&) = delete;
        Token(const Token&) = delete;
        Token& operator=(Token&& other)
        { m_parent = other.m_parent; other.m_parent = nullptr; return *this; }
        Token(Token&& other)
        { m_parent = other.m_parent; other.m_parent = nullptr; }
        void reset() { if (m_parent) m_parent->decrement(); m_parent = nullptr; }
        ~Token() { if (m_parent) m_parent->decrement(); }
        operator bool() const { return m_parent != nullptr; }
        ShaderImpl& get() const { return static_cast<ShaderImpl&>(*m_parent); }
    };

    Token lock() { return Token(this); }
};

}

#endif // BOO_GRAPHICSDEV_COMMON_HPP
