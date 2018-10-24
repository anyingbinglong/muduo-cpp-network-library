// Copyright 2010, Shuo Chen.  All rights reserved.
// http://code.google.com/p/muduo/
//
// Use of this source code is governed by a BSD-style license
// that can be found in the License file.

// Author: Shuo Chen (chenshuo at chenshuo dot com)

#include <muduo/net/poller/EPollPoller.h>

#include <muduo/base/Logging.h>
#include <muduo/net/Channel.h>

#include <boost/static_assert.hpp>

#include <assert.h>
#include <errno.h>
#include <poll.h>
#include <sys/epoll.h>
#include <unistd.h>

using namespace muduo;
using namespace muduo::net;

// On Linux, the constants of poll(2) and epoll(4)
// are expected to be the same.
BOOST_STATIC_ASSERT(EPOLLIN == POLLIN);
BOOST_STATIC_ASSERT(EPOLLPRI == POLLPRI);
BOOST_STATIC_ASSERT(EPOLLOUT == POLLOUT);
BOOST_STATIC_ASSERT(EPOLLRDHUP == POLLRDHUP);
BOOST_STATIC_ASSERT(EPOLLERR == POLLERR);
BOOST_STATIC_ASSERT(EPOLLHUP == POLLHUP);

namespace
{
    /// class Channel类的作用：
    /// （1）channel：通道，大管子，文件描述符（文件描述符：就是插在文件上的大管子）
    /// （2）记录：实际发生的事件的表的一个表项的类型（内容）
    /// （3）用一个class Channel类，来管理一个文件描述符
  
    /// 下面3个变量的作用：
    /// 表项内容channel：
    ///   1.在epoll的内核事件监听表epollfd_中，已经存在
    ///   2.在epoll的内核事件监听表epollfd_中，不存在
    /// （1）标识，表项内容channel，在epoll的内核事件监听表epollfd_中，是否存在
    /// （2）标识，表项内容channel，在epoll的内核事件监听表epollfd_中，的状态

    /// kNew--新建状态：表项内容channel，在epoll的内核事件监听表epollfd_中不存在
    const int kNew = -1;
    /// kAdded--已添加状态：表项内容channel，在epoll的内核事件监听表epollfd_中存在，也就是已经被加入到epoll的内核事件监听表epollfd_中
    const int kAdded = 1;
    /// kDeleted--已删除状态：表项内容channel，需要从epoll的内核事件监听表epollfd_中，删除掉，
    ///                       意味着，该表项内容，在epoll的内核事件监听表epollfd_中，已经存在了，但是需要删除它
    const int kDeleted = 2;
}

EPollPoller::EPollPoller(EventLoop *loop)
    : Poller(loop),
      /// 创建epoll内核事件监听表epollfd_
      epollfd_(::epoll_create1(EPOLL_CLOEXEC)),
      events_(kInitEventListSize)
{
    if (epollfd_ < 0)
    {
        LOG_SYSFATAL << "EPollPoller::EPollPoller";
    }
}

EPollPoller::~EPollPoller()
{
    ::close(epollfd_);
}

/// 函数参数含义：
/// int timeoutMs：epoll函数的超时时间
/// ChannelList* activeChannels：记录实际发生的事件的表
/// 函数功能：
/// (1) numEvents：存放，有事件发生的文件描述符的总数
///   监听epoll的内核事件监听表epollfd_中，存放的文件描述符上，是否有相应的事件发生
/// (2)遍历epoll的内核事件监听表epollfd_，从中找出有事件发生的fd，并将该fd所对应的表项的内容，
///   填入到，记录实际发生的事件的表activeChannels和记录实际发生的事件的表events_中保存
/// (3)本质上，EPollPoller::poll这个函数，就是epoll_wait函数所做的事
Timestamp EPollPoller::poll(int timeoutMs, ChannelList *activeChannels)
{
    LOG_TRACE << "fd total count " << channels_.size();
    /// numEvents：存放，有事件发生的文件描述符的总数
    /// 监听epoll的内核事件监听表epollfd_中，存放的文件描述符上，是否有相应的事件发生
    /// 将有事件发生的文件描述符，所在的表项内容，填入到，记录实际发生的事件的表events_中保存
    int numEvents = ::epoll_wait(epollfd_,
                                 &*events_.begin(),
                                 static_cast<int>(events_.size()),
                                 timeoutMs);
    int savedErrno = errno;
    Timestamp now(Timestamp::now());
    if (numEvents > 0)
    {
        LOG_TRACE << numEvents << " events happened";
        /// 函数参数含义：
        /// int numEvents：存放，有事件发生的文件描述符的总数
        /// ChannelList* activeChannels：记录实际发生的事件的表
        /// 函数功能：
        /// 遍历events_表（用于记录实际发生的事件的表），从中找出有事件发生的fd，并将该fd所对应的表项的内容，
        /// 填入到，记录实际发生的事件的表activeChannels中保存
        fillActiveChannels(numEvents, activeChannels);
        if (implicit_cast<size_t>(numEvents) == events_.size())
        {
            events_.resize(events_.size() * 2);
        }
    }
    else if (numEvents == 0)
    {
        LOG_TRACE << "nothing happened";
    }
    else
    {
        // error happens, log uncommon ones
        if (savedErrno != EINTR)
        {
            errno = savedErrno;
            LOG_SYSERR << "EPollPoller::poll()";
        }
    }
    return now;
}

/// 函数参数含义：
/// int numEvents：存放，有事件发生的文件描述符的总数
/// ChannelList* activeChannels：记录实际发生的事件的表
/// 函数功能：
/// 遍历events_表（用于记录实际发生的事件的表），从中找出有事件发生的fd，并将该fd所对应的表项的内容，
/// 填入到，记录实际发生的事件的表activeChannels中保存
void EPollPoller::fillActiveChannels(int numEvents,
                                     ChannelList *activeChannels) const
{
    assert(implicit_cast<size_t>(numEvents) <= events_.size());
    /// 遍历events_表（用于记录实际发生的事件的表），从中找出有事件发生的fd，并将该fd所对应的表项的内容，
    /// 填入到，记录实际发生的事件的表activeChannels中保存
    for (int i = 0; i < numEvents; ++i)
    {
        /// events_[i].data.ptr所指向的内存中，保存了有事件发生的fd
        /// static_cast<Channel *>(events_[i].data.ptr)此处，使用这种方法，
        /// 直接将events_[i].data.ptr所指向的内存，作为channel文件描述符管理对象的内存
        Channel *channel = static_cast<Channel *>(events_[i].data.ptr);
        /// #ifndef NDEBUG这里的代码，是用来调试使用的
#ifndef NDEBUG
        /// 找出有事件发生的fd
        int fd = channel->fd();
        /// ChannelMap：从fd（文件描述符）到Channel*（class Channel文件描述符fd管理类）的映射表
        /// 网络库中，所有的文件描述符fd，其上注册的事件events，以及其上实际发生的事件revents，都保存在了，ChannelMap表channels_中
        ChannelMap::const_iterator it = channels_.find(fd);
        assert(it != channels_.end());
        assert(it->second == channel);
#endif
        /// 将实际发生的事件，放到文件描述符管理对象channel中保存
        channel->set_revents(events_[i].events);
        /// 将表项的内容channel，
        /// 填入到，记录实际发生的事件的表activeChannels中保存
        activeChannels->push_back(channel);
    }
}

/// 函数参数含义：
/// Channel* channel：
///   （1）文件描述符fd管理类
///   （2）将要存放到，epoll的内核事件监听表epollfd_中的，一个表项内容
///    (3) 将要修改的，epoll的内核事件监听表epollfd_中的，一个表项内容
/// 函数功能：
/// 这个操作，相当于epoll_ctl函数参数选项中的EPOLL_CTL_ADD和EPOLL_CTL_MOD
/// （1）在epoll的内核事件监听表epollfd_中，新增一个表项
/// （2）修改epoll的内核事件监听表epollfd_中的，某个表项
void EPollPoller::updateChannel(Channel *channel)
{
    /// 确保：执行EPollPoller::updateChannel函数的线程，是IO线程
    /// IO线程：创建了EventLoop对象的线程，就是IO线程
    Poller::assertInLoopThread();
    /// 获取，表项内容channel，在epoll的内核事件监听表epollfd_中，的状态
    const int index = channel->index();
    LOG_TRACE << "fd = " << channel->fd()
              << " events = " << channel->events() << " index = " << index;
    /// kNew：将要存放到，epoll的内核事件监听表epollfd_中的，一个表项内容，在epoll的内核事件监听表epollfd_中不存在
    /// kDeleted：将要存放到，epoll的内核事件监听表epollfd_中的，一个表项内容，需要从epoll的内核事件监听表epollfd_中，删除掉，
    ///           意味着，该表项内容，在epoll的内核事件监听表epollfd_中，已经存在了，但是需要删除它
    /// kAdded：将要存放到，epoll的内核事件监听表epollfd_中的，一个表项内容，已经被加入到epoll的内核事件监听表epollfd_中
    if (index == kNew || index == kDeleted)
    {
        // a new one, add with EPOLL_CTL_ADD
        int fd = channel->fd();
        /// (1)kNew：将要存放到，epoll的内核事件监听表epollfd_中的，一个表项内容，在epoll的内核事件监听表epollfd_中不存在
        /// (2)channel管理的文件描述符，为新增的文件描述符
        if (index == kNew)
        {
            /// (1)channel管理的文件描述符，为新增的文件描述符
            /// (2)网络库中，所有的文件描述符fd，其上注册的事件events，以及其上实际发生的事件revents，都保存在了，ChannelMap表channels_中
            assert(channels_.find(fd) == channels_.end());
            /// (1)网络库中，所有的文件描述符fd，其上注册的事件events，以及其上实际发生的事件revents，都保存在了，ChannelMap表channels_中
            /// (2)将新增的文件描述符管理类对象channel，添加到ChannelMap表channels_中
            channels_[fd] = channel;
        }
        else // index == kDeleted ：将要存放到，epoll的内核事件监听表epollfd_中的，一个表项内容，在epoll的内核事件监听表epollfd_中，已经存在
        {
            assert(channels_.find(fd) != channels_.end());
            assert(channels_[fd] == channel);
        }
        /// 从kNew状态，改为，kAdded状态：标识，新增表项channel，已经被加入到epoll的内核事件监听表epollfd_中
        /// kNew：将要存放到，epoll的内核事件监听表epollfd_中的，一个表项内容，在epoll的内核事件监听表epollfd_中不存在
        /// kAdded：将要存放到，epoll的内核事件监听表epollfd_中的，一个表项内容，已经被加入到epoll的内核事件监听表epollfd_中
        channel->set_index(kAdded);
        /// 实施，在epoll的内核事件监听表epollfd_中，新增一个表项
        update(EPOLL_CTL_ADD, channel);
    }
    else /// 修改epoll的内核事件监听表epollfd_中的，某个表项
    {
        /// 修改epoll的内核事件监听表epollfd_中的，表项channel
        // update existing one with EPOLL_CTL_MOD/DEL
        int fd = channel->fd();
        (void)fd;
        assert(channels_.find(fd) != channels_.end());
        assert(channels_[fd] == channel);
        /// kAdded--已添加状态：表项内容channel，在epoll的内核事件监听表epollfd_中存在，也就是已经被加入到epoll的内核事件监听表epollfd_中
        /// kAdded：将要修改的，epoll的内核事件监听表epollfd_中的，表项内容channel，已经被加入到epoll的内核事件监听表epollfd_中
        assert(index == kAdded);
        /// 不再关注channel，所管理的文件描述符上的任何事件
        if (channel->isNoneEvent())
        {
            /// 实施，删除epoll的内核事件监听表epollfd_中的，表项channel
            update(EPOLL_CTL_DEL, channel);
            /// kDeleted--已删除状态：表项内容channel，需要从epoll的内核事件监听表epollfd_中，删除掉，
            ///                       意味着，该表项内容，在epoll的内核事件监听表epollfd_中，已经存在了，但是需要删除它
            /// 设置表项内容channel_的状态，为kNew--新建状态
            channel->set_index(kDeleted);
        }
        else
        {
            /// 实施，修改epoll的内核事件监听表epollfd_中的，表项channel
            update(EPOLL_CTL_MOD, channel);
        }
    }
}

/// 函数参数含义：
/// Channel* channel：
///   （1）文件描述符fd管理类
///   （2）将要删除的，epoll的内核事件监听表epollfd_中的，一个表项内容
/// 函数功能：
/// 这个操作，相当于epoll_ctl函数参数选项中的EPOLL_CTL_DEL
/// 删除epoll的内核事件监听表epollfd_中的，某个表项
void EPollPoller::removeChannel(Channel *channel)
{
    /// 确保：执行EPollPoller::removeChannel函数的线程，是IO线程
    /// IO线程：创建了EventLoop对象的线程，就是IO线程
    Poller::assertInLoopThread();
    int fd = channel->fd();
    LOG_TRACE << "fd = " << fd;
    assert(channels_.find(fd) != channels_.end());
    assert(channels_[fd] == channel);
    /// 不再关注channel，所管理的文件描述符上的任何事件
    assert(channel->isNoneEvent());
    /// 获取，表项内容channel，在epoll的内核事件监听表epollfd_中，的状态
    int index = channel->index();
    /// kAdded：将要，从epoll的内核事件监听表epollfd_中，删除的表项内容channel，在epoll的内核事件监听表epollfd_中存在
    /// kDeleted：表项内容channel，需要从epoll的内核事件监听表epollfd_中，删除掉，
    ///           意味着，该表项内容，在epoll的内核事件监听表epollfd_中，已经存在了，但是需要删除它
    assert(index == kAdded || index == kDeleted);
    /// (1)网络库中，所有的文件描述符fd，其上注册的事件events，以及其上实际发生的事件revents，都保存在了，ChannelMap表channels_中
    /// (2)将文件描述符管理类对象channel，从ChannelMap表channels_中删除掉
    size_t n = channels_.erase(fd);
    (void)n;
    assert(n == 1);
    /// kAdded：将要，从epoll的内核事件监听表epollfd_中，删除的表项内容channel，在epoll的内核事件监听表epollfd_中存在
    if (index == kAdded)
    {
        /// 实施，删除epoll的内核事件监听表epollfd_中的，表项channel
        update(EPOLL_CTL_DEL, channel);
    }
    /// kNew--新建状态：表项内容channel，在epoll的内核事件监听表epollfd_中不存在
    /// 设置表项内容channel_的状态，为kNew--新建状态
    channel->set_index(kNew);
}

/// 函数参数含义：
/// operation：传入epoll_ctl函数的参数 EPOLL_CTL_DEL，EPOLL_CTL_MOD，EPOLL_CTL_ADD
/// 函数功能：
/// 在epoll的内核事件监听表epollfd_中，对表项内容channel，实施operation操作
void EPollPoller::update(int operation, Channel *channel)
{
    struct epoll_event event;
    bzero(&event, sizeof event);
    /// 获得：关心的IO事件（用户注册的事件）events_
    event.events = channel->events();
    event.data.ptr = channel;
    /// 获得：class Channel类，所管理的文件描述符fd_
    int fd = channel->fd();
    LOG_TRACE << "epoll_ctl op = " << operationToString(operation)
              << " fd = " << fd << " event = { " << channel->eventsToString() << " }";
    if (::epoll_ctl(epollfd_, operation, fd, &event) < 0)
    {
        if (operation == EPOLL_CTL_DEL)
        {
            LOG_SYSERR << "epoll_ctl op =" << operationToString(operation) << " fd =" << fd;
        }
        else
        {
            LOG_SYSFATAL << "epoll_ctl op =" << operationToString(operation) << " fd =" << fd;
        }
    }
}

const char *EPollPoller::operationToString(int op)
{
    switch (op)
    {
    case EPOLL_CTL_ADD:
        return "ADD";
    case EPOLL_CTL_DEL:
        return "DEL";
    case EPOLL_CTL_MOD:
        return "MOD";
    default:
        assert(false && "ERROR op");
        return "Unknown Operation";
    }
}
