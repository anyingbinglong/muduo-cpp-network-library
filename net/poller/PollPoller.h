// Copyright 2010, Shuo Chen.  All rights reserved.
// http://code.google.com/p/muduo/
//
// Use of this source code is governed by a BSD-style license
// that can be found in the License file.

// Author: Shuo Chen (chenshuo at chenshuo dot com)
//
// This is an internal header file, you should not include this.

#ifndef MUDUO_NET_POLLER_POLLPOLLER_H
#define MUDUO_NET_POLLER_POLLPOLLER_H

#include <muduo/net/Poller.h>

#include <vector>

struct pollfd;

namespace muduo
{
    namespace net
    {

        ///
        /// IO Multiplexing with poll(2).
        ///
        /// class PollPoller IO复用的封装：封装了poll
        class PollPoller : public Poller
        {
        public:

            PollPoller(EventLoop *loop);
            virtual ~PollPoller();

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
            virtual Timestamp poll(int timeoutMs, ChannelList *activeChannels);

            /// 函数参数含义：
            /// Channel* channel：
            ///   （1）文件描述符fd管理类
            ///   （2）将要存放到，pollfds_表（相当于epoll的内核事件监听表）的，一个表项内容
            ///    (3) 将要修改的，pollfds_表（相当于epoll的内核事件监听表）的，一个表项内容
            /// 函数功能：
            /// 这个操作，相当于epoll_ctl函数参数选项中的EPOLL_CTL_ADD和EPOLL_CTL_MOD
            /// （1）在pollfds_表（相当于epoll的内核事件监听表）中，新增一个表项
            /// （2）修改pollfds_表（相当于epoll的内核事件监听表）中的，某个表项
            virtual void updateChannel(Channel *channel);

            /// 函数参数含义：
            /// Channel* channel：
            ///   （1）文件描述符fd管理类
            ///   （2）将要删除的，pollfds_表（相当于epoll的内核事件监听表）的，一个表项内容
            /// 函数功能：
            /// 这个操作，相当于epoll_ctl函数参数选项中的EPOLL_CTL_DEL
            /// 删除pollfds_表（相当于epoll的内核事件监听表）中的，某个表项
            virtual void removeChannel(Channel *channel);

        private:
            /// 函数参数含义：
            /// int numEvents：存放，有事件发生的文件描述符的总数
            /// ChannelList* activeChannels：记录实际发生的事件的表
            /// 函数功能：
            /// 遍历pollfds_表（相当于epoll的内核事件监听表），从中找出有事件发生的fd，并将该fd所对应的表项的内容，
            /// 填入到，记录实际发生的事件的表activeChannels中保存
            void fillActiveChannels(int numEvents,
                                    ChannelList *activeChannels) const;

            /// struct pollfd结构类型的数组，存储我们感兴趣的文件描述符上发生的可读、可写和异常事件
            /// std::vector<struct pollfd>的逻辑结构示意图：相当于epoll中的内核事件监听表
            typedef std::vector<struct pollfd> PollFdList;

            /// pollfds_表：
            /// （1）逻辑结构示意图：相当于epoll中的内核事件监听表
            /// （2）创建pollfds_对象的操作，相当于调用epoll_create函数，创建了一张epoll内核事件监听表
            PollFdList pollfds_;
        };
    }
}
#endif  // MUDUO_NET_POLLER_POLLPOLLER_H
