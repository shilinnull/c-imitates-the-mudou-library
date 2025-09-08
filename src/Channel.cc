#include "Channel.h"
#include "Logger.h"

#include<sys/epoll.h>

const int Channel::KNoneEvent = 0;
const int Channel::KReadEvent = EPOLLIN | EPOLLPRI;
const int Channel::KWriteEvent = EPOLLOUT;


Channel::Channel(EventLoop *loop, int fd)
    :loop_(loop), fd_(fd), events_(0), revents_(0), index_(-1), tied_(false)
{}

Channel::~Channel() 
{}

// 一个TcpConnection新连接创建的时候 TcpConnection => Channel 
void Channel::tie(const std::shared_ptr<void> &obj)
{
    tie_ = obj;
    tied_ = true;
}

void Channel::update() 
{
    // loop_->updateChannel(this);
}

void Channel::remove()
{
    // loop_->removeChannel(this);
}

void Channel::handleEvent(Timestamp receiveTime)
{
    if(tied_) 
    {
        std::shared_ptr<void> guard = tie_.lock();
        if(guard) handleEventWithGuard(receiveTime);
    }
    else 
    {
        handleEventWithGuard(receiveTime);   
    }
}

// 根据poller通知的channel发生的具体事件， 由channel负责调用具体的回调操作
void Channel::handleEventWithGuard(Timestamp receiveTime)
{
    LOG_INFO("channel handleEvent revents:%d\n", revents_);
    if((revents_ & EPOLLHUP) && !(revents_ & EPOLLIN)) // 对端关闭
    {
        if(closeCallback_) closeCallback_();
        return;
    }

    if(revents_ & EPOLLERR) // 出错
    {
        if(errorCallback_) errorCallback_();
    }
    if(revents_ & (EPOLLIN | EPOLLPRI)) // 可读事件
    {
        if(readCallback_) readCallback_(receiveTime);
    }
    if(revents_ & EPOLLOUT) // 可写事件
    {
        if(wrirteCallback_) wrirteCallback_();
    }
}