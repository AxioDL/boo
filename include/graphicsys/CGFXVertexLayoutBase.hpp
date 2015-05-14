#ifndef CGFXVERTEXLAYOUTBASE_HPP
#define CGFXVERTEXLAYOUTBASE_HPP

class CGFXVertexLayoutBase
{
    unsigned m_uvCount;
    unsigned m_weightCount;
public:
    CGFXVertexLayoutBase(unsigned uvCount=0, unsigned weightCount=0)
    : m_uvCount(uvCount),
      m_weightCount(weightCount) {}
    virtual ~CGFXVertexLayoutBase() {}

    inline unsigned uvCount() {return m_uvCount;}
    inline unsigned weightCount() {return m_weightCount;}
    inline bool isSkinned() {return m_weightCount > 0;}

};

#endif // CGFXVERTEXLAYOUTBASE_HPP
