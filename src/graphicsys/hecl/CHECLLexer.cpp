#include "graphicsys/hecl/CHECLLexer.hpp"

CHECLLexer::CHECLLexer(const CGFXVertexLayoutBase& vertLayout,
                       const std::string& colorHECL)
: m_vertLayout(vertLayout)
{
}

CHECLLexer::CHECLLexer(const CGFXVertexLayoutBase& vertLayout,
                       const std::string& colorHECL,
                       const std::string& alphaHECL)
: m_vertLayout(vertLayout)
{
}
