#pragma once

#include "boo/BooObject.hpp"
#include <iterator>

namespace boo {

/** Linked-list iterator shareable by ListNode types. */
template <class T>
class ListIterator {
  T* m_node;

public:
  using iterator_category = std::bidirectional_iterator_tag;
  using value_type = T;
  using difference_type = std::ptrdiff_t;
  using pointer = T*;
  using reference = T&;

  explicit ListIterator(T* node) : m_node(node) {}
  T& operator*() const { return *m_node; }
  bool operator!=(const ListIterator& other) const { return m_node != other.m_node; }
  ListIterator& operator++() {
    m_node = m_node->m_next;
    return *this;
  }
  ListIterator& operator--() {
    m_node = m_node->m_prev;
    return *this;
  }
};

/** Linked-list IObj node made part of objects participating in list.
 *  Subclasses must implement static methods _getHeadPtr() and _getHeadLock()
 *  to support the common list-management functionality.
 */
template <class N, class H, class P = IObj>
struct ListNode : P {
  using iterator = ListIterator<N>;
  iterator begin() { return iterator(static_cast<N*>(this)); }
  iterator end() { return iterator(nullptr); }

  H m_head;
  N* m_next;
  N* m_prev = nullptr;
  ListNode(H head) : m_head(head) {
    auto lk = N::_getHeadLock(head);
    m_next = N::_getHeadPtr(head);
    if (m_next)
      m_next->m_prev = static_cast<N*>(this);
    N::_getHeadPtr(head) = static_cast<N*>(this);
  }

protected:
  ~ListNode() {
    if (m_prev) {
      if (m_next)
        m_next->m_prev = m_prev;
      m_prev->m_next = m_next;
    } else {
      if (m_next)
        m_next->m_prev = nullptr;
      N::_getHeadPtr(m_head) = m_next;
    }
  }
};

static inline uint32_t flp2(uint32_t x) {
  x = x | (x >> 1);
  x = x | (x >> 2);
  x = x | (x >> 4);
  x = x | (x >> 8);
  x = x | (x >> 16);
  return x - (x >> 1);
}

/* When instrumenting with MemorySanitizer, external libraries
 * (particularly the OpenGL implementation) will report tons of false
 * positives. The BOO_MSAN_NO_INTERCEPT macro declares a RAII object
 * to temporarily suspend memory tracking so external calls can be made.
 */
#if defined(__has_feature)
#if __has_feature(memory_sanitizer)
#define BOO_MSAN 1
#include <sanitizer/msan_interface.h>
struct InterceptorScope {
  InterceptorScope() { __msan_scoped_disable_interceptor_checks(); }
  ~InterceptorScope() { __msan_scoped_enable_interceptor_checks(); }
};
#define BOO_MSAN_NO_INTERCEPT InterceptorScope _no_intercept;
#define BOO_MSAN_UNPOISON(data, length) __msan_unpoison(data, length)
#endif
#endif
#ifndef BOO_MSAN_NO_INTERCEPT
#define BOO_MSAN_NO_INTERCEPT
#endif
#ifndef BOO_MSAN_UNPOISON
#define BOO_MSAN_UNPOISON(data, length)
#endif

} // namespace boo
