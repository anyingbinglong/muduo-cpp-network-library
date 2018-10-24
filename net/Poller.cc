// Copyright 2010, Shuo Chen.  All rights reserved.
// http://code.google.com/p/muduo/
//
// Use of this source code is governed by a BSD-style license
// that can be found in the License file.

// Author: Shuo Chen (chenshuo at chenshuo dot com)

#include <muduo/net/Poller.h>

#include <muduo/net/Channel.h>

using namespace muduo;
using namespace muduo::net;

Poller::Poller(EventLoop *loop)
    : ownerLoop_(loop)
{
}

Poller::~Poller()
{
}

/// 函数参数含义：
/// class Channel类的作用：
/// （1）channel：通道，大管子，文件描述符（文件描述符：就是插在文件上的大管子）
/// （2）记录：实际发生的事件的表的一个表项的类型（内容）
/// （3）用一个class Channel类，来管理一个文件描述符
/// 函数功能：
/// 从fd（文件描述符）到Channel*（class Channel文件描述符fd管理类）的映射
/// 网络库中，所有的文件描述符fd，其上注册的事件events，以及其上实际发生的事件revents，都保存在了，ChannelMap表中
/// 判断，channel，在ChannelMap表 channels_中是否存在
bool Poller::hasChannel(Channel *channel) const
{
    /// 确保：执行Poller::hasChannel函数的线程，是IO线程
    /// IO线程：创建了EventLoop对象的线程，就是IO线程
    assertInLoopThread();

    /// class Channel类的作用：
    /// （1）channel：通道，大管子，文件描述符（文件描述符：就是插在文件上的大管子）
    /// （2）记录：实际发生的事件的表的一个表项的类型（内容）
    /// （3）用一个class Channel类，来管理一个文件描述符
    /// 从fd（文件描述符）到Channel*（class Channel文件描述符fd管理类）的映射
    /// 网络库中，所有的文件描述符fd，其上注册的事件events，以及其上实际发生的事件revents，都保存在了，ChannelMap表中
    /// 判断，channel，在ChannelMap表 channels_中是否存在
    ChannelMap::const_iterator it = channels_.find(channel->fd());
    return it != channels_.end() && it->second == channel;
}

