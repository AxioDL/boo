#ifndef BOO_GRAPHICSDEV_COMMON_HPP
#define BOO_GRAPHICSDEV_COMMON_HPP

/* Private header for managing shader data
 * binding lifetimes through rendering cycle */

#include <atomic>
#include <vector>
#include <mutex>
#include "boo/graphicsdev/IGraphicsDataFactory.hpp"

namespace boo
{

struct BaseGraphicsData;
struct BaseGraphicsPool;

template<class NodeCls, class DataCls = BaseGraphicsData>
struct GraphicsDataNode;

/** Inherited by data factory implementations to track the head data and pool nodes */
struct GraphicsDataFactoryHead
{
    std::mutex m_dataMutex;
    BaseGraphicsData* m_dataHead = nullptr;
    BaseGraphicsPool* m_poolHead = nullptr;
};

/** Linked-list iterator shareable by data container types */
template<class T>
class DataIterator
{
    T* m_node;
public:
    using value_type = T;
    using pointer = T*;
    using reference = T&;
    using iterator_category = std::bidirectional_iterator_tag;

    explicit DataIterator(T* node) : m_node(node) {}
    T& operator*() const { return *m_node; }
    bool operator!=(const DataIterator& other) const { return m_node != other.m_node; }
    DataIterator& operator++() { m_node = m_node->m_next; return *this; }
    DataIterator& operator--() { m_node = m_node->m_prev; return *this; }
};

/** Private generalized data container class.
 *  Keeps head pointers to all graphics objects by type
 */
struct BaseGraphicsData : IObj
{
    GraphicsDataFactoryHead& m_head;
    BaseGraphicsData* m_next;
    BaseGraphicsData* m_prev = nullptr;
    GraphicsDataNode<IShaderPipeline, BaseGraphicsData>* m_SPs = nullptr;
    GraphicsDataNode<IShaderDataBinding, BaseGraphicsData>* m_SBinds = nullptr;
    GraphicsDataNode<IGraphicsBufferS, BaseGraphicsData>* m_SBufs = nullptr;
    GraphicsDataNode<IGraphicsBufferD, BaseGraphicsData>* m_DBufs = nullptr;
    GraphicsDataNode<ITextureS, BaseGraphicsData>* m_STexs = nullptr;
    GraphicsDataNode<ITextureSA, BaseGraphicsData>* m_SATexs = nullptr;
    GraphicsDataNode<ITextureD, BaseGraphicsData>* m_DTexs = nullptr;
    GraphicsDataNode<ITextureR, BaseGraphicsData>* m_RTexs = nullptr;
    GraphicsDataNode<IVertexFormat, BaseGraphicsData>* m_VFmts = nullptr;
    template<class T> GraphicsDataNode<T, BaseGraphicsData>*& getHead();
    template<class T> size_t countForward()
    { auto* head = getHead<T>(); return head ? head->countForward() : 0; }

    explicit BaseGraphicsData(GraphicsDataFactoryHead& head)
    : m_head(head)
    {
        std::lock_guard<std::mutex> lk(m_head.m_dataMutex);
        m_next = head.m_dataHead;
        if (m_next)
            m_next->m_prev = this;
        head.m_dataHead = this;
    }
    ~BaseGraphicsData()
    {
        std::lock_guard<std::mutex> lk(m_head.m_dataMutex);
        if (m_prev)
        {
            if (m_next)
                m_next->m_prev = m_prev;
            m_prev->m_next = m_next;
        }
        else
        {
            if (m_next)
                m_next->m_prev = nullptr;
            m_head.m_dataHead = m_next;
        }
    }

    using iterator = DataIterator<BaseGraphicsData>;
    iterator begin() { return iterator(this); }
    iterator end() { return iterator(nullptr); }
};

template <> inline GraphicsDataNode<IShaderPipeline, BaseGraphicsData>*&
BaseGraphicsData::getHead<IShaderPipeline>() { return m_SPs; }
template <> inline GraphicsDataNode<IShaderDataBinding, BaseGraphicsData>*&
BaseGraphicsData::getHead<IShaderDataBinding>() { return m_SBinds; }
template <> inline GraphicsDataNode<IGraphicsBufferS, BaseGraphicsData>*&
BaseGraphicsData::getHead<IGraphicsBufferS>() { return m_SBufs; }
template <> inline GraphicsDataNode<IGraphicsBufferD, BaseGraphicsData>*&
BaseGraphicsData::getHead<IGraphicsBufferD>() { return m_DBufs; }
template <> inline GraphicsDataNode<ITextureS, BaseGraphicsData>*&
BaseGraphicsData::getHead<ITextureS>() { return m_STexs; }
template <> inline GraphicsDataNode<ITextureSA, BaseGraphicsData>*&
BaseGraphicsData::getHead<ITextureSA>() { return m_SATexs; }
template <> inline GraphicsDataNode<ITextureD, BaseGraphicsData>*&
BaseGraphicsData::getHead<ITextureD>() { return m_DTexs; }
template <> inline GraphicsDataNode<ITextureR, BaseGraphicsData>*&
BaseGraphicsData::getHead<ITextureR>() { return m_RTexs; }
template <> inline GraphicsDataNode<IVertexFormat, BaseGraphicsData>*&
BaseGraphicsData::getHead<IVertexFormat>() { return m_VFmts; }

/** Private generalized pool container class.
 *  Keeps head pointer to exactly one dynamic buffer while otherwise conforming to BaseGraphicsData
 */
struct BaseGraphicsPool : IObj
{
    GraphicsDataFactoryHead& m_head;
    BaseGraphicsPool* m_next;
    BaseGraphicsPool* m_prev = nullptr;
    GraphicsDataNode<IGraphicsBufferD, BaseGraphicsPool>* m_DBufs = nullptr;
    template<class T> GraphicsDataNode<T, BaseGraphicsPool>*& getHead();
    template<class T> size_t countForward()
    { auto* head = getHead<T>(); return head ? head->countForward() : 0; }

    explicit BaseGraphicsPool(GraphicsDataFactoryHead& head)
    : m_head(head)
    {
        std::lock_guard<std::mutex> lk(m_head.m_dataMutex);
        m_next = head.m_poolHead;
        if (m_next)
            m_next->m_prev = this;
        head.m_poolHead = this;
    }
    ~BaseGraphicsPool()
    {
        std::lock_guard<std::mutex> lk(m_head.m_dataMutex);
        if (m_prev)
        {
            if (m_next)
                m_next->m_prev = m_prev;
            m_prev->m_next = m_next;
        }
        else
        {
            if (m_next)
                m_next->m_prev = nullptr;
            m_head.m_poolHead = m_next;
        }
    }

    using iterator = DataIterator<BaseGraphicsPool>;
    iterator begin() { return iterator(this); }
    iterator end() { return iterator(nullptr); }
};

template <> inline GraphicsDataNode<IGraphicsBufferD, BaseGraphicsPool>*&
BaseGraphicsPool::getHead<IGraphicsBufferD>() { return m_DBufs; }

/** Private generalised graphics object node.
 *  Keeps a strong reference to the data pool that it's a member of;
 *  as well as doubly-linked pointers to same-type sibling objects
 */
template<class NodeCls, class DataCls>
struct GraphicsDataNode : NodeCls
{
    ObjToken<DataCls> m_data;
    GraphicsDataNode<NodeCls, DataCls>* m_next;
    GraphicsDataNode<NodeCls, DataCls>* m_prev = nullptr;

    explicit GraphicsDataNode(const ObjToken<DataCls>& data)
    : m_data(data)
    {
        std::lock_guard<std::mutex> lk(m_data->m_head.m_dataMutex);
        m_next = data->template getHead<NodeCls>();
        if (m_next)
            m_next->m_prev = this;
        data->template getHead<NodeCls>() = this;
    }
    ~GraphicsDataNode()
    {
        std::lock_guard<std::mutex> lk(m_data->m_head.m_dataMutex);
        if (m_prev)
        {
            if (m_next)
                m_next->m_prev = m_prev;
            m_prev->m_next = m_next;
        }
        else
        {
            if (m_next)
                m_next->m_prev = nullptr;
            m_data->template getHead<NodeCls>() = m_next;
        }
    }

    class iterator
    {
        GraphicsDataNode<NodeCls, DataCls>* m_node;
    public:
        using value_type = NodeCls;
        using pointer = NodeCls*;
        using reference = NodeCls&;
        using iterator_category = std::bidirectional_iterator_tag;

        explicit iterator(GraphicsDataNode<NodeCls, DataCls>* node) : m_node(node) {}
        NodeCls& operator*() const { return *m_node; }
        bool operator!=(const iterator& other) const { return m_node != other.m_node; }
        iterator& operator++() { m_node = m_node->m_next; return *this; }
        iterator& operator--() { m_node = m_node->m_prev; return *this; }
    };

    iterator begin() { return iterator(this); }
    iterator end() { return iterator(nullptr); }

    size_t countForward()
    {
        size_t ret = 0;
        for (auto& n : *this)
            ++ret;
        return ret;
    }
};

/** Hash table entry for owning sharable shader objects */
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
