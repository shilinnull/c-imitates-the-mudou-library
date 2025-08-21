#ifndef __SERVER_HPP__
#define __SERVER_HPP__

#include <string>
#include <cstring>
#include <vector>
#include <unordered_map>
#include <functional>
#include <thread>
#include <mutex>
#include <cassert>
#include <cstdint>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <sys/timerfd.h>

#include "Logger.hpp"

#define BUFFER_DEFAULT_SIZE 1024 // 默认缓冲区大小
#define MAX_EPOLLEVENTS 1024     // 最大epoll事件数
#define MAX_LISTEN 1024          // 最大监听队列长度

typedef enum
{
    DISCONNECTED, // 连接关闭状态
    CONNECTING,   //  连接建立成功-待处理状态
    CONNECTED,    // 连接建立完成，各种设置已完成，可以通信的状态
    DISCONNECTING // 待关闭状态
} ConnStatus;

// 缓冲区模块
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

// 对套接字操作封装的一个模块
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
        _sockfd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (_sockfd < 0)
        {
            LOG(LogLevel::FATAL) << "Create socket failed";
            return false;
        }
        LOG(LogLevel::INFO) << "Create socket success, _sockfd: " << _sockfd;
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
        int ret = connect(_sockfd, (struct sockaddr *)&addr, len);
        if (ret < 0)
        {
            LOG(LogLevel::FATAL) << "Connect socket failed";
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
        if (n <= 0)
        {
            // EAGAIN 当前socket的接收缓冲区中没有数据了，在非阻塞的情况下才会有这个错误
            // EINTR  表示当前socket的阻塞等待，被信号打断了
            if (errno == EAGAIN || errno == EINTR)
                return 0; // 表示这次接收没有接收到数据
            LOG(LogLevel::ERROR) << "Recv socket failed";
            return -1;
        }
        return n; // 返回实际接收的字节数
    }
    ssize_t NonBlockRecv(void *buf, size_t len)
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
    ssize_t NonBlockSend(const void *buf, size_t len)
    {
        if (len == 0)
            return 0;                        // 发送长度为0，直接返回
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
        if (block_flag)
            SetNonBlocking();
        if (!Bind(ip, port))
            return false;
        if (!Listen())
            return false;
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
        setsockopt(_sockfd, SOL_SOCKET, SO_REUSEADDR, (void *)&on, sizeof(on)); // 开启地址重用
        on = 1;
        setsockopt(_sockfd, SOL_SOCKET, SO_REUSEPORT, (void *)&on, sizeof(on)); // 开启端口重用
    }
    // 设置套接字非阻塞属性
    void SetNonBlocking()
    {
        int flag = fcntl(_sockfd, F_GETFL, 0);
        fcntl(_sockfd, F_SETFL, flag | O_NONBLOCK);
        LOG(LogLevel::DEBUG) << "Set socket nonblocking";
    }

private:
    int _sockfd; // 套接字描述符
};

class Poller;
class EventLoop;
// 对一个描述符需要进行的IO事件管理的模块
class Channel
{
    using EventCallback = std::function<void()>;

public:
    Channel(EventLoop *loop, int fd) : _fd(fd), _loop(loop), _events(0), _revents(0) {}
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
    }
    // 当前是否监控了可写
    bool Writable()
    {
        return (_events & EPOLLOUT);
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
    EventLoop *_loop;              // 所属的事件循环
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
            LOG(LogLevel::WARNING) << "Update epoll failed";
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
        LOG(LogLevel::INFO) << "Create epoll success, _epfd: " << _epfd;
    }
    ~Poller() {}

    // 添加或修改监控事件
    void UpdateEvent(Channel *channel)
    {
        bool ret = HasChannel(channel);
        if (!ret)
        {
            // 不存在就添加
            LOG(LogLevel::DEBUG) << "Add channel to epoll: " << channel->Fd();
            _channels.insert(std::make_pair(channel->Fd(), channel));
            return Update(channel, EPOLL_CTL_ADD);
        }
        LOG(LogLevel::DEBUG) << "Update channel in epoll: " << channel->Fd();
        return Update(channel, EPOLL_CTL_MOD);
    }
    // 移除监控
    void RemoveEvent(Channel *channel)
    {
        auto it = _channels.find(channel->Fd());
        if (it != _channels.end())
        {
            LOG(LogLevel::DEBUG) << "Remove channel from epoll: " << channel->Fd();
            _channels.erase(it);
        }
        Update(channel, EPOLL_CTL_DEL);
    }
    // 开始监控，返回活跃连接
    void Poll(std::vector<Channel *> *active)
    {
        int nfds = epoll_wait(_epfd, _events, MAX_EPOLLEVENTS, -1);
        if (nfds < 0)
        {
            if (errno == EINTR) // 被信号打断了
                return;

            LOG(LogLevel::DEBUG) << "epoll wait error: " << strerror(errno);
            abort(); // 退出程序
        }
        for (int i = 0; i < nfds; i++)
        {
            auto it = _channels.find(_events[i].data.fd);
            if (it == _channels.end())
            {
                LOG(LogLevel::FATAL) << "Poll epoll failed";
                abort(); // 退出程序
            }
            it->second->SetREvents(_events[i].events); // 设置实际就绪的事件
            active->push_back(it->second);
        }
    }

private:
    int _epfd;                                    // epoll句柄
    struct epoll_event _events[MAX_EPOLLEVENTS];  // 事件数组
    std::unordered_map<int, Channel *> _channels; // 连接描述符到Channel的映射
};

using TaskFunc = std::function<void()>;
using ReleaseFunc = std::function<void()>;
class TimerTask
{
public:
    TimerTask(uint64_t id, uint32_t delay, const TaskFunc &cb)
        : _id(id), _time_out(delay), _canceled(false), _task_cb(cb)
    {
    }

    ~TimerTask()
    {
        if (_canceled == false)
            _task_cb();
        _release();
    }

    void SetRelease(const ReleaseFunc &cb) { _release = cb; }
    uint32_t DelayTime() { return _time_out; }
    void Cancel() { _canceled = true; }

private:
    uint64_t _id;         // 任务id
    uint32_t _time_out;   // 超时时间
    bool _canceled;       // false-表示没有被取消， true-表示被取消
    TaskFunc _task_cb;    // 执行的定时任务
    ReleaseFunc _release; // 删除TimerWheel中保存的定时器对象信息
};
class TimerWheel
{
    using PtrTask = std::shared_ptr<TimerTask>;
    using WeakTask = std::weak_ptr<TimerTask>;

private:
    void RemoveTimer(uint64_t id)
    {
        auto it = _timers.find(id);
        if (it != _timers.end())
            _timers.erase(it);
    }

    static int CreateTimerfd()
    {
        int timerfd = timerfd_create(CLOCK_MONOTONIC, 0);
        if (timerfd < 0)
        {
            LOG(LogLevel::FATAL) << "timerfd_create failed!";
            abort();
        }
        LOG(LogLevel::INFO) << "timerfd_create success, timefd: " << timerfd;
        struct itimerspec itime;
        itime.it_value.tv_sec = 1;
        itime.it_value.tv_nsec = 0; // 第一次超时时间为1s后
        itime.it_interval.tv_sec = 1;
        itime.it_interval.tv_nsec = 0; // 第一次超时后，每次超时的间隔时
        timerfd_settime(timerfd, 0, &itime, NULL);
        return timerfd;
    }
    int ReadTimerfd()
    {
        uint64_t times = 0;
        int ret = read(_timerfd, &times, 8);
        if (ret < 0)
        {
            LOG(LogLevel::FATAL) << "read timerfd fail!";
            abort();
        }
        return times;
    }
    // 这个函数应该每秒钟被执行一次，相当于秒针向后走了一步
    void RunTimerTask()
    {
        _tick = (_tick + 1) % _capacity;
        _wheel[_tick].clear(); // 清空指定位置的数组，就会把数组中保存的所有管理定时器对象的shared_ptr释放掉
    }

    void OnTime()
    {
        int times = ReadTimerfd();
        for (int i = 0; i < times; i++)
        {
            RunTimerTask();
        }
    }

    void TimerAddInLoop(uint64_t id, uint32_t delay, const TaskFunc &cb)
    {
        PtrTask pt(new TimerTask(id, delay, cb));
        pt->SetRelease(std::bind(&TimerWheel::RemoveTimer, this, id));
        int pos = (_tick + delay) % _capacity;
        _wheel[pos].push_back(pt);
        _timers[id] = WeakTask(pt);
    }
    void TimerRefreshInLoop(uint64_t id)
    {
        // 通过保存的定时器对象的weak_ptr构造一个shared_ptr出来，添加到轮子中
        auto it = _timers.find(id);
        if (it == _timers.end())
            return; // 没找着定时任务，没法刷新，没法延迟

        PtrTask pt = it->second.lock(); // lock获取weak_ptr管理的对象对应的shared_ptr
        int delay = pt->DelayTime();
        int pos = (_tick + delay) % _capacity;
        _wheel[pos].push_back(pt);
    }
    void TimerCancelInLoop(uint64_t id)
    {
        auto it = _timers.find(id);
        if (it == _timers.end())
            return;
        PtrTask pt = it->second.lock();
        if (pt)
            pt->Cancel();
    }

public:
    TimerWheel(EventLoop *loop)
        : _tick(0), _capacity(60), _wheel(_capacity),
          _loop(loop), _timerfd(CreateTimerfd()), _timer_channel(new Channel(loop, _timerfd))
    {
        _timer_channel->SetReadCallback(std::bind(&TimerWheel::OnTime, this));
        _timer_channel->EnableRead(); // 启动读事件监控
    }

    /*定时器中有个_timers成员，定时器信息的操作有可能在多线程中进行，因此需要考虑线程安全问题*/
    /*如果不想加锁，那就把对定期的所有操作，都放到一个线程中进行*/
    // 添加定时任务
    void TimerAdd(uint64_t id, uint32_t delay, const TaskFunc &cb);
    // 刷新/延迟定时任务
    void TimerRefresh(uint64_t id);
    // 取消定时任务
    void TimerCancel(uint64_t id);

    bool HasTimer(uint64_t id)
    {
        auto it = _timers.find(id);
        if (it == _timers.end())
            return false;
        return true;
    }

private:
    int _tick;     // 秒针，走到哪里释放哪里，相当于执行哪里的任务
    int _capacity; // 最大延迟时间
    std::vector<std::vector<PtrTask>> _wheel;
    std::unordered_map<uint64_t, WeakTask> _timers;

    EventLoop *_loop;                        // 定时器事件循环对象
    int _timerfd;                            // 定时器描述符--可读事件回调就是读取计数器，执行定时任务
    std::unique_ptr<Channel> _timer_channel; // 定时器事件监控对象
};

class EventLoop
{
    using Functor = std::function<void()>;

private:
    int CreateEventFd()
    {
        int efd = eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
        if (efd < 0)
        {
            LOG(LogLevel::FATAL) << "create eventfd fail!";
            abort();
        }
        LOG(LogLevel::INFO) << "create eventfd success, eventFd: " << efd;
        return efd;
    }
    void ReadEventfd()
    {
        uint64_t res = 0;
        int ret = read(_event_fd, &res, sizeof(res));
        if (ret < 0)
        {
            if (errno == EAGAIN || errno == EINTR)
                return;
            LOG(LogLevel::FATAL) << "read eventfd fail!";
            abort();
        }
    }

    void RunAllTasks()
    {
        std::vector<Functor> functor;
        {
            std::unique_lock<std::mutex> lock(_mutex);
            _tasks.swap(functor);
        }
        for (auto &f : functor)
            f();
    }

    void WakeUpEventfd()
    {
        uint64_t val = 1;
        int ret = write(_event_fd, &val, sizeof val);
        if (ret < 0)
        {
            if (errno == EINTR)
                return;
            LOG(LogLevel::FATAL) << "write eventfd fail!";
            abort();
        }
    }

public:
    EventLoop()
        : _thread_id(std::this_thread::get_id()),
          _event_fd(CreateEventFd()),
          _event_channel(new Channel(this, _event_fd)),
          _timer_wheel(this)
    {
        // 给eventfd添加可读事件回调函数，读取eventfd事件通知次数
        _event_channel->SetReadCallback(std::bind(&EventLoop::ReadEventfd, this));
        // 启动eventfd的读事件监控
        _event_channel->EnableRead();
    }

    ~EventLoop() {}

    void Start()
    {
        for (;;)
        {
            // 1. 事件监控
            std::vector<Channel *> actives;
            _poller.Poll(&actives);
            // 2. 事件处理
            for (auto &channel : actives)
                channel->HandleEvent(); // 处理事件
            // 3. 执行任务
            RunAllTasks();
        }
    }

    bool isInLoop() const
    {
        return std::this_thread::get_id() == _thread_id;
    }
    void AssertInLoop()
    {
        assert(_thread_id == std::this_thread::get_id());
    }
    // 将操作压入任务池
    void QueueInLoop(const Functor &&cb)
    {
        {
            std::unique_lock<std::mutex> lock(_mutex);
            _tasks.emplace_back(cb);
        }
        // 唤醒有可能因为没有事件就绪，而导致的epoll阻塞；
        // 其实就是给eventfd写入一个数据，eventfd就会触发可读事件
        WakeUpEventfd();
    }

    // 判断将要执行的任务是否处于当前线程中，如果是则执行，不是则压入队列。
    void RunInLoop(const Functor &&cb)
    {
        if (isInLoop())
            return cb();
        return QueueInLoop(std::move(cb));
    }

    // 添加或修改监控事件
    void UpdateEvent(Channel *channel)
    {
        _poller.UpdateEvent(channel);
    }

    // 移除监控
    void RemoveEvent(Channel *channel)
    {
        _poller.RemoveEvent(channel);
    }

    void TimerAdd(uint64_t id, uint32_t delay, const TaskFunc &cb)
    {
        return _timer_wheel.TimerAdd(id, delay, cb);
    }

    void TimerRefresh(uint64_t id)
    {
        return _timer_wheel.TimerRefresh(id);
    }

    void TimerCancel(uint64_t id)
    {
        return _timer_wheel.TimerCancel(id);
    }

    bool HasTimer(uint64_t id)
    {
        return _timer_wheel.HasTimer(id);
    }

private:
    std::thread::id _thread_id; // 线程id
    int _event_fd;              // eventfd唤醒IO事件监控有可能导致的阻塞
    std::unique_ptr<Channel> _event_channel;
    Poller _poller;              // 进行所有描述符的事件监控
    std::vector<Functor> _tasks; // 任务池
    std::mutex _mutex;           // 实现任务池操作的线程安全

    TimerWheel _timer_wheel; // 定时器轮子
};

class Any
{
public:
    Any() : _content(nullptr) {}
    template <class T>
    Any(const T &val) : _content(new placeholder<T>(val)) {}
    Any(const Any &other) : _content(other._content ? other._content->clone() : nullptr) {}
    ~Any() { delete _content; }

    Any &swap(Any &other)
    {
        std::swap(_content, other._content);
        return *this;
    }

    // 返回子类对象保存的数据的指针
    template <class T>
    T *get()
    {
        // 想要获取的数据类型，必须和保存的数据类型一致
        assert(_content->type() == typeid(T));
        return &((placeholder<T> *)_content)->_val;
    }

    // 赋值运算符的重载函数
    template <class T>
    Any &operator=(const T &val)
    {
        // 为val构造一个临时的通用容器，然后与当前容器自身进行指针交换，临时对象释放的时候，原先保存的数据也就被释放了
        Any(val).swap(*this);
        return *this;
    }

    template <class T>
    Any &operator=(const Any &other)
    {
        Any(other).swap(*this);
        return *this;
    }

private:
    class holder
    {
    public:
        virtual ~holder() {}
        virtual const std::type_info &type() const = 0;
        virtual holder *clone() const = 0;
    };
    template <class T>
    class placeholder : public holder
    {
    public:
        placeholder(const T &val)
            : _val(val) {}

        virtual const std::type_info &type() const { return typeid(T); }
        virtual holder *clone() const { return new placeholder(_val); }

    public:
        T _val;
    };
    holder *_content;
};
class Connection;
using PtrConnection = std::shared_ptr<Connection>;
class Connection : public std::enable_shared_from_this<Connection>
{
    using ConnectedCallback = std::function<void(const PtrConnection &)>;
    using MessageCallback = std::function<void(const PtrConnection &, Buffer *)>;
    using ClosedCallback = std::function<void(const PtrConnection &)>;
    using AnyEventCallback = std::function<void(const PtrConnection &)>;

private:
    /* 五个channel的事件回调函数 */
    // 描述符可读事件触发后调用的函数，接收socket数据放到接收缓冲区中，然后调用_message_callback
    void HandleRead()
    {
        // 1. 接收socket的数据，放到缓冲区中
        char buf[65536];
        ssize_t ret = _socket.NonBlockRecv(buf, sizeof(buf) - 1);
        if (ret < 0)
        {
            // 出错了不能直接关闭连接
            return ShutdownInLoop();
        }
        // 这里的等于0表示的是没有读取到数据，而并不是连接断开了，连接断开返回的是-1
        // 将数据放入输入缓冲区,写入之后顺便将写偏移向后移动
        _in_buffer.WriteAndPush(buf, ret);
        // 2. 调用massage_callback进行业务处理
        if (_in_buffer.ReadableSize() > 0)
        {
            // shared_from_this--从当前对象自身获取自身的shared_ptr管理对象
            return _message_callback(shared_from_this(), &_in_buffer);
        }
    }
    // 描述符可写事件触发后调用的函数，将发送缓冲区中的数据进行发送
    void HandleWrite()
    {
        ssize_t ret = _socket.NonBlockSend(_out_buffer.ReadPosition(), _out_buffer.ReadableSize());
        if (ret < 0)
        {
            if (_in_buffer.ReadableSize() > 0)
            {
                _message_callback(shared_from_this(), &_in_buffer);
            }
            return Release(); // 释放连接
        }
        _out_buffer.MoveReadOffset(ret);
        if (_out_buffer.ReadableSize() == 0)
        {
            _channel.DisableRead(); // 没有数据待发送了，关闭写事件监控
            if (_statu == DISCONNECTED)
            {
                return Release();
            }
        }
        return;
    }
    // 描述符触发挂断事件
    void HandleClose()
    {
        /*一旦连接挂断了，套接字就什么都干不了了，因此有数据待处理就处理一下，完毕关闭连接*/
        if (_in_buffer.ReadableSize() > 0)
            _message_callback(shared_from_this(), &_in_buffer);
        return Release();
    }
    // 描述符触发出错事件
    void HandleError()
    {
        HandleClose(); // 一样需要进行处理
    }
    // 描述符触发任意事件: 1. 刷新连接的活跃度--延迟定时销毁任务；  2. 调用组件使用者的任意事件回调
    void HandleEvent()
    {
        if (_enable_inactive_release == true)
        {
            _loop->TimerRefresh(_conn_id);
        }
        if (_event_callback)
        {
            _event_callback(shared_from_this());
        }
    }

    // 连接获取之后，所处的状态下要进行各种设置（启动读监控,调用回调函数）
    void EstablishedInLoop()
    {
        // 1. 修改连接状态  2. 启动读事件监控 3. 调用回调函数
        assert(_statu == CONNECTING); // 当前的状态必须一定是上层的半连接状态
        _statu = CONNECTED;           // 当前函数执行完毕，则连接进入已完成连接状态
        _channel.EnableRead();
        if (_connected_callback)
            _connected_callback(shared_from_this());
    }
    // 这个接口才是实际的释放接口
    void ReleaseInLoop()
    {
        // 1. 修改连接状态
        _statu = DISCONNECTED;
        // 2. 移除连接的事件监控
        _channel.Remove();
        // 3. 关闭描述符
        _socket.Close();
        // 4. 如果当前定时器队列中还有定时销毁任务，则取消任务
        if (_loop->HasTimer(_conn_id))
            CancelInactiveReleaseInLoop();
        // 5. 调用关闭回调函数，避免先移除服务器管理的连接信息导致Connection被释放，
        //  再去处理会出错，因此先调用用户的回调函数
        if (_closed_callback)
            _closed_callback(shared_from_this());
        // 移除服务器的内部管理连接
        if (_server_closed_callback)
            _server_closed_callback(shared_from_this());
    }
    // 这个接口并不是实际的发送接口，而只是把数据放到了发送缓冲区，启动了可写事件监控
    void SendInLoop(Buffer &buf)
    {
        if (_statu == DISCONNECTED) // 查看状态
            return;
        _out_buffer.WriteBufferAndPush(buf); // 把数据放入缓冲区
        if (_channel.Writable() == false)
            _channel.EnableWrite(); // 启动写事件监控
    }
    // 这个关闭操作并非实际的连接释放操作，需要判断还有没有数据待处理，待发送
    void ShutdownInLoop()
    {
        _statu = DISCONNECTED; // 设置半关闭状态
        if (_in_buffer.ReadableSize() > 0)
            if (_message_callback)
                _message_callback(shared_from_this(), &_in_buffer);
        // 要么就是写入数据的时候出错关闭，要么就是没有待发送数据，直接关闭
        if (_out_buffer.ReadableSize() > 0)
            if (_channel.Writable() == false)
                _channel.EnableWrite();

        if (_out_buffer.ReadableSize() == 0)
            Release();
    }
    // 启动非活跃连接超时释放规则
    void EnableInactiveReleaseInLoop(int sec)
    {
        // 1. 设置标志
        _enable_inactive_release = true;
        // 2. 如果定时器存在就刷新延迟
        if (_loop->HasTimer(_conn_id))
            return _loop->TimerRefresh(_conn_id);
        // 3. 如果不存在定时销毁任务则新增
        _loop->TimerAdd(_conn_id, sec, std::bind(&Connection::Release, this));
    }
    // 创建非活跃连接超时
    void CancelInactiveReleaseInLoop()
    {
        _enable_inactive_release = false;
        if (_loop->HasTimer(_conn_id))
            _loop->TimerCancel(_conn_id);
    }

    void UpgradeInLoop(const Any &context, const ConnectedCallback &conn,
                       const MessageCallback &msg, const ClosedCallback &closed,
                       const AnyEventCallback &event)
    {
        _context = context;
        _connected_callback = conn;
        _message_callback = msg;
        _closed_callback = closed;
        _event_callback = event;
    }

public:
    Connection(EventLoop *loop, uint64_t conn_id, int sockfd)
        : _conn_id(conn_id), _sockfd(sockfd), _enable_inactive_release(false),
          _loop(_loop), _statu(CONNECTING), _socket(_sockfd), _channel(_loop, _sockfd)
    {
        _channel.SetReadCallback(std::bind(&Connection::HandleRead, this));
        _channel.SetWriteCallback(std::bind(&Connection::HandleWrite, this));
        _channel.SetCloseCallback(std::bind(&Connection::HandleClose, this));
        _channel.SetEventCallback(std::bind(&Connection::HandleEvent, this));
        _channel.SetErrorCallback(std::bind(&Connection::HandleError, this));
    }
    ~Connection() { LOG(LogLevel::DEBUG) << "Release Connection: " << this; }
    // 获取管理的文件描述符
    int Fd() { return _sockfd; }
    // 获取连接ID
    int Id() { return _conn_id; }
    // 是否处于CONNECTED状态
    bool Connected() { return _statu == CONNECTED; }
    // 设置上下文--连接建立完成时进行调用
    void SetContext(const Any &context) { _context = context; }
    // 获取上下文，返回的是指针
    Any *GetContext() { return &_context; }
    void SetConnectedCallback(const ConnectedCallback &cb) { _connected_callback = cb; }
    void SetMessageCallback(const MessageCallback &cb) { _message_callback = cb; }
    void SetClosedCallback(const ClosedCallback &cb) { _closed_callback = cb; }
    void SetAnyEventCallback(const AnyEventCallback &cb) { _event_callback = cb; }
    void SetSrvClosedCallback(const ClosedCallback &cb) { _server_closed_callback = cb; }
    // 连接建立就绪后，进行channel回调设置，启动读监控，调用_connected_callback
    void Established()
    {
        _loop->RunInLoop(std::bind(&Connection::Established, this));
    }
    // 发送数据，将数据放到发送缓冲区，启动写事件监控
    void Send(const char *data, size_t len)
    {
        /*外界传入的data，可能是个临时的空间，我们现在只是把发送操作压入了任务池，有可能并没有被立即执行
        因此有可能执行的时候，data指向的空间有可能已经被释放了。*/
        Buffer buf;
        buf.WriteAndPush(data, len);
        _loop->RunInLoop(std::bind(&Connection::SendInLoop, this, buf));
    }
    // 提供给组件使用者的关闭接口--并不实际关闭，需要判断有没有数据待处理
    void Shutdown()
    {
        _loop->RunInLoop(std::bind(&Connection::ShutdownInLoop, this));
    }
    void Release()
    {
        _loop->QueueInLoop(std::bind(&Connection::ReleaseInLoop, this));
    }
    // 启动非活跃销毁，并定义多长时间无通信就是非活跃，添加定时任务
    void EnableInactiveRelease(int sec)
    {
        _loop->RunInLoop(std::bind(&Connection::EnableInactiveReleaseInLoop, this, sec));
    }
    // 取消非活跃销毁
    void CancelInactiveRelease()
    {
        _loop->RunInLoop(std::bind(&Connection::CancelInactiveReleaseInLoop, this));
    }

    // 切换协议---重置上下文以及阶段性回调处理函数 -- 而是这个接口必须在EventLoop线程中立即执行
    // 防备新的事件触发后，处理的时候，切换任务还没有被执行--会导致数据使用原协议处理了。
    void Upgrade(const Any &context, const ConnectedCallback &conn, const MessageCallback &msg,
                 const ClosedCallback &closed, const AnyEventCallback &event)
    {
        _loop->AssertInLoop();
        _loop->RunInLoop(std::bind(&Connection::UpgradeInLoop, this, context, conn, msg, closed, event));
    }

private:
    uint64_t _conn_id;             // 连接的唯一ID，便于连接的管理和查找
    int _sockfd;                   // 连接关联的文件描述符
    bool _enable_inactive_release; // 连接是否启动非活跃, 默认false
    EventLoop *_loop;              // 连接锁关联的EventLoop
    ConnStatus _statu;             // 连接状态
    Socket _socket;                // 套接字操作管理
    Channel _channel;              // 连接的事件管理
    Buffer _in_buffer;             // 输入缓冲区---存放从socket中读取到的数据
    Buffer _out_buffer;            // 输出缓冲区---存放要发送给对端的数据
    Any _context;                  // 请求的接收处理上下文

    /*这四个回调函数，是让服务器模块来设置的（其实服务器模块的处理回调也是组件使用者设置的）
    这几个回调都是组件使用者使用的*/
    ConnectedCallback _connected_callback;
    MessageCallback _message_callback;
    ClosedCallback _closed_callback;
    AnyEventCallback _event_callback;

    /*组件内的连接关闭回调--组件内设置的，因为服务器组件内会把所有的连接管理起来，一旦某个连接要关闭
      就应该从管理的地方移除掉自己的信息*/
    ClosedCallback _server_closed_callback;
};

void Channel::Remove()
{
    _loop->RemoveEvent(this);
}

void Channel::Update()
{
    _loop->UpdateEvent(this);
}

// 添加定时任务
void TimerWheel::TimerAdd(uint64_t id, uint32_t delay, const TaskFunc &cb)
{
    _loop->RunInLoop(std::bind(&TimerWheel::TimerAddInLoop, this, id, delay, cb));
}

// 刷新/延迟定时任务
void TimerWheel::TimerRefresh(uint64_t id)
{
    _loop->RunInLoop(std::bind(&TimerWheel::TimerRefreshInLoop, this, id));
}

// 取消定时任务
void TimerWheel::TimerCancel(uint64_t id)
{
    _loop->RunInLoop(std::bind(&TimerWheel::TimerCancelInLoop, this, id));
}

#endif // __SERVER_HPP__