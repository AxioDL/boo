#include "graphicsys/hecl/IHECLBackend.hpp"

class CHECLBackendHLSL final : public IHECLBackend
{
public:
    CHECLBackendHLSL(const CHECLLexer& lexer)
    {
    }
    Type getType() const {return HLSL;}
};

IHECLBackend* NewHECLBackendHLSL(const CHECLLexer& lexer)
{
    return new CHECLBackendHLSL(lexer);
}
