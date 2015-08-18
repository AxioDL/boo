#ifndef HECLEXPRESSIONS_HPP
#define HECLEXPRESSIONS_HPP

#include <string>
#include "IHECLBackend.hpp"

class IHECLExpression
{
    /* Traverse expression tree and assemble
     * backend-specific stage objects */
    virtual IHECLBackendStage* recursiveStages(IHECLBackend& backend) const=0;
};

class CHECLNumberLiteral final : IHECLExpression
{
};

class CHECLVector final : IHECLExpression
{
};

class CHECLTextureSample final : IHECLExpression
{
};

class CHECLTextureGatherSample final : IHECLExpression
{
};

class CHECLMulOperation final : IHECLExpression
{
};

class CHECLAddOperation final : IHECLExpression
{
};

class CHECLSubOperation final : IHECLExpression
{
};

class CHECLRoot final : IHECLExpression
{
};

#endif // HECLEXPRESSIONS_HPP
