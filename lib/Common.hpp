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

} // namespace boo
