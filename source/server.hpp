#ifndef __SERVER_HPP__
#define __SERVER_HPP__

#include <string>
#include <cstring>
#include <vector>
#include <unordered_map>
#include <functional>
#include <cassert>
#include <cstdint>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/epoll.h>

#include "Logger.hpp"

#define BUFFER_DEFAULT_SIZE 1024 // 默认缓冲区大小
#define MAX_EPOLLEVENTS 1024     // 最大epoll事件数
#define MAX_LISTEN 1024          // 最大监听队列长度

class Buffer
{
public:
    Buffer() : _buffer(BUFFER_DEFAULT_SIZE), _reader_idx(0), _writer_idx(0) {}
    ~Buffer() {}

    // 获取缓冲区起始地址
    char *Begin() { return &*_buffer.begin(); }

    // 获取当前写入起始地址
    char *WritePosition() { return Begin() + _writer_idx; }

    // 获取当前读取起始地址
    char *ReadPosition() { return Begin() + _reader_idx; }

    // 获取前沿空闲空间大小--写偏移之后的空闲空间 写偏移之后的空闲空间, 总体空间大小减去写偏移
    uint64_t FrontFreeSize() { return _reader_idx; }

    // 获取后沿空闲空间大小--读偏移之前的空闲空间
    uint64_t BackFreeSize() { return _buffer.size() - _writer_idx; }

    // 获取可读数据大小       写偏移 - 读偏移
    uint64_t ReadableSize() { return _writer_idx - _reader_idx; }

    // 将读偏移向后移动
    void MoveReadOffset(uint64_t len)
    {
        if (len == 0)
            return;
        // 向后移动的大小，必须小于可读数据大小
        assert(len <= ReadableSize());
        _reader_idx += len;
    }

    // 将写偏移向后移动
    void MoveWriteOffset(uint64_t len)
    {
        // 向后移动的大小，必须小于当前后边的空闲空间大小
        assert(len <= BackFreeSize());
        _writer_idx += len;
    }

    // 确保可写空间足够（整体空闲空间够了就移动数据，否则就扩容）
    void EnsureWriteSpace(uint64_t len)
    {
        // 如果末尾空闲空间大小足够，直接返回
        if (BackFreeSize() >= len)
            return;
        // 末尾空闲空间不够，则判断：加上起始位置的空闲空间大小是否足够, 够了就将数据移动到起始位置
        if (FrontFreeSize() + BackFreeSize() >= len)
        {
            // 将数据移动到起始位置
            size_t rsz = ReadableSize();
            std::copy(ReadPosition(), ReadPosition() + rsz, Begin());
            _reader_idx = 0;
            _writer_idx = rsz;
        }
        else
        {
            // 总体空间不够，则需要扩容，不移动数据，直接给写偏移之后扩容足够空间即可
            _buffer.resize(_writer_idx + len);
            LOG(LogLevel::DEBUG) << "RESIZE " << _writer_idx + len;
        }
    }

    // 写入数据
    void Write(const void *data, uint64_t len)
    {
        // 1. 保证有足够空间，2. 拷贝数据进去
        EnsureWriteSpace(len);
        std::copy(static_cast<const char *>(data), static_cast<const char *>(data) + len, WritePosition());
    }

    // 写入数据并压入
    void WriteAndPush(const void *data, uint64_t len)
    {
        Write(data, len);
        MoveWriteOffset(len);
    }

    // 写入字符串
    void WriteString(const std::string &s)
    {
        Write(s.c_str(), s.size());
    }

    // 写入字符串并压入
    void WriteStringAndPush(const std::string &s)
    {
        WriteString(s);
        MoveWriteOffset(s.size());
    }

    // 写入缓冲区
    void WriteBuffer(Buffer &buf)
    {
        Write(buf.ReadPosition(), buf.ReadableSize());
    }

    // 写入缓冲区并压入
    void WriteBufferAndPush(Buffer &data)
    {
        WriteBuffer(data);
        MoveWriteOffset(data.ReadableSize());
    }

    // 读取数据
    void Read(void *buf, uint64_t len)
    {
        assert(len <= ReadableSize());
        std::copy(ReadPosition(), ReadPosition() + len, static_cast<char *>(buf));
    }

    // 读取数据并弹出
    void ReadAndPop(void *buf, uint64_t len)
    {
        Read(buf, len);
        MoveReadOffset(len);
    }

    // 读取字符串
    std::string ReadAsString(uint64_t len)
    {
        // 要求要获取的数据大小必须小于可读数据大小
        assert(len <= ReadableSize());
        std::string s(ReadPosition(), len);
        return s;
    }

    // 读取字符串并弹出
    std::string ReadAsStringAndPop(uint64_t len)
    {
        assert(len <= ReadableSize());
        std::string s = ReadAsString(len);
        MoveReadOffset(len);
        return s;
    }

    // 找到CRLF
    char *FindCRLF()
    {
        char *p = (char *)memchr(ReadPosition(), '\n', ReadableSize());
        return p;
    }

    // 读取一行
    std::string GetLine()
    {
        char *pos = FindCRLF();
        if (pos == nullptr)
            return "";
        return ReadAsString(pos - ReadPosition() + 1); // 包含了\n
    }

    // 读取一行并弹出
    std::string GetLineAndPop()
    {
        std::string s = GetLine();
        MoveReadOffset(s.size());
        return s;
    }

    // 清空缓冲区
    void Clear()
    {
        _reader_idx = 0;
        _writer_idx = 0;
    }

private:
    std::vector<char> _buffer; // vector 缓冲区
    uint64_t _reader_idx;      // 读偏移
    uint64_t _writer_idx;      // 写偏移
};

class Socket
{
public:
    Socket() : _sockfd(-1) {}
    Socket(int sockfd) : _sockfd(sockfd) {}
    ~Socket() {}
    int Fd() const { return _sockfd; }

    // 创建套接字
    bool Create()
    {
        _sockfd = socket(AF_INET, SOCK_STREAM, 0);
        if (_sockfd < 0)
        {
            LOG(LogLevel::FATAL) << "Create socket failed";
            return false;
        }
        return true;
    }
    // 绑定地址信息
    bool Bind(const std::string &ip, uint16_t port)
    {
        struct sockaddr_in addr;
        addr.sin_family = AF_INET;                    // IPv4
        addr.sin_port = htons(port);                  // 端口号
        addr.sin_addr.s_addr = inet_addr(ip.c_str()); // IP地址
        socklen_t len = sizeof(struct sockaddr_in);
        int ret = bind(_sockfd, (struct sockaddr *)&addr, len);
        if (ret < 0)
        {
            LOG(LogLevel::FATAL) << "Bind socket failed";
            return false;
        }
        return true;
    }
    // 开始监听
    bool Listen(int backlog = MAX_LISTEN)
    {
        int ret = listen(_sockfd, backlog);
        if (ret < 0)
        {
            LOG(LogLevel::FATAL) << "Listen socket failed";
            return false;
        }
        return true;
    }
    // 向服务器发起连接
    bool Connect(const std::string &ip, uint16_t port)
    {
        struct sockaddr_in addr;
        addr.sin_family = AF_INET;                    // IPv4
        addr.sin_port = htons(port);                  // 端口号
        addr.sin_addr.s_addr = inet_addr(ip.c_str()); // IP地址
        socklen_t len = sizeof(struct sockaddr_in);
        int ret = connect(_sockfd, (sockaddr *)&addr, len);
        if (ret < 0)
        {
            LOG(LogLevel::WARNING) << "Connect socket failed";
            return false;
        }
        return true;
    }
    // 获取新连接
    int Accept()
    {
        int newfd = accept(_sockfd, nullptr, nullptr);
        if (newfd < 0)
        {
            LOG(LogLevel::WARNING) << "Accept socket failed";
            return -1;
        }
        return newfd;
    }
    // 接收数据
    ssize_t Recv(void *buf, size_t len, int flags = 0)
    {
        ssize_t n = recv(_sockfd, buf, len, flags);
        if (n < 0)
        {
            // EAGAIN 当前socket的接收缓冲区中没有数据了，在非阻塞的情况下才会有这个错误
            // EINTR  表示当前socket的阻塞等待，被信号打断了
            if (errno == EAGAIN || errno == EINTR)
                return 0; // 表示这次接收没有接收到数据
            else
            {
                LOG(LogLevel::ERROR) << "Recv socket failed";
                return -1;
            }
        }
        return n; // 返回实际接收的字节数
    }
    ssize_t NonBlockRecv(void *buf, size_t len, int flags = 0)
    {
        return Recv(buf, len, MSG_DONTWAIT); // MSG_DONTWAIT 表示当前接收为非阻塞
    }
    // 发送数据
    ssize_t Send(const void *buf, size_t len, int flags = 0)
    {
        ssize_t n = send(_sockfd, buf, len, flags);
        if (n < 0)
        {
            if (errno == EAGAIN || errno == EINTR)
                return 0; // 表示这次发送没有发送成功
            else
            {
                LOG(LogLevel::ERROR) << "Send socket failed";
                return -1;
            }
        }
        return n; // 返回实际发送的字节数
    }
    ssize_t NonBlockSend(const void *buf, size_t len, int flags = 0)
    {
        return Send(buf, len, MSG_DONTWAIT); // MSG_DONTWAIT 表示当前发送为非阻塞
    }
    // 关闭套接字
    void Close()
    {
        if (_sockfd != -1)
        {
            close(_sockfd);
            _sockfd = -1;
        }
    }
    // 创建一个服务端连接
    bool CreateServerSocket(uint16_t port, const std::string &ip = "0.0.0.0", bool block_flag = false)
    {
        // 1. 创建套接字, 2. 绑定地址，3. 开始监听，4. 设置非阻塞， 5. 启动地址重用
        if (!Create())
            return false;
        if (!Bind(ip, port))
            return false;
        if (!Listen())
            return false;
        if (block_flag)
            SetNonBlocking();
        EnableAddressReuse();
        return true;
    }
    // 创建一个客户端连接
    bool CreateClientSocket(uint16_t port, const std::string &ip)
    {
        // 1. 创建套接字, 2. 连接服务器
        if (!Create())
            return false;
        if (!Connect(ip, port))
            return false;
        return true;
    }
    // 开启地址端口重用
    void EnableAddressReuse()
    {
        int on = 1;
        setsockopt(_sockfd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on)); // 开启地址重用
        on = 1;
        setsockopt(_sockfd, SOL_SOCKET, SO_REUSEPORT, &on, sizeof(on)); // 开启端口重用
    }
    // 设置套接字非阻塞属性
    void SetNonBlocking()
    {
        int flags = fcntl(_sockfd, F_GETFL, 0);
        if (flags == -1)
        {
            LOG(LogLevel::WARNING) << "fcntl get flags failed";
            return;
        }
        flags = fcntl(_sockfd, F_SETFL, flags | O_NONBLOCK);
        if (flags == -1)
        {
            LOG(LogLevel::WARNING) << "fcntl set flags failed";
            return;
        }
        LOG(LogLevel::DEBUG) << "Set socket nonblocking";
    }

private:
    int _sockfd; // 套接字描述符
};

class Poller;

class Channel
{
    using EventCallback = std::function<void()>;

public:
    Channel(Poller *poller, int fd) : _fd(0), _poller(poller), _events(0), _revents(0) {}
    ~Channel() {}
    int Fd() const { return _fd; }
    uint32_t Events() const { return _events; }             // 获取想要监控的事件
    void SetREvents(uint32_t events) { _revents = events; } // 设置实际就绪的事件
    void SetReadCallback(const EventCallback &cb) { _read_callback = cb; }
    void SetWriteCallback(const EventCallback &cb) { _write_callback = cb; }
    void SetErrorCallback(const EventCallback &cb) { _error_callback = cb; }
    void SetCloseCallback(const EventCallback &cb) { _close_callback = cb; }
    void SetEventCallback(const EventCallback &cb) { _event_callback = cb; }
    // 当前是否监控了可读
    bool Readable()
    {
        return (_events & EPOLLIN);
        Update();
    }
    // 当前是否监控了可写
    bool Writable()
    {
        return (_events & EPOLLOUT);
        Update();
    }
    // 启动读事件监控
    void EnableRead()
    {
        _revents |= EPOLLIN;
        Update();
    }
    // 启动写事件监控
    void EnableWrite()
    {
        _revents |= EPOLLOUT;
        Update();
    }
    // 关闭读事件监控
    void DisableRead()
    {
        _revents &= ~EPOLLIN;
        Update();
    }
    // 关闭写事件监控
    void DisableWrite()
    {
        _revents &= ~EPOLLOUT;
        Update();
    }
    // 关闭所有事件监控
    void DisableAll()
    {
        _revents = 0;
        Update();
    }

    // 移除当前连接
    void Remove(); // 类外实现
    // 更新当前连接状态
    void Update(); // 类外实现

    // 事件处理，一旦连接触发了事件，就调用这个函数，自己触发了什么事件如何处理自己决定
    void HandleEvent()
    {
        if ((_revents & EPOLLIN) || (_revents & EPOLLRDHUP) || (_revents & EPOLLPRI))
        {
            /*不管任何事件，都调用的回调函数*/
            if (_read_callback)
                _read_callback();
        }

        /*有可能会释放连接的操作事件，一次只处理一个*/
        if (_revents & EPOLLOUT)
        {
            if (_write_callback)
                _write_callback();
        }
        else if (_revents & EPOLLERR)
        {
            if (_error_callback)
                _error_callback();
        }
        else if (_revents & EPOLLHUP)
        {
            if (_close_callback)
                _close_callback();
        }

        // 任意事件被触发的回调函数
        if (_event_callback)
            _event_callback();
    }

private:
    int _fd;                       // 套接字描述符
    Poller *_poller;               // 所属的Poller
    uint32_t _events;              // 当前需要监控的事件
    uint32_t _revents;             // 当前连接触发的事件
    EventCallback _read_callback;  // 可读事件被触发的回调函数
    EventCallback _write_callback; // 可写事件被触发的回调函数
    EventCallback _error_callback; // 错误事件被触发的回调函数
    EventCallback _close_callback; // 连接断开事件被触发的回调函数
    EventCallback _event_callback; // 任意事件被触发的回调函数
};

class Poller
{
private:
    bool HasChannel(Channel *channel)
    {
        auto it = _channels.find(channel->Fd());
        if (it == _channels.end())
            return false;
        return true;
    }

    void Update(Channel *channel, int op)
    {
        int fd = channel->Fd();
        struct epoll_event ev;
        ev.data.fd = fd;
        ev.events = channel->Events();

        int ret = epoll_ctl(_epfd, op, fd, &ev);
        if (ret < 0)
        {
            LOG(LogLevel::WARNING) << "Update epoll failed";
            return;
        }
    }

public:
    Poller()
    {
        _epfd = epoll_create(MAX_EPOLLEVENTS);
        if (_epfd < 0)
        {
            LOG(LogLevel::FATAL) << "Create epoll failed";
            abort(); // 退出程序
        }
    }
    ~Poller() {}

    // 添加或修改监控事件
    void UpdateEvent(Channel *channel)
    {
        bool ret = HasChannel(channel);
        if (!ret)
        {
            // 不存在就添加
            _channels.insert(std::make_pair(channel->Fd(), channel));
            return Update(channel, EPOLL_CTL_ADD);
        }
        return Update(channel, EPOLL_CTL_MOD);
    }
    // 移除监控
    void RemoveEvent(Channel *channel)
    {
        auto it = _channels.find(channel->Fd());
        if (it == _channels.end())
            _channels.erase(channel->Fd());
        Update(channel, EPOLL_CTL_DEL);
    }
    // 开始监控，返回活跃连接
    void Poll(std::vector<Channel *> *active)
    {
        int nfd = epoll_wait(_epfd, _events, MAX_EPOLLEVENTS, -1);
        if (nfd < 0)
        {
            if (errno == EINTR) // 被信号打断了
                return;
            LOG(LogLevel::FATAL) << "Poll epoll failed";
            abort(); // 退出程序
        }
        for (int i = 0; i < nfd; i++)
        {
            auto it = _channels.find(_events[i].data.fd);
            if (it == _channels.end())
            {
                LOG(LogLevel::FATAL) << "Poll epoll failed";
                abort(); // 退出程序
            }
            it->second->SetREvents(_events[i].events); //设置实际就绪的事件
            active->push_back(it->second);
        }
    }

private:
    int _epfd;                                    // epoll句柄
    struct epoll_event _events[MAX_EPOLLEVENTS];  // 事件数组
    std::unordered_map<int, Channel *> _channels; // 连接描述符到Channel的映射
};

void Channel::Remove()
{
    _poller->RemoveEvent(this);
}

void Channel::Update()
{
    _poller->UpdateEvent(this);
}
#endif // __SERVER_HPP__