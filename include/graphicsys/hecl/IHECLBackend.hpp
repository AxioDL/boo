#ifndef IHECLBACKEND_HPP
#define IHECLBACKEND_HPP

#include <string>
#include "CHECLLexer.hpp"

class IHECLBackend;

IHECLBackend* NewHECLBackendOutline(const CHECLLexer& lexer);
IHECLBackend* NewHECLBackendGLSL(const CHECLLexer& lexer);
IHECLBackend* NewHECLBackendHLSL(const CHECLLexer& lexer);
IHECLBackend* NewHECLBackendMetal(const CHECLLexer& lexer);
IHECLBackend* NewHECLBackendTEV(const CHECLLexer& lexer);
IHECLBackend* NewHECLBackendGLSLCafe(const CHECLLexer& lexer);

class IHECLBackend
{
public:
    enum Type
    {
        AUTO      = 0,
        OUTLINE   = 1,
        GLSL      = 2,
        HLSL      = 3,
        METAL     = 4,
        TEV       = 5,
        GLSL_CAFE = 6
    };
    virtual Type getType() const=0;

    virtual bool hasVertexSourceForm() const {return false;}
    virtual bool hasFragmentSourceForm() const {return false;}
    virtual bool hasVertexBinaryForm() const {return false;}
    virtual bool hasFragmentBinaryForm() const {return false;}
    virtual bool hasBinaryForm() const {return false;}

    virtual std::string* emitNewVertexSource() {return NULL;}
    virtual std::string* emitNewFragmentSource() {return NULL;}
    virtual void* emitNewVertexBinary(size_t& szOut) {szOut = 0;return NULL;}
    virtual void* emitNewFragmentBinary(size_t& szOut) {szOut = 0;return NULL;}
    virtual void* emitNewBinary(size_t& szOut) {szOut = 0;return NULL;}

    static inline IHECLBackend* NewHECLBackend(Type backendType, const CHECLLexer& lexer)
    {
        switch (backendType)
        {
        case AUTO:
#if HW_RVL
            return NewHECLBackendTEV(lexer);
#elif HW_CAFE
            return NewHECLBackendGLSLCafe(lexer);
#elif _WIN32
            return NewHECLBackendHLSL(lexer);
#else
            return NewHECLBackendGLSL(lexer);
#endif
        case OUTLINE:
            return NewHECLBackendOutline(lexer);
        case GLSL:
            return NewHECLBackendGLSL(lexer);
        case HLSL:
            return NewHECLBackendHLSL(lexer);
        case METAL:
            return NewHECLBackendMetal(lexer);
        case TEV:
            return NewHECLBackendTEV(lexer);
        case GLSL_CAFE:
            return NewHECLBackendGLSLCafe(lexer);
        }
        return NULL;
    }

};


#endif // IHECLBACKEND_HPP
