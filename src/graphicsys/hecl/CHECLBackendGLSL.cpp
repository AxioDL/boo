#include "graphicsys/hecl/IHECLBackend.hpp"

class CHECLBackendGLSL : public IHECLBackend
{
public:
    CHECLBackendGLSL(const CHECLLexer& lexer)
    {
    }
    Type getType() const {return GLSL;}
};

IHECLBackend* NewHECLBackendGLSL(const CHECLLexer& lexer)
{
    return new CHECLBackendGLSL(lexer);
}


class CHECLBackendGLSLCafe final : public CHECLBackendGLSL
{
public:
    CHECLBackendGLSLCafe(const CHECLLexer& lexer)
    : CHECLBackendGLSL(lexer)
    {
    }
    Type getType() const {return GLSL_CAFE;}
};

IHECLBackend* NewHECLBackendGLSLCafe(const CHECLLexer& lexer)
{
    return new CHECLBackendGLSLCafe(lexer);
}

