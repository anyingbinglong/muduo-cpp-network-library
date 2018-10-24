// Copyright 2010, Shuo Chen.  All rights reserved.
// http://code.google.com/p/muduo/
//
// Use of this source code is governed by a BSD-style license
// that can be found in the License file.

// Author: Shuo Chen (chenshuo at chenshuo dot com)

#include <muduo/base/Logging.h>
#include <muduo/net/Channel.h>
#include <muduo/net/EventLoop.h>

#include <sstream>

#include <poll.h>

using namespace muduo;
using namespace muduo::net;

/// 不再关注class Channel类，所管理的文件描述符上的任何事件
const int Channel::kNoneEvent = 0;

/// POLLIN：数据（包括普通数据和优先级数据）可读
/// POLLPRI：高优先级数据可读
const int Channel::kReadEvent = POLLIN | POLLPRI;

/// POLLOUT：数据包括普通数据和优先级数据）可写
const int Channel::kWriteEvent = POLLOUT;

Channel::Channel(EventLoop *loop, int fd__)
    : loop_(loop),
      fd_(fd__),
      events_(0),
      revents_(0),
      index_(-1),
      logHup_(true),
      tied_(false),
      eventHandling_(false),
      addedToLoop_(false)
{
}

Channel::~Channel()
{
    assert(!eventHandling_);
    assert(!addedToLoop_);
    if (loop_->isInLoopThread())
    {
        assert(!loop_->hasChannel(this));
    }
}

void Channel::tie(const boost::shared_ptr<void> &obj)
{
    tie_ = obj;
    tied_ = true;
}

/// ====================================================================================================
/// 在，class PollPoller IO复用的封装：封装了poll，中的功能
/// ====================================================================================================
/// 函数参数含义：
/// Channel* channel：
///   （1）文件描述符fd管理类
///   （2）将要存放到，pollfds_表（相当于epoll的内核事件监听表）的，一个表项内容
///    (3) 将要修改的，pollfds_表（相当于epoll的内核事件监听表）的，一个表项内容
/// 函数功能：
/// 这个操作，相当于epoll_ctl函数参数选项中的EPOLL_CTL_ADD和EPOLL_CTL_MOD
/// （1）在pollfds_表（相当于epoll的内核事件监听表）中，新增一个表项
/// （2）修改pollfds_表（相当于epoll的内核事件监听表）中的，某个表项
/// ====================================================================================================
/// 在，class EPollPoller IO复用的封装：封装了epoll中的功能
/// ====================================================================================================
/// 函数参数含义：
/// Channel* channel：
///   （1）文件描述符fd管理类
///   （2）将要存放到，epoll的内核事件监听表epollfd_中的，一个表项内容
///    (3) 将要修改的，epoll的内核事件监听表epollfd_中的，一个表项内容
/// 函数功能：
/// 这个操作，相当于epoll_ctl函数参数选项中的EPOLL_CTL_ADD和EPOLL_CTL_MOD
/// （1）在epoll的内核事件监听表epollfd_中，新增一个表项
/// （2）修改epoll的内核事件监听表epollfd_中的，某个表项
void Channel::update()
{
    addedToLoop_ = true;
    /// ====================================================================================================
    /// 在，class PollPoller IO复用的封装：封装了poll，中的功能
    /// ====================================================================================================
    /// 函数参数含义：
    /// Channel* channel：
    ///   （1）文件描述符fd管理类
    ///   （2）将要存放到，pollfds_表（相当于epoll的内核事件监听表）的，一个表项内容
    ///    (3) 将要修改的，pollfds_表（相当于epoll的内核事件监听表）的，一个表项内容
    /// 函数功能：
    /// 这个操作，相当于epoll_ctl函数参数选项中的EPOLL_CTL_ADD和EPOLL_CTL_MOD
    /// （1）在pollfds_表（相当于epoll的内核事件监听表）中，新增一个表项
    /// （2）修改pollfds_表（相当于epoll的内核事件监听表）中的，某个表项
    /// ====================================================================================================
    /// 在，class EPollPoller IO复用的封装：封装了epoll中的功能
    /// ====================================================================================================
    /// 函数参数含义：
    /// Channel* channel：
    ///   （1）文件描述符fd管理类
    ///   （2）将要存放到，epoll的内核事件监听表epollfd_中的，一个表项内容
    ///    (3) 将要修改的，epoll的内核事件监听表epollfd_中的，一个表项内容
    /// 函数功能：
    /// 这个操作，相当于epoll_ctl函数参数选项中的EPOLL_CTL_ADD和EPOLL_CTL_MOD
    /// （1）在epoll的内核事件监听表epollfd_中，新增一个表项
    /// （2）修改epoll的内核事件监听表epollfd_中的，某个表项
    loop_->updateChannel(this);
}

/// ====================================================================================================
/// 在，class PollPoller IO复用的封装：封装了poll，中的功能
/// ====================================================================================================
/// 函数参数含义：
/// Channel* channel：
///   （1）文件描述符fd管理类
///   （2）将要删除的，pollfds_表（相当于epoll的内核事件监听表）的，一个表项内容
/// 函数功能：
/// 这个操作，相当于epoll_ctl函数参数选项中的EPOLL_CTL_DEL
/// 删除pollfds_表（相当于epoll的内核事件监听表）中的，某个表项
/// ====================================================================================================
/// 在，class EPollPoller IO复用的封装：封装了epoll中的功能
/// ====================================================================================================
/// 函数参数含义：
/// Channel* channel：
///   （1）文件描述符fd管理类
///   （2）将要删除的，epoll的内核事件监听表epollfd_中的，一个表项内容
/// 函数功能：
/// 这个操作，相当于epoll_ctl函数参数选项中的EPOLL_CTL_DEL
/// 删除epoll的内核事件监听表epollfd_中的，某个表项
void Channel::remove()
{
    assert(isNoneEvent());
    addedToLoop_ = false;
    /// ====================================================================================================
    /// 在，class PollPoller IO复用的封装：封装了poll，中的功能
    /// ====================================================================================================
    /// 函数参数含义：
    /// Channel* channel：
    ///   （1）文件描述符fd管理类
    ///   （2）将要删除的，pollfds_表（相当于epoll的内核事件监听表）的，一个表项内容
    /// 函数功能：
    /// 这个操作，相当于epoll_ctl函数参数选项中的EPOLL_CTL_DEL
    /// 删除pollfds_表（相当于epoll的内核事件监听表）中的，某个表项
    /// ====================================================================================================
    /// 在，class EPollPoller IO复用的封装：封装了epoll中的功能
    /// ====================================================================================================
    /// 函数参数含义：
    /// Channel* channel：
    ///   （1）文件描述符fd管理类
    ///   （2）将要删除的，epoll的内核事件监听表epollfd_中的，一个表项内容
    /// 函数功能：
    /// 这个操作，相当于epoll_ctl函数参数选项中的EPOLL_CTL_DEL
    /// 删除epoll的内核事件监听表epollfd_中的，某个表项
    loop_->removeChannel(this);
}

/// revents_：里面存放着已经就绪(实际发生)的事件（由内核填充）
/// 分发：调用某个socket文件描述符上所发生的事件，所对应的事件处理函数，处理发生的事件的这个过程，就是分发
/// 实现事件分发机制：根据class Channel所管理的文件描述符上，实际发生（已经就绪）的事件revents_，调用相应的事件处理函数
void Channel::handleEvent(Timestamp receiveTime)
{
    boost::shared_ptr<void> guard;
    if (tied_)
    {
        guard = tie_.lock();
        if (guard)
        {
            handleEventWithGuard(receiveTime);
        }
    }
    else
    {
        handleEventWithGuard(receiveTime);
    }
}

void Channel::handleEventWithGuard(Timestamp receiveTime)
{
    eventHandling_ = true;
    LOG_TRACE << reventsToString();
    /// revents_：里面存放着已经就绪(实际发生)的事件（由内核填充）
    /// 分发：调用某个socket文件描述符上所发生的事件，所对应的事件处理函数，处理发生的事件的这个过程，就是分发
    /// 实现事件分发机制：根据class Channel所管理的文件描述符上，实际发生（已经就绪）的事件revents_，调用相应的事件处理函数
    if ((revents_ & POLLHUP) && !(revents_ & POLLIN))
    {
        if (logHup_)
        {
            LOG_WARN << "fd = " << fd_ << " Channel::handle_event() POLLHUP";
        }
        if (closeCallback_) closeCallback_();
    }

    if (revents_ & POLLNVAL)
    {
        LOG_WARN << "fd = " << fd_ << " Channel::handle_event() POLLNVAL";
    }

    if (revents_ & (POLLERR | POLLNVAL))
    {
        if (errorCallback_) errorCallback_();
    }
    if (revents_ & (POLLIN | POLLPRI | POLLRDHUP))
    {
        if (readCallback_) readCallback_(receiveTime);
    }
    if (revents_ & POLLOUT)
    {
        if (writeCallback_) writeCallback_();
    }
    eventHandling_ = false;
}

string Channel::reventsToString() const
{
    return eventsToString(fd_, revents_);
}

string Channel::eventsToString() const
{
    return eventsToString(fd_, events_);
}

string Channel::eventsToString(int fd, int ev)
{
    std::ostringstream oss;
    oss << fd << ": ";
    if (ev & POLLIN)
        oss << "IN ";
    if (ev & POLLPRI)
        oss << "PRI ";
    if (ev & POLLOUT)
        oss << "OUT ";
    if (ev & POLLHUP)
        oss << "HUP ";
    if (ev & POLLRDHUP)
        oss << "RDHUP ";
    if (ev & POLLERR)
        oss << "ERR ";
    if (ev & POLLNVAL)
        oss << "NVAL ";

    return oss.str().c_str();
}
