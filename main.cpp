#include "libBoo.hpp"
#include <iostream>

int main(int argc, char* argv[])
{
    IContext* context = new CContext();

    std::cout << context->name()      << std::endl;
    std::cout << context->version()   << std::endl;
    std::cout << context->depthSize() << std::endl;
    delete context;
}
