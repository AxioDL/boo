#include "graphicsys/hecl/IHECLBackend.hpp"

class CHECLBackendMetal final : public IHECLBackend
{
public:
    CHECLBackendMetal(const CHECLLexer& lexer)
    {
    }
    Type getType() const {return METAL;}
};

IHECLBackend* NewHECLBackendMetal(const CHECLLexer& lexer)
{
    return new CHECLBackendMetal(lexer);
}
