
#include <stdio.h>
#include <boo.hpp>

int main(int argc, char** argv)
{
    IContext* ctx = new CContext;

    if (ctx->create())
    {
    }

    delete ctx;
    return 0;
}
