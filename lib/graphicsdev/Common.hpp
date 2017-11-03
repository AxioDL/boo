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

    explicit BaseGraphicsData(GraphicsDataFactoryHead& head)
    : m_head(head)
    {
        std::lock_guard<std::mutex> lk(m_head.m_dataMutex);
        m_next = head.m_dataHead;
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
                m_next->m_prev = m_head.m_dataHead;
            m_head.m_dataHead = m_next;
        }
    }

    class iterator
    {
        BaseGraphicsData* m_node;
    public:
        using value_type = BaseGraphicsData;
        using pointer = BaseGraphicsData*;
        using reference = BaseGraphicsData&;
        using iterator_category = std::bidirectional_iterator_tag;

        explicit iterator(BaseGraphicsData* node) : m_node(node) {}
        BaseGraphicsData& operator*() const { return *m_node; }
        bool operator!=(const iterator& other) const { return m_node != other.m_node; }
        iterator& operator++() { m_node = m_node->m_next; return *this; }
        iterator& operator--() { m_node = m_node->m_prev; return *this; }
    };

    iterator begin() { return iterator(this); }
    iterator end() { return iterator(nullptr); }
};

template <> GraphicsDataNode<IShaderPipeline, BaseGraphicsData>*&
BaseGraphicsData::getHead<IShaderPipeline>() { return m_SPs; }
template <> GraphicsDataNode<IShaderDataBinding, BaseGraphicsData>*&
BaseGraphicsData::getHead<IShaderDataBinding>() { return m_SBinds; }
template <> GraphicsDataNode<IGraphicsBufferS, BaseGraphicsData>*&
BaseGraphicsData::getHead<IGraphicsBufferS>() { return m_SBufs; }
template <> GraphicsDataNode<IGraphicsBufferD, BaseGraphicsData>*&
BaseGraphicsData::getHead<IGraphicsBufferD>() { return m_DBufs; }
template <> GraphicsDataNode<ITextureS, BaseGraphicsData>*&
BaseGraphicsData::getHead<ITextureS>() { return m_STexs; }
template <> GraphicsDataNode<ITextureSA, BaseGraphicsData>*&
BaseGraphicsData::getHead<ITextureSA>() { return m_SATexs; }
template <> GraphicsDataNode<ITextureD, BaseGraphicsData>*&
BaseGraphicsData::getHead<ITextureD>() { return m_DTexs; }
template <> GraphicsDataNode<ITextureR, BaseGraphicsData>*&
BaseGraphicsData::getHead<ITextureR>() { return m_RTexs; }
template <> GraphicsDataNode<IVertexFormat, BaseGraphicsData>*&
BaseGraphicsData::getHead<IVertexFormat>() { return m_VFmts; }

/** Private generalized pool container class.
 *  Keeps head pointer to exactly one dynamic buffer while otherwise conforming to IGraphicsData
 */
struct BaseGraphicsPool : IObj
{
    GraphicsDataFactoryHead& m_head;
    BaseGraphicsPool* m_next;
    BaseGraphicsPool* m_prev = nullptr;
    GraphicsDataNode<IGraphicsBufferD, BaseGraphicsPool>* m_DBufs = nullptr;
    template<class T> GraphicsDataNode<T, BaseGraphicsPool>*& getHead();

    explicit BaseGraphicsPool(GraphicsDataFactoryHead& head)
    : m_head(head)
    {
        std::lock_guard<std::mutex> lk(m_head.m_dataMutex);
        m_next = head.m_poolHead;
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
                m_next->m_prev = m_head.m_poolHead;
            m_head.m_poolHead = m_next;
        }
    }

    class iterator
    {
        BaseGraphicsPool* m_node;
    public:
        using value_type = BaseGraphicsPool;
        using pointer = BaseGraphicsPool*;
        using reference = BaseGraphicsPool&;
        using iterator_category = std::bidirectional_iterator_tag;

        explicit iterator(BaseGraphicsPool* node) : m_node(node) {}
        BaseGraphicsPool& operator*() const { return *m_node; }
        bool operator!=(const iterator& other) const { return m_node != other.m_node; }
        iterator& operator++() { m_node = m_node->m_next; return *this; }
        iterator& operator--() { m_node = m_node->m_prev; return *this; }
    };

    iterator begin() { return iterator(this); }
    iterator end() { return iterator(nullptr); }
};

template <> GraphicsDataNode<IGraphicsBufferD, BaseGraphicsPool>*&
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
                m_next->m_prev = m_data->template getHead<NodeCls>();
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
