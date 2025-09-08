#pragma once

#include "noncopyable.h"
#include "Timestamp.h"

#include <functional>
#include <memory>

class EventLoop;

class Channel : noncopyable
{
public:
    using EventCallback = std::function<void()>;
    using ReadEventCallback = std::function<void(Timestamp)>;
    Channel(EventLoop *loop, int fd);
    ~Channel();

    void handleEvent(Timestamp receiveTime);

    // 设置回调函数
    void setReadCallback(ReadEventCallback cb) { readCallback_ = cb; }
    void setWriteCallback(EventCallback cb) { wrirteCallback_ = cb; }
    void setCloseCallback(EventCallback cb) { closeCallback_ = cb; }
    void setErrorCallback(EventCallback cb) { errorCallback_ = cb; }

    // 防止当channel被手动remove掉，channel还在执行回调操作
    void tie(const std::shared_ptr<void> &);

    int fd() const { return fd_; }
    int events() const { return events_; }
    void set_revents(int revt) { revents_ = revt; }

    // 设置fd相应的事件状态
    void enableReading()
    {
        events_ |= KReadEvent;
        update();
    }
    void disableReading()
    {
        events_ &= ~KReadEvent;
        update();
    }
    void enableWriting()
    {
        events_ |= KWriteEvent;
        update();
    }
    void disableWriting()
    {
        events_ &= ~KWriteEvent;
        update();
    }
    void disableAll()
    {
        events_ = KNoneEvent;
        update();
    }

    // 返回fd当前的事件状态
    bool isKReadEvent() const { return events_ == KReadEvent; }
    bool isWriting() const { return events_ & KWriteEvent; }
    bool isReading() const { return events_ & KReadEvent; }

    int index() const { return index_; }
    void set_index(int idx) { index_ = idx; }

    EventLoop *owerLoop() { return loop_; }
    void remove();

private:
    void update();
    void handleEventWithGuard(Timestamp receiveTime);

    static const int KReadEvent;
    static const int KWriteEvent;
    static const int KNoneEvent;

    EventLoop *loop_; // 事件循环
    const int fd_;    // fd, Poller监听的对象
    int events_;      // 注册感兴趣的事件
    int revents_;     // poller返回的具体发生的事件
    int index_;

    std::weak_ptr<void> tie_;
    bool tied_;

    // 因为channel通道里面能够获知fd最终发生的具体的事件revents，所以它负责调用具体事件的回调操作
    ReadEventCallback readCallback_;
    EventCallback wrirteCallback_;
    EventCallback closeCallback_;
    EventCallback errorCallback_;
};