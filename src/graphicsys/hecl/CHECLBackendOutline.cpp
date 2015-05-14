#include "graphicsys/hecl/IHECLBackend.hpp"

class CHECLBackendOutline final : public IHECLBackend
{
public:
    CHECLBackendOutline(const CHECLLexer& lexer)
    {
    }
    Type getType() const {return OUTLINE;}
};

IHECLBackend* NewHECLBackendOutline(const CHECLLexer& lexer)
{
    return new CHECLBackendOutline(lexer);
}

