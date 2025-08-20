#include "../../source/server.hpp"
#include <iostream>

int main()
{
    Socket cli_sock;
    cli_sock.CreateClientSocket(8500, "127.0.0.1");
    for (int i = 0; i < 5; i++)
    {
        std::string s = "hello world!";
        cli_sock.Send(s.c_str(), s.size());
        LOG(LogLevel::DEBUG) << "send: " << s;
        char buf[1024]{0};
        cli_sock.Recv(buf, 1023);
        LOG(LogLevel::DEBUG) << buf;
        sleep(1);
    }
    while(1) sleep(1);
    return 0;
}

#if 0
int main()
{
    Socket cli_sock;
    cli_sock.CreateClientSocket(8500, "127.0.0.1");
    std::string str = "hello world!";
    cli_sock.Send(str.c_str(), str.size());
    char buf[1024]{0};
    cli_sock.Recv(buf, 1023);
    printf("%s", buf);
    return 0;
}

#endif