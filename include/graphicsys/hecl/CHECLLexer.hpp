#ifndef CHECLLEXER_HPP
#define CHECLLEXER_HPP

#include <string>
#include "graphicsys/CGFXVertexLayoutBase.hpp"

class CHECLLexer
{
    const CGFXVertexLayoutBase& m_vertLayout;
public:
    CHECLLexer(const CGFXVertexLayoutBase& vertLayout,
               const std::string& colorHECL);
    CHECLLexer(const CGFXVertexLayoutBase& vertLayout,
               const std::string& colorHECL,
               const std::string& alphaHECL);

    inline const CGFXVertexLayoutBase& getVertLayout() const {return m_vertLayout;}
};

#endif // CHECLLEXER_HPP
