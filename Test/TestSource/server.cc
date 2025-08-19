#include "../../source/server.hpp"

void HadnleClose(Channel *channel)
{
    std::cout << "关闭连接！\n";
    channel->Remove();
    delete channel;
}

void HandleRead(Channel *channel)
{
    int fd = channel->Fd();
    char buf[1024]{0};
    int ret = recv(fd, buf, 1023, 0);
    if (ret < 0)
        return HadnleClose(channel);
    std::cout << "recv: " << buf << std::endl;
    channel->EnableWrite(); // 发送数据后再次开启可写事件监听
}
void HandleWrite(Channel *channel)
{
    int fd = channel->Fd();
    const char *msg = "hello world";
    int ret = send(fd, msg, strlen(msg), 0);
    if (ret < 0)
        return HadnleClose(channel);
    channel->DisableWrite(); // 发送完数据后关闭可写事件监听
}

void HandleError(Channel *channel)
{
    return HadnleClose(channel);
}
void HandleEvent(Channel *channel)
{
    std::cout << "有一个事件来了！\n";
}
void Accepter(Poller *poller, Channel *lst_channel)
{
    int fd = lst_channel->Fd();
    int newfd = accept(fd, NULL, NULL);
    if (newfd < 0)
        return;
    Channel *channel = new Channel(poller, newfd);
    channel->SetReadCallback(std::bind(HandleRead, channel));
    channel->SetWriteCallback(std::bind(HandleWrite, channel));
    channel->SetCloseCallback(std::bind(HadnleClose, channel));
    channel->SetErrorCallback(std::bind(HandleError, channel));
    channel->SetEventCallback(std::bind(HandleEvent, channel));
    channel->EnableRead(); // 启动可读事件监听
}

int main()
{
    Socket lst_sock;
    Poller poller;
    lst_sock.CreateServerSocket(8500);
    Channel channel(&poller, lst_sock.Fd());
    channel.SetReadCallback(std::bind(Accepter, &poller, &channel)); // 设置可读事件回调
    channel.EnableRead();                                            // 启动可读事件监听
    for (;;)
    {
        std::vector<Channel *> active_channels;
        poller.Poll(&active_channels);
        for (auto &a : active_channels)
            a->HandleEvent();
    }
    lst_sock.Close();
    return 0;
}

#if 0
int main()
{
    Socket lst_sock;
    lst_sock.CreateServerSocket(8500);
    while (1)
    {
        int newfd = lst_sock.Accept();
        if (newfd < 0)
        {
            continue;
        }
        Socket cli_sock(newfd);
        char buf[1024] = {0};
        int ret = cli_sock.Recv(buf, 1023);
        if (ret < 0)
        {
            cli_sock.Close();
            continue;
        }
        cli_sock.Send(buf, ret);
        cli_sock.Close();
    }
    lst_sock.Close();
    return 0;
}
#endif