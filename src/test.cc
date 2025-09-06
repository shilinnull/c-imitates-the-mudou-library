#include <iostream>
#include "Timestamp.h"
#include "InetAddress.h"

int main()
{
    InetAddress addr(8080);
    std::cout << addr.toIPPort() << std::endl;
    return 0;
}

// int main()
// {
//     std::cout << Timestamp::now().toString() << std::endl;
//     return 0;
// }