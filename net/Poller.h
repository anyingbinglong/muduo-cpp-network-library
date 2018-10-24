// Copyright 2010, Shuo Chen.  All rights reserved.
// http://code.google.com/p/muduo/
//
// Use of this source code is governed by a BSD-style license
// that can be found in the License file.

// Author: Shuo Chen (chenshuo at chenshuo dot com)
//
// This is an internal header file, you should not include this.

#ifndef MUDUO_NET_POLLER_H
#define MUDUO_NET_POLLER_H

#include <map>
#include <vector>
#include <boost/noncopyable.hpp>

#include <muduo/base/Timestamp.h>
#include <muduo/net/EventLoop.h>

namespace muduo
{
    namespace net
    {

        class Channel;

        ///
        /// Base class for IO Multiplexing
        ///
        /// This class doesn't own the Channel objects.
        /// class Poller IO复用的封装：封装了poll 和 epoll
        class Poller : boost::noncopyable
        {
        public:
            /// std::vector<Channel*>的逻辑结构示意图：
            /// 记录，实际发生的事件的表，相当于传入epoll_wait函数，作为参数的数组，形成的那张表
            typedef std::vector<Channel *> ChannelList;

            Poller(EventLoop *loop);
            virtual ~Poller();

            /// Polls the I/O events.
            /// Must be called in the loop thread.
            /// ====================================================================================================
            /// 在，class PollPoller IO复用的封装：封装了poll，中的功能
            /// ====================================================================================================
            /// 函数参数含义：
            /// int timeoutMs：poll函数的超时时间
            /// ChannelList* activeChannels：记录实际发生的事件的表
            /// 函数功能：
            /// (1) numEvents：存放，有事件发生的文件描述符的总数
            ///   监听pollfds_表（相当于epoll的内核事件监听表）中，存放的文件描述符上，是否有相应的事件发生
            /// (2)遍历pollfds_表（相当于epoll的内核事件监听表），从中找出有事件发生的fd，并将该fd所对应的表项的内容，
            ///   填入到，记录实际发生的事件的表activeChannels中保存
            /// (3)这2个操作，合到一起，相当于epoll_wait函数的功能
            ///   因而，PollPoller::poll这个函数的功能，本质上，就是epoll_wait函数所做的事，
            ///   所以，PollPoller::poll这个函数，也就相当于epoll_wait函数
            /// ====================================================================================================
            /// 在，class EPollPoller IO复用的封装：封装了epoll中的功能
            /// ====================================================================================================
            /// 函数参数含义：
            /// int timeoutMs：epoll函数的超时时间
            /// ChannelList* activeChannels：记录实际发生的事件的表
            /// 函数功能：
            /// (1) numEvents：存放，有事件发生的文件描述符的总数
            ///   监听epoll的内核事件监听表epollfd_中，存放的文件描述符上，是否有相应的事件发生
            /// (2)遍历epoll的内核事件监听表epollfd_，从中找出有事件发生的fd，并将该fd所对应的表项的内容，
            ///   填入到，记录实际发生的事件的表activeChannels和记录实际发生的事件的表events_中保存
            /// (3)本质上，EPollPoller::poll这个函数，就是epoll_wait函数所做的事
            virtual Timestamp poll(int timeoutMs, ChannelList *activeChannels) = 0;

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
            virtual void updateChannel(Channel *channel) = 0;

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
            virtual void removeChannel(Channel *channel) = 0;

            /// 函数参数含义：
            /// class Channel类的作用：
            /// （1）channel：通道，大管子，文件描述符（文件描述符：就是插在文件上的大管子）
            /// （2）记录：实际发生的事件的表的一个表项的类型（内容）
            /// （3）用一个class Channel类，来管理一个文件描述符
            /// 函数功能：
            /// 从fd（文件描述符）到Channel*（class Channel文件描述符fd管理类）的映射
            /// 网络库中，所有的文件描述符fd，其上注册的事件events，以及其上实际发生的事件revents，都保存在了，ChannelMap表中
            /// 判断，channel，在ChannelMap表 channels_中是否存在
            virtual bool hasChannel(Channel *channel) const;

            static Poller *newDefaultPoller(EventLoop *loop);

            /// 确保:(1）执行事件循环（EventLoop::loop()）的线程，是IO线程
            ///      (2）执行某个函数的线程，是IO线程
            /// IO线程：创建了EventLoop对象的线程，就是IO线程
            void assertInLoopThread() const
            {
                ownerLoop_->assertInLoopThread();
            }

        protected:
            /// class Channel类的作用：
            /// （1）channel：通道，大管子，文件描述符（文件描述符：就是插在文件上的大管子）
            /// （2）记录：实际发生的事件的表的一个表项的类型（内容）
            /// （3）用一个class Channel类，来管理一个文件描述符
            /// 从fd（文件描述符）到Channel*（class Channel文件描述符fd管理类）的映射
            /// 网络库中，所有的文件描述符fd，其上注册的事件events，以及其上实际发生的事件revents，都保存在了，ChannelMap表中
            typedef std::map<int, Channel *> ChannelMap;
            ChannelMap channels_;

        private:
            EventLoop *ownerLoop_;
        };

    }
}
#endif  // MUDUO_NET_POLLER_H
