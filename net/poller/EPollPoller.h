// Copyright 2010, Shuo Chen.  All rights reserved.
// http://code.google.com/p/muduo/
//
// Use of this source code is governed by a BSD-style license
// that can be found in the License file.

// Author: Shuo Chen (chenshuo at chenshuo dot com)
//
// This is an internal header file, you should not include this.

#ifndef MUDUO_NET_POLLER_EPOLLPOLLER_H
#define MUDUO_NET_POLLER_EPOLLPOLLER_H

#include <muduo/net/Poller.h>

#include <vector>

struct epoll_event;

namespace muduo
{
    namespace net
    {

        ///
        /// IO Multiplexing with epoll(4).
        ///
        /// class EPollPoller IO复用的封装：封装了epoll
        class EPollPoller : public Poller
        {
        public:
            EPollPoller(EventLoop *loop);
            virtual ~EPollPoller();

            /// 函数参数含义：
            /// int timeoutMs：epoll函数的超时时间
            /// ChannelList* activeChannels：记录实际发生的事件的表
            /// 函数功能：
            /// (1) numEvents：存放，有事件发生的文件描述符的总数
            ///   监听epoll的内核事件监听表epollfd_中，存放的文件描述符上，是否有相应的事件发生
            /// (2)遍历epoll的内核事件监听表epollfd_，从中找出有事件发生的fd，并将该fd所对应的表项的内容，
            ///   填入到，记录实际发生的事件的表activeChannels和记录实际发生的事件的表events_中保存
            /// (3)本质上，EPollPoller::poll这个函数，就是epoll_wait函数所做的事
            virtual Timestamp poll(int timeoutMs, ChannelList *activeChannels);

            /// 函数参数含义：
            /// Channel* channel：
            ///   （1）文件描述符fd管理类
            ///   （2）将要存放到，epoll的内核事件监听表epollfd_中的，一个表项内容
            ///    (3) 将要修改的，epoll的内核事件监听表epollfd_中的，一个表项内容
            /// 函数功能：
            /// 这个操作，相当于epoll_ctl函数参数选项中的EPOLL_CTL_ADD和EPOLL_CTL_MOD
            /// （1）在epoll的内核事件监听表epollfd_中，新增一个表项
            /// （2）修改epoll的内核事件监听表epollfd_中的，某个表项
            virtual void updateChannel(Channel *channel);

            /// 函数参数含义：
            /// Channel* channel：
            ///   （1）文件描述符fd管理类
            ///   （2）将要删除的，epoll的内核事件监听表epollfd_中的，一个表项内容
            /// 函数功能：
            /// 这个操作，相当于epoll_ctl函数参数选项中的EPOLL_CTL_DEL
            /// 删除epoll的内核事件监听表epollfd_中的，某个表项
            virtual void removeChannel(Channel *channel);

        private:
            /// 用于设置：
            /// 记录实际发生的事件的表
            /// 也就是传入epoll_wait函数的参数，用于记录实际发生的事件的表，的，初始的大小
            static const int kInitEventListSize = 16;

            static const char *operationToString(int op);

            /// 函数参数含义：
            /// int numEvents：存放，有事件发生的文件描述符的总数
            /// ChannelList* activeChannels：记录实际发生的事件的表
            /// 函数功能：
            /// 遍历events_表（用于记录实际发生的事件的表），从中找出有事件发生的fd，并将该fd所对应的表项的内容，
            /// 填入到，记录实际发生的事件的表activeChannels中保存
            void fillActiveChannels(int numEvents,
                                    ChannelList *activeChannels) const;

            /// 函数参数含义：
            /// operation：传入epoll_ctl函数的参数 EPOLL_CTL_DEL，EPOLL_CTL_MOD，EPOLL_CTL_ADD
            /// 函数功能：
            /// 在epoll的内核事件监听表epollfd_中，对表项内容channel，实施operation操作
            void update(int operation, Channel *channel);

            /// 用于定义：
            /// 记录实际发生的事件的表
            /// 也就是传入epoll_wait函数的参数，用于记录实际发生的事件的表
            typedef std::vector<struct epoll_event> EventList;
            /// 记录实际发生的事件的表
            /// 也就是传入epoll_wait函数的参数，用于记录实际发生的事件的表
            EventList events_;

            /// 指向epoll_create1创建的，epoll内核事件监听表
            /// 也可以说，epollfd_就代表epoll内核事件监听表
            int epollfd_;
        };
    }
}
#endif  // MUDUO_NET_POLLER_EPOLLPOLLER_H
