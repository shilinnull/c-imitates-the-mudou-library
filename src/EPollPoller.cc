#include "EPollPoller.h"
#include "Logger.h"
#include "Channel.h"

#include <unistd.h>
#include <strings.h>

const int kNew = -1;    // channel未添加到epoll中
const int kAdded = 1;   // channel已经添加到epoll中
const int kDeleted = 2; // channel已经从epoll中删除

EPollPoller::EPollPoller(EventLoop *loop)
    : Poller(loop)
    , epollfd_(::epoll_create1(EPOLL_CLOEXEC))
    , events_(kInitEventListSize)
{
    if (epollfd_ < 0)
    {
        LOG_FATAL("epoll_create error:%d \n", errno);
    }
}

EPollPoller::~EPollPoller()
{
    ::close(epollfd_);
}

Timestamp EPollPoller::poll(int timeoutMs, ChannelList *activeChannels)
{
        // 实际上应该用LOG_DEBUG输出日志更为合理
    LOG_INFO("func=%s => fd total count:%lu \n", __FUNCTION__, channels_.size());

    int numEvents = ::epoll_wait(epollfd_, &*events_.begin(), static_cast<int>(events_.size()), timeoutMs);
    int saveError = errno; // 记录错误码
    Timestamp now(Timestamp::now());
    if (numEvents > 0)
    {
        LOG_INFO("%d events happened \n", numEvents);
        fillActiveChannels(numEvents, activeChannels);
        if (static_cast<size_t>(numEvents) == events_.size())
        {
            events_.resize(events_.size() * 2); // 扩容
        }
    }
    else if (numEvents == 0)
    {
        LOG_INFO("%s timeout! \n", __FUNCTION__);
    }
    else
    {
        if (saveError != EINTR) // 被信号中断
        {
            errno = saveError;
            LOG_ERROR("EPollPoller::poll() error:%d \n", errno);
        }
    }
    return now;
}

void EPollPoller::updateChannel(Channel *channel)
{
    const int index = channel->index();
    LOG_INFO("func=%s => fd=%d events=%d index=%d \n", __FUNCTION__, channel->fd(), channel->events(), index);
    if (index == kNew || index == kDeleted) // 新增的channel或者已经删除的channel
    {
        if (index == kNew)
        {
            int fd = channel->fd();
            channels_[fd] = channel;
        }
        channel->set_index(kAdded);
        update(EPOLL_CTL_ADD, channel);
    }
    else // 已经添加过的channel, 更新或者删除
    {
        int fd = channel->fd();
        if (channel->isKReadEvent())
        {
            update(EPOLL_CTL_DEL, channel);
            channel->set_index(kDeleted);
        }
        else
        {
            update(EPOLL_CTL_MOD, channel);
        }
    }
}
void EPollPoller::removeChannel(Channel *channel)
{
    int fd = channel->fd();
    channels_.erase(fd);
    LOG_INFO("func=%s => fd=%d\n", __FUNCTION__, fd);
    int index = channel->index();
    if (index == kAdded) // 已经添加到epoll中的
    {
        update(EPOLL_CTL_DEL, channel);
    }
    channel->set_index(kNew); // 设置为初始状态
}

// 填写活跃的连接
void EPollPoller::fillActiveChannels(int numEvents, ChannelList *activeChannels) const
{
    for (int i = 0; i < numEvents; ++i)
    {
        Channel *channel = static_cast<Channel *>(events_[i].data.ptr);
        channel->set_revents(events_[i].events);
        activeChannels->push_back(channel);
    }
}
// 更新channel通道
void EPollPoller::update(int operation, Channel *channel)
{
    epoll_event event;
    bzero(&event, sizeof event);
    event.events = channel->events();
    event.data.ptr = channel;
    int fd = channel->fd();
    if (::epoll_ctl(epollfd_, operation, fd, &event) < 0)
    {
        if (operation == EPOLL_CTL_DEL)
            LOG_ERROR("epoll_ctl del error:%d\n", errno);
        else
            LOG_FATAL("epoll_ctl add/mod error:%d\n", errno);
    }
}

int main()
{
    EPollPoller poller(nullptr);
    return 0;
}