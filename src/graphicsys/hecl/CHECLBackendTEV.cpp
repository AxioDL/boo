#include "graphicsys/hecl/IHECLBackend.hpp"

class CHECLBackendTEV final : public IHECLBackend
{
public:
    CHECLBackendTEV(const CHECLLexer& lexer)
    {
    }
    Type getType() const {return TEV;}
};

IHECLBackend* NewHECLBackendTEV(const CHECLLexer& lexer)
{
    return new CHECLBackendTEV(lexer);
}
