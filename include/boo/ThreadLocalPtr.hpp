#pragma once

#ifndef __SWITCH__

#if _WIN32
#else
#include <pthread.h>
#endif

/** Multiplatform TLS-pointer wrapper (for compilers without proper thread_local support) */
template <class T>
class ThreadLocalPtr {
#if _WIN32
  DWORD m_key;

public:
  ThreadLocalPtr() { m_key = TlsAlloc(); }
  ~ThreadLocalPtr() { TlsFree(m_key); }
  T* get() const { return static_cast<T*>(TlsGetValue(m_key)); }
  void reset(T* v = nullptr) { TlsSetValue(m_key, LPVOID(v)); }
#else
  pthread_key_t m_key;

public:
  ThreadLocalPtr() { pthread_key_create(&m_key, nullptr); }
  ~ThreadLocalPtr() { pthread_key_delete(m_key); }
  T* get() const { return static_cast<T*>(pthread_getspecific(m_key)); }
  void reset(T* v = nullptr) { pthread_setspecific(m_key, v); }
#endif
  T* operator->() { return get(); }
};

#endif
