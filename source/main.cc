#include <iostream>
#include "server.hpp"

void TestConnection()
{
    Buffer buffer;
    std::string s = "hello world";
    buffer.WriteStringAndPush(s);                    // 写入字符串并推送
    std::cout << buffer.ReadableSize() << std::endl; // 读取可读字节数 11

    std::string t;
    t = buffer.ReadAsString(buffer.ReadableSize());
    std::cout << t << std::endl; // hello world

    std::string t1;
    t1 = buffer.ReadAsStringAndPop(buffer.ReadableSize());
    std::cout << t1 << std::endl;                    // hello world
    std::cout << buffer.ReadableSize() << std::endl; // 0

    for (int i = 0; i < 100; i++)
    {
        buffer.WriteStringAndPush("hello world " + std::to_string(i) + "\n");
    }

    while (buffer.ReadableSize() > 0)
    {
        LOG(LogLevel::DEBUG) << "剩余字节数: " << buffer.ReadableSize();
        std::string t2;
        t2 = buffer.GetLineAndPop();
        LOG(LogLevel::DEBUG) << t2;
    }
    LOG(LogLevel::DEBUG) << "剩余字节数: " << buffer.ReadableSize();
}

int main()
{
    TestConnection();

    return 0;
}
