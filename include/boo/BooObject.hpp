#ifndef BOOOBJECT_HPP
#define BOOOBJECT_HPP

#include <atomic>

namespace boo
{

class IObj
{
    std::atomic_int m_refCount = {0};
public:
    virtual ~IObj() = default;
    void increment() { m_refCount++; }
    void decrement()
    {
        if (m_refCount.fetch_sub(1) == 1)
            delete this;
    }
};

template<class SubCls>
class ObjToken
{
    SubCls* m_obj = nullptr;
public:
    ObjToken() = default;
    ObjToken(SubCls* obj) : m_obj(obj) { if (m_obj) m_obj->increment(); }
    ObjToken(const ObjToken& other) : m_obj(other.m_obj) { if (m_obj) m_obj->increment(); }
    ObjToken(ObjToken&& other) : m_obj(other.m_obj) { other.m_obj = nullptr; }
    ObjToken& operator=(SubCls* obj)
    { if (m_obj) m_obj->decrement(); m_obj = obj; if (m_obj) m_obj->increment(); return *this; }
    ObjToken& operator=(const ObjToken& other)
    { if (m_obj) m_obj->decrement(); m_obj = other.m_obj; if (m_obj) m_obj->increment(); return *this; }
    ObjToken& operator=(ObjToken&& other)
    { if (m_obj) m_obj->decrement(); m_obj = other.m_obj; other.m_obj = nullptr; return *this; }
    ~ObjToken() { if (m_obj) m_obj->decrement(); }
    SubCls* get() const { return m_obj; }
    SubCls* operator->() const { return m_obj; }
    SubCls& operator*() const { return *m_obj; }
    template<class T> T* cast() const { return static_cast<T*>(m_obj); }
    operator bool() const { return m_obj != nullptr; }
    void reset() { if (m_obj) m_obj->decrement(); m_obj = nullptr; }
};

}

#endif // BOOOBJECT_HPP
