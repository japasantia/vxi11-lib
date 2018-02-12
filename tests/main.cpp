#include <QCoreApplication>

#include "vxi11_lib.h"

int Vxi11_SRQCallback(char* args);

int main(int argc, char *argv[])
{
    VxiHandle handle;

    //int ret = Vxi11_OpenDevice(&handle, "186.88.137.129");

    //ret = Vxi11_Send(&handle, "*IDN?\n", 6);

    Vxi11_RegisterSRQHandler(Vxi11_SRQCallback);
}

int Vxi11_SRQCallback(char* args)
{
    printf(args);
}
