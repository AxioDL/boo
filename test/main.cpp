
#include <stdio.h>
#include <boo.hpp>

int main(int argc, char** argv)
{
    IGraphicsContext* ctx = new CGraphicsContext;

    if (ctx->create())
    {
    }

    delete ctx;
    return 0;
}
