#ifndef BOO_GRAPHICSDEV_COMMON_HPP
#define BOO_GRAPHICSDEV_COMMON_HPP

/* Private header for managing shader data
 * binding lifetimes through rendering cycle */

#include <atomic>
#include "boo/graphicsdev/IGraphicsDataFactory.hpp"

namespace boo
{

template <class DataImpl>
class IGraphicsDataPriv : public IGraphicsData
{
    std::atomic_int m_refCount = {1};
public:
    void increment() { m_refCount++; }
    void decrement()
    {
        if (m_refCount.fetch_sub(1) == 1)
            delete static_cast<DataImpl*>(this);
    }
};

template <class DataImpl>
class IShaderDataBindingPriv : public IShaderDataBinding
{
    IGraphicsDataPriv<DataImpl>* m_parent;

public:
    IShaderDataBindingPriv(IGraphicsDataPriv<DataImpl>* p) : m_parent(p) {}
    class Token
    {
        IGraphicsDataPriv<DataImpl>* m_data = nullptr;
    public:
        Token() = default;
        Token(const IShaderDataBindingPriv* p)
        : m_data(p->m_parent)
        { m_data->increment(); }
        Token& operator=(const Token&) = delete;
        Token(const Token&) = delete;
        Token& operator=(Token&& other)
        { m_data = other.m_data; other.m_data = nullptr; return *this; }
        Token(Token&& other)
        { m_data = other.m_data; other.m_data = nullptr; }
        ~Token() { if (m_data) m_data->decrement(); }
    };

    Token lock() const { return Token(this); }
};

}

#endif // BOO_GRAPHICSDEV_COMMON_HPP
