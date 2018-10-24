// Copyright 2010, Shuo Chen.  All rights reserved.
// http://code.google.com/p/muduo/
//
// Use of this source code is governed by a BSD-style license
// that can be found in the License file.

// Author: Shuo Chen (chenshuo at chenshuo dot com)

#include <muduo/net/poller/PollPoller.h>

#include <muduo/base/Logging.h>
#include <muduo/base/Types.h>
#include <muduo/net/Channel.h>

#include <assert.h>
#include <errno.h>
#include <poll.h>

using namespace muduo;
using namespace muduo::net;

PollPoller::PollPoller(EventLoop* loop)
  : Poller(loop)
{
}

PollPoller::~PollPoller()
{
}

/// 函数参数含义：
/// int timeoutMs：poll函数的超时时间
/// ChannelList* activeChannels：记录实际发生的事件的表
/// 函数功能：
/// (1) numEvents：存放，有事件发生的文件描述符的总数
/// 	监听pollfds_表（相当于epoll的内核事件监听表）中，存放的文件描述符上，是否有相应的事件发生
/// (2)遍历pollfds_表（相当于epoll的内核事件监听表），从中找出有事件发生的fd，并将该fd所对应的表项的内容，
/// 	填入到，记录实际发生的事件的表activeChannels中保存
/// (3)这2个操作，合到一起，相当于epoll_wait函数的功能
/// 	因而，PollPoller::poll这个函数的功能，本质上，就是epoll_wait函数所做的事，
/// 	所以，PollPoller::poll这个函数，也就相当于epoll_wait函数
Timestamp PollPoller::poll(int timeoutMs, ChannelList* activeChannels)
{
  // XXX pollfds_ shouldn't change
  /// numEvents：存放，有事件发生的文件描述符的总数
  /// 监听pollfds_表（相当于epoll的内核事件监听表）中，存放的文件描述符上，是否有相应的事件发生
  int numEvents = ::poll(&*pollfds_.begin(), pollfds_.size(), timeoutMs);
  int savedErrno = errno;
  Timestamp now(Timestamp::now());
  if (numEvents > 0)
  {
    LOG_TRACE << numEvents << " events happened";
    /// 遍历pollfds_表（相当于epoll的内核事件监听表），从中找出有事件发生的fd，并将该fd所对应的表项的内容，
    /// 填入到，记录实际发生的事件的表activeChannels中保存
    fillActiveChannels(numEvents, activeChannels);
  }
  else if (numEvents == 0)
  {
    LOG_TRACE << " nothing happened";
  }
  else
  {
    if (savedErrno != EINTR)
    {
      errno = savedErrno;
      LOG_SYSERR << "PollPoller::poll()";
    }
  }
  return now;
}

/// 函数参数含义：
/// int numEvents：存放，有事件发生的文件描述符的总数
/// ChannelList* activeChannels：记录实际发生的事件的表
/// 函数功能：
/// 遍历pollfds_表（相当于epoll的内核事件监听表），从中找出有事件发生的fd，并将该fd所对应的表项的内容，
/// 填入到，记录实际发生的事件的表activeChannels中保存
void PollPoller::fillActiveChannels(int numEvents,
                                    ChannelList* activeChannels) const
{
  /// 遍历pollfds_表（相当于epoll的内核事件监听表），从中找出有事件发生的fd，并将该fd所对应的表项的内容，
  /// 填入到，记录实际发生的事件的表activeChannels中保存
  for (PollFdList::const_iterator pfd = pollfds_.begin();
      pfd != pollfds_.end() && numEvents > 0; ++pfd)
  {
    /// struct pollfd
    /// {
    ///   int fd;       // 文件描述符
    ///   short events; // 关心的IO事件（用户注册的事件）
    ///   short revents;// 实际发生的事件（由内核填充）
    /// };
    /// 文件描述符fd上有事件发生
    if (pfd->revents > 0)
    {
      --numEvents;
      /// ChannelMap：从fd（文件描述符）到Channel*（class Channel文件描述符fd管理类）的映射表
      /// 网络库中，所有的文件描述符fd，其上注册的事件events，以及其上实际发生的事件revents，都保存在了，ChannelMap表channels_中
      ChannelMap::const_iterator ch = channels_.find(pfd->fd);
      assert(ch != channels_.end());
      /// 获取文件描述符pfd->fd的class Channel文件描述符管理类
      /// channel：存放，pollfds_表（相当于epoll的内核事件监听表）的，一个表项内容
      Channel* channel = ch->second;
      assert(channel->fd() == pfd->fd);
      /// 将实际发生的事件，放到pfd->fd对应的文件描述符管理类中保存
      channel->set_revents(pfd->revents);
      // pfd->revents = 0;
      /// 将pfd->fd所对应的表项的内容channel，
      /// 填入到，记录实际发生的事件的表activeChannels中保存
      activeChannels->push_back(channel);
    }
  }
}

/// 函数参数含义：
/// Channel* channel：
///   （1）文件描述符fd管理类
///   （2）将要存放到，pollfds_表（相当于epoll的内核事件监听表）的，一个表项内容
///    (3) 将要修改的，pollfds_表（相当于epoll的内核事件监听表）的，一个表项内容
/// 函数功能：
/// 这个操作，相当于epoll_ctl函数参数选项中的EPOLL_CTL_ADD和EPOLL_CTL_MOD
/// （1）在pollfds_表（相当于epoll的内核事件监听表）中，新增一个表项
/// （2）修改pollfds_表（相当于epoll的内核事件监听表）中的，某个表项
void PollPoller::updateChannel(Channel* channel)
{
  /// 确保：执行PollPoller::updateChannel函数的线程，是IO线程
  /// IO线程：创建了EventLoop对象的线程，就是IO线程
  Poller::assertInLoopThread();
  LOG_TRACE << "fd = " << channel->fd() << " events = " << channel->events();
  /// 在pollfds_表（相当于epoll的内核事件监听表）中，新增一个表项
  /// 相当于epoll_ctl函数参数选项中的EPOLL_CTL_ADD
  if (channel->index() < 0)
  {
    // a new one, add to pollfds_
    /// (1)channel管理的文件描述符，为新增的文件描述符
    /// (2)网络库中，所有的文件描述符fd，其上注册的事件events，以及其上实际发生的事件revents，都保存在了，ChannelMap表channels_中
    assert(channels_.find(channel->fd()) == channels_.end());
    /// 新建一个，用于存放pollfds_表（相当于epoll的内核事件监听表）的，一个表项内容的，结构体变量
    struct pollfd pfd;
    /// 获得：class Channel类，所管理的文件描述符fd_
    pfd.fd = channel->fd();
    pfd.events = static_cast<short>(channel->events());
    /// 赋值为0，表示未发生任何事件
    pfd.revents = 0;
    /// 在pollfds_表（相当于epoll的内核事件监听表）中，新增一个表项
    pollfds_.push_back(pfd);
    /// 计算：class Channel类，所管理的文件描述符，在pollfds表（相当于epoll的内核事件监听表）中的位置
    int idx = static_cast<int>(pollfds_.size())-1;
    /// 设置：  
    /// （1）class Channel类，所管理的文件描述符，对应的struct pollfd，
    /// 在struct pollfd结构类型数组PollFdList pollfds中的位置（下标）
    /// （2）class Channel类，所管理的文件描述符，在pollfds表（相当于epoll的内核事件监听表）中的位置
    channel->set_index(idx);
    /// (1)网络库中，所有的文件描述符fd，其上注册的事件events，以及其上实际发生的事件revents，都保存在了，ChannelMap表channels_中
    /// (2)将新增的文件描述符管理类对象channel，添加到ChannelMap表channels_中
    channels_[pfd.fd] = channel;
  }
  else/// 修改pollfds_表（相当于epoll的内核事件监听表）中的，某个表项
  {   /// 相当于epoll_ctl函数参数选项中的EPOLL_CTL_MOD
    // update existing one
    /// (1)channel管理的文件描述符，为已经存在的文件描述符
    /// (2)网络库中，所有的文件描述符fd，其上注册的事件events，以及其上实际发生的事件revents，都保存在了，ChannelMap表channels_中
    assert(channels_.find(channel->fd()) != channels_.end());
    assert(channels_[channel->fd()] == channel);
    /// 获得：class Channel类，所管理的文件描述符，在pollfds表（相当于epoll的内核事件监听表）中的位置
    int idx = channel->index();
    assert(0 <= idx && idx < static_cast<int>(pollfds_.size()));
    /// 获得：class Channel类，所管理的文件描述符，在pollfds表（相当于epoll的内核事件监听表）中idx位置的表项内容
    struct pollfd& pfd = pollfds_[idx];
    assert(pfd.fd == channel->fd() || pfd.fd == -channel->fd()-1);
    /// channel->fd()获得：class Channel类，所管理的文件描述符
    pfd.fd = channel->fd();
    /// 更新（修改）channel所管理的文件描述符上，由用户注册的事件channel->events()
    pfd.events = static_cast<short>(channel->events());
    pfd.revents = 0;
    if (channel->isNoneEvent())
    {
      // ignore this pollfd
      pfd.fd = -channel->fd()-1;
    }
  }
}

/// 函数参数含义：
/// Channel* channel：
///   （1）文件描述符fd管理类
///   （2）将要删除的，pollfds_表（相当于epoll的内核事件监听表）的，一个表项内容
/// 函数功能：
/// 这个操作，相当于epoll_ctl函数参数选项中的EPOLL_CTL_DEL
/// 删除pollfds_表（相当于epoll的内核事件监听表）中的，某个表项
void PollPoller::removeChannel(Channel* channel)
{
  /// 确保：执行PollPoller::updateChannel函数的线程，是IO线程
  /// IO线程：创建了EventLoop对象的线程，就是IO线程
  Poller::assertInLoopThread();
  LOG_TRACE << "fd = " << channel->fd();
  /// (1)channel管理的文件描述符，为要删除的文件描述符
  /// (2)网络库中，所有的文件描述符fd，其上注册的事件events，以及其上实际发生的事件revents，都保存在了，ChannelMap表channels_中
  assert(channels_.find(channel->fd()) != channels_.end());
  assert(channels_[channel->fd()] == channel);
  /// 判断：用户是否没有在fd上，注册任何事件
  assert(channel->isNoneEvent());
  /// 获得：class Channel类，所管理的文件描述符，在pollfds表（相当于epoll的内核事件监听表）中的位置
  int idx = channel->index();
  assert(0 <= idx && idx < static_cast<int>(pollfds_.size()));
  /// 获得：class Channel类，所管理的文件描述符，在pollfds表（相当于epoll的内核事件监听表）中idx位置的表项内容
  const struct pollfd& pfd = pollfds_[idx]; (void)pfd;
  assert(pfd.fd == -channel->fd()-1 && pfd.events == channel->events());

  /// 下面的代码：开始实施，删除pollfds_表（相当于epoll的内核事件监听表）中的，channel表项
  /// (1)channel管理的文件描述符，为要删除的文件描述符
  /// (2)网络库中，所有的文件描述符fd，其上注册的事件events，以及其上实际发生的事件revents，都保存在了，ChannelMap表channels_中
  /// (3)从ChannelMap表channels_中，删除表项channel
  size_t n = channels_.erase(channel->fd());
  assert(n == 1); (void)n;
  /// 待删除的表项，在pollfds_表的末尾
  if (implicit_cast<size_t>(idx) == pollfds_.size()-1)
  {
  	/// 实施删除表项
    pollfds_.pop_back();
  }
  else/// 待删除的表项，在pollfds_表的中间位置
  {
  	/// 获取pollfds表中的，最后一个表项
    int channelAtEnd = pollfds_.back().fd;
    /// 将待删除的元素pollfds_.begin()+idx，与最后一个元素pollfds_.end()-1，进行交换
    iter_swap(pollfds_.begin()+idx, pollfds_.end()-1);
    if (channelAtEnd < 0)
    {
      channelAtEnd = -channelAtEnd-1;
    }
    channels_[channelAtEnd]->set_index(idx);
    /// 实施删除表项
    pollfds_.pop_back();
  }
}

