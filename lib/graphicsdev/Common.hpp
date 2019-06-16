#pragma once

/* Private header for managing shader data
 * binding lifetimes through rendering cycle */

#include <atomic>
#include <vector>
#include <mutex>
#include <cassert>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <queue>
#include "boo/graphicsdev/IGraphicsDataFactory.hpp"
#include "../Common.hpp"

namespace boo {

struct BaseGraphicsData;
struct BaseGraphicsPool;

template <class NodeCls, class DataCls = BaseGraphicsData>
struct GraphicsDataNode;

/** Inherited by data factory implementations to track the head data and pool nodes */
struct GraphicsDataFactoryHead {
  std::recursive_mutex m_dataMutex;
  BaseGraphicsData* m_dataHead = nullptr;
  BaseGraphicsPool* m_poolHead = nullptr;

  ~GraphicsDataFactoryHead() {
    assert(m_dataHead == nullptr && "Dangling graphics data pools detected");
    assert(m_poolHead == nullptr && "Dangling graphics data pools detected");
  }
};

/** Private generalized data container class.
 *  Keeps head pointers to all graphics objects by type
 */
struct BaseGraphicsData : ListNode<BaseGraphicsData, GraphicsDataFactoryHead*> {
  static BaseGraphicsData*& _getHeadPtr(GraphicsDataFactoryHead* head) { return head->m_dataHead; }
  static std::unique_lock<std::recursive_mutex> _getHeadLock(GraphicsDataFactoryHead* head) {
    return std::unique_lock<std::recursive_mutex>{head->m_dataMutex};
  }

  __BooTraceFields

  GraphicsDataNode<IShaderStage, BaseGraphicsData>* m_Ss = nullptr;
  GraphicsDataNode<IShaderPipeline, BaseGraphicsData>* m_SPs = nullptr;
  GraphicsDataNode<IShaderDataBinding, BaseGraphicsData>* m_SBinds = nullptr;
  GraphicsDataNode<IGraphicsBufferS, BaseGraphicsData>* m_SBufs = nullptr;
  GraphicsDataNode<IGraphicsBufferD, BaseGraphicsData>* m_DBufs = nullptr;
  GraphicsDataNode<ITextureS, BaseGraphicsData>* m_STexs = nullptr;
  GraphicsDataNode<ITextureSA, BaseGraphicsData>* m_SATexs = nullptr;
  GraphicsDataNode<ITextureD, BaseGraphicsData>* m_DTexs = nullptr;
  GraphicsDataNode<ITextureR, BaseGraphicsData>* m_RTexs = nullptr;
  GraphicsDataNode<ITextureCubeR, BaseGraphicsData>* m_CubeRTexs = nullptr;
  template <class T>
  GraphicsDataNode<T, BaseGraphicsData>*& getHead();
  template <class T>
  size_t countForward() {
    auto* head = getHead<T>();
    return head ? head->countForward() : 0;
  }
  std::unique_lock<std::recursive_mutex> destructorLock() override {
    return std::unique_lock<std::recursive_mutex>{m_head->m_dataMutex};
  }

  explicit BaseGraphicsData(GraphicsDataFactoryHead& head __BooTraceArgs)
  : ListNode<BaseGraphicsData, GraphicsDataFactoryHead*>(&head) __BooTraceInitializer {}
};

template <>
inline GraphicsDataNode<IShaderStage, BaseGraphicsData>*& BaseGraphicsData::getHead<IShaderStage>() {
  return m_Ss;
}
template <>
inline GraphicsDataNode<IShaderPipeline, BaseGraphicsData>*& BaseGraphicsData::getHead<IShaderPipeline>() {
  return m_SPs;
}
template <>
inline GraphicsDataNode<IShaderDataBinding, BaseGraphicsData>*& BaseGraphicsData::getHead<IShaderDataBinding>() {
  return m_SBinds;
}
template <>
inline GraphicsDataNode<IGraphicsBufferS, BaseGraphicsData>*& BaseGraphicsData::getHead<IGraphicsBufferS>() {
  return m_SBufs;
}
template <>
inline GraphicsDataNode<IGraphicsBufferD, BaseGraphicsData>*& BaseGraphicsData::getHead<IGraphicsBufferD>() {
  return m_DBufs;
}
template <>
inline GraphicsDataNode<ITextureS, BaseGraphicsData>*& BaseGraphicsData::getHead<ITextureS>() {
  return m_STexs;
}
template <>
inline GraphicsDataNode<ITextureSA, BaseGraphicsData>*& BaseGraphicsData::getHead<ITextureSA>() {
  return m_SATexs;
}
template <>
inline GraphicsDataNode<ITextureD, BaseGraphicsData>*& BaseGraphicsData::getHead<ITextureD>() {
  return m_DTexs;
}
template <>
inline GraphicsDataNode<ITextureR, BaseGraphicsData>*& BaseGraphicsData::getHead<ITextureR>() {
  return m_RTexs;
}
template <>
inline GraphicsDataNode<ITextureCubeR, BaseGraphicsData>*& BaseGraphicsData::getHead<ITextureCubeR>() {
  return m_CubeRTexs;
}

/** Private generalized pool container class.
 *  Keeps head pointer to exactly one dynamic buffer while otherwise conforming to BaseGraphicsData
 */
struct BaseGraphicsPool : ListNode<BaseGraphicsPool, GraphicsDataFactoryHead*> {
  static BaseGraphicsPool*& _getHeadPtr(GraphicsDataFactoryHead* head) { return head->m_poolHead; }
  static std::unique_lock<std::recursive_mutex> _getHeadLock(GraphicsDataFactoryHead* head) {
    return std::unique_lock<std::recursive_mutex>{head->m_dataMutex};
  }

  __BooTraceFields

  GraphicsDataNode<IGraphicsBufferD, BaseGraphicsPool>* m_DBufs = nullptr;
  template <class T>
  GraphicsDataNode<T, BaseGraphicsPool>*& getHead();
  template <class T>
  size_t countForward() {
    auto* head = getHead<T>();
    return head ? head->countForward() : 0;
  }
  std::unique_lock<std::recursive_mutex> destructorLock() override {
    return std::unique_lock<std::recursive_mutex>{m_head->m_dataMutex};
  }

  explicit BaseGraphicsPool(GraphicsDataFactoryHead& head __BooTraceArgs)
  : ListNode<BaseGraphicsPool, GraphicsDataFactoryHead*>(&head) __BooTraceInitializer {}
};

template <>
inline GraphicsDataNode<IGraphicsBufferD, BaseGraphicsPool>*& BaseGraphicsPool::getHead<IGraphicsBufferD>() {
  return m_DBufs;
}

/** Private generalised graphics object node.
 *  Keeps a strong reference to the data pool that it's a member of;
 *  as well as doubly-linked pointers to same-type sibling objects
 */
template <class NodeCls, class DataCls>
struct GraphicsDataNode : ListNode<GraphicsDataNode<NodeCls, DataCls>, ObjToken<DataCls>, NodeCls> {
  using base = ListNode<GraphicsDataNode<NodeCls, DataCls>, ObjToken<DataCls>, NodeCls>;
  static GraphicsDataNode<NodeCls, DataCls>*& _getHeadPtr(ObjToken<DataCls>& head) {
    return head->template getHead<NodeCls>();
  }
  static std::unique_lock<std::recursive_mutex> _getHeadLock(ObjToken<DataCls>& head) {
    return std::unique_lock<std::recursive_mutex>{head->m_head->m_dataMutex};
  }

  std::unique_lock<std::recursive_mutex> destructorLock() override {
    return std::unique_lock<std::recursive_mutex>{base::m_head->m_head->m_dataMutex};
  }

  explicit GraphicsDataNode(const ObjToken<DataCls>& data)
  : ListNode<GraphicsDataNode<NodeCls, DataCls>, ObjToken<DataCls>, NodeCls>(data) {}

  class iterator {
    GraphicsDataNode<NodeCls, DataCls>* m_node;

  public:
    using iterator_category = std::bidirectional_iterator_tag;
    using value_type = NodeCls;
    using difference_type = std::ptrdiff_t;
    using pointer = NodeCls*;
    using reference = NodeCls&;

    explicit iterator(GraphicsDataNode<NodeCls, DataCls>* node) : m_node(node) {}
    NodeCls& operator*() const { return *m_node; }
    bool operator!=(const iterator& other) const { return m_node != other.m_node; }
    iterator& operator++() {
      m_node = m_node->m_next;
      return *this;
    }
    iterator& operator--() {
      m_node = m_node->m_prev;
      return *this;
    }
  };

  iterator begin() { return iterator(this); }
  iterator end() { return iterator(nullptr); }

  size_t countForward() {
    size_t ret = 0;
    for (auto& n : *this)
      ++ret;
    return ret;
  }
};

void UpdateGammaLUT(ITextureD* tex, float gamma);

/** Generic work-queue for asynchronously building shader pipelines on supported backends
 */
template <class ShaderPipelineType>
class PipelineCompileQueue {
  struct Task {
    ObjToken<IShaderPipeline> m_pipeline;
    explicit Task(ObjToken<IShaderPipeline> pipeline) : m_pipeline(pipeline) {}
    void run() {
      m_pipeline.cast<ShaderPipelineType>()->compile();
    }
  };

  std::queue<Task> m_tasks;
  size_t m_outstandingTasks = 0;
  std::vector<std::thread> m_threads;
  std::mutex m_mt;
  std::condition_variable m_cv, m_backcv;
  bool m_running = true;

  void worker() {
    std::unique_lock<std::mutex> lk(m_mt);
    while (m_running) {
      m_cv.wait(lk, [this]() { return !m_tasks.empty() || !m_running; });
      if (!m_running)
        break;
      Task t = std::move(m_tasks.front());
      m_tasks.pop();
      lk.unlock();
      t.run();
      lk.lock();
      --m_outstandingTasks;
      m_backcv.notify_all();
    }
  }

public:
  void addPipeline(ObjToken<IShaderPipeline> pipeline) {
    std::lock_guard<std::mutex> lk(m_mt);
    m_tasks.emplace(pipeline);
    ++m_outstandingTasks;
    m_cv.notify_one();
  }

  void waitUntilReady() {
    std::unique_lock<std::mutex> lk(m_mt);
    m_backcv.wait(lk, [this]() { return m_outstandingTasks == 0 || !m_running; });
  }

  PipelineCompileQueue() {
    unsigned int numThreads = std::thread::hardware_concurrency();
    if (numThreads > 1)
      --numThreads;
    m_threads.reserve(numThreads);
    for (unsigned int i = 0; i < numThreads; ++i)
      m_threads.emplace_back(std::bind(&PipelineCompileQueue::worker, this));
  }

  ~PipelineCompileQueue() {
    m_running = false;
    m_cv.notify_all();
    for (auto& t : m_threads) t.join();
  }
};

} // namespace boo
