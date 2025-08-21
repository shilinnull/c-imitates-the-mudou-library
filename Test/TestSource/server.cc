#include "../../source/server.hpp"

std::unordered_map<ino64_t, PtrConnection> _conns;
uint64_t conn_id;


void OnMessage(const PtrConnection &conn, Buffer *buf)
{
    LOG(LogLevel::DEBUG) << buf->ReadPosition();
    buf->MoveReadOffset(buf->ReadableSize());
    std::string s = "hello world";
    conn->Send(s.c_str(), s.size());
    conn->Shutdown();
}

void ConnectionDestory(const PtrConnection &conn)
{
    _conns.erase(conn->Id());
}

void OnConnected(const PtrConnection &conn)
{
    LOG(LogLevel::DEBUG) << "new Connection: " << conn.get();
}

void Accepter(EventLoop *loop, Channel *lst_channel)
{
    int fd = lst_channel->Fd();
    int newfd = accept(fd, nullptr, nullptr);
    if (newfd < 0)
        return;

    conn_id++;

    PtrConnection conn(new Connection(loop, conn_id, newfd));
    conn->SetMessageCallback(std::bind(OnMessage, std::placeholders::_1, std::placeholders::_2));
    conn->SetSrvClosedCallback(std::bind(ConnectionDestory, std::placeholders::_1));
    conn->SetConnectedCallback(std::bind(OnConnected, std::placeholders::_1));
    conn->EnableInactiveRelease(10);
    conn->Established();
    _conns.insert(std::make_pair(conn_id, conn));
}

int main()
{
    srand(time(nullptr));
    Socket lst_sock;
    EventLoop loop;
    lst_sock.CreateServerSocket(8500);
    Channel channel(&loop, lst_sock.Fd());
    channel.SetReadCallback(std::bind(Accepter, &loop, &channel)); // 设置可读事件回调
    channel.EnableRead();                                          // 启动可读事件监听
    for (;;)
    {
        loop.Start(); // 启动事件循环
    }
    lst_sock.Close();
    return 0;
}

#if 0
void HandleClose(Channel *channel)
{
    LOG(LogLevel::DEBUG) << "close: " << channel->Fd();
    channel->Remove();
    delete channel;
}

void HandleRead(Channel *channel)
{
    int fd = channel->Fd();
    char buf[1024]{0};
    int ret = recv(fd, buf, 1023, 0);
    if (ret <= 0)
        return HandleClose(channel);
    LOG(LogLevel::DEBUG) << "recv: " << ret << " bytes from " << fd;
    channel->EnableWrite(); // 发送数据后再次开启可写事件监听(回复)
}
void HandleWrite(Channel *channel)
{
    int fd = channel->Fd();
    const char *msg = "写入数据: hello world";
    int ret = send(fd, msg, strlen(msg), 0);
    if (ret < 0)
        return HandleClose(channel);
    channel->DisableWrite(); // 发送完数据后关闭可写事件监听
}

void HandleError(Channel *channel)
{
    return HandleClose(channel);
}
void HandleEvent(EventLoop *loop, Channel *channel, uint64_t timerid)
{
    LOG(LogLevel::DEBUG) << "有新的事件来了";
    loop->TimerRefresh(timerid); // 刷新活跃度
}
void Accepter(EventLoop *loop, Channel *lst_channel)
{
    int fd = lst_channel->Fd();
    int newfd = accept(fd, nullptr, nullptr);
    if (newfd < 0)
        return;

    uint64_t timerid = rand() % 10000;
    Channel *channel = new Channel(loop, newfd);
    channel->SetReadCallback(std::bind(HandleRead, channel));
    channel->SetWriteCallback(std::bind(HandleWrite, channel));
    channel->SetCloseCallback(std::bind(HandleClose, channel));
    channel->SetErrorCallback(std::bind(HandleError, channel));
    channel->SetEventCallback(std::bind(HandleEvent, loop, channel, timerid));
    loop->TimerAdd(timerid, 10, std::bind(HandleClose, channel));
    channel->EnableRead(); // 启动可读事件监听
}

int main()
{
    srand(time(nullptr));
    Socket lst_sock;
    EventLoop loop;
    lst_sock.CreateServerSocket(8500);
    Channel channel(&loop, lst_sock.Fd());
    channel.SetReadCallback(std::bind(Accepter, &loop, &channel)); // 设置可读事件回调
    channel.EnableRead();                                          // 启动可读事件监听
    for (;;)
    {
        loop.Start(); // 启动事件循环
    }
    lst_sock.Close();
    return 0;
}


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