#pragma once

#include <atomic>

namespace boo {

class IObj {
  std::atomic_int m_refCount = {0};

protected:
  virtual ~IObj() = default;

public:
  void increment() { m_refCount.fetch_add(1, std::memory_order_relaxed); }
  void decrement() {
    if (m_refCount.fetch_sub(1, std::memory_order_release) == 1) {
      std::atomic_thread_fence(std::memory_order_acquire);
      delete this;
    }
  }
};

template <class SubCls>
class ObjToken {
  SubCls* m_obj = nullptr;

public:
  ObjToken() = default;
  ObjToken(SubCls* obj) : m_obj(obj) {
    if (m_obj)
      m_obj->increment();
  }
  ObjToken(const ObjToken& other) : m_obj(other.m_obj) {
    if (m_obj)
      m_obj->increment();
  }
  ObjToken(ObjToken&& other) : m_obj(other.m_obj) { other.m_obj = nullptr; }
  ObjToken& operator=(SubCls* obj) {
    if (m_obj)
      m_obj->decrement();
    m_obj = obj;
    if (m_obj)
      m_obj->increment();
    return *this;
  }
  ObjToken& operator=(const ObjToken& other) {
    if (m_obj)
      m_obj->decrement();
    m_obj = other.m_obj;
    if (m_obj)
      m_obj->increment();
    return *this;
  }
  ObjToken& operator=(ObjToken&& other) {
    if (m_obj)
      m_obj->decrement();
    m_obj = other.m_obj;
    other.m_obj = nullptr;
    return *this;
  }
  ~ObjToken() {
    if (m_obj)
      m_obj->decrement();
  }
  SubCls* get() const { return m_obj; }
  SubCls* operator->() const { return m_obj; }
  SubCls& operator*() const { return *m_obj; }
  template <class T>
  T* cast() const {
    return static_cast<T*>(m_obj);
  }
  operator bool() const { return m_obj != nullptr; }
  void reset() {
    if (m_obj)
      m_obj->decrement();
    m_obj = nullptr;
  }
};

} // namespace boo
