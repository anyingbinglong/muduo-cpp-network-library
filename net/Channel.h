// Copyright 2010, Shuo Chen.  All rights reserved.
// http://code.google.com/p/muduo/
//
// Use of this source code is governed by a BSD-style license
// that can be found in the License file.

// Author: Shuo Chen (chenshuo at chenshuo dot com)
//
// This is an internal header file, you should not include this.

#ifndef MUDUO_NET_CHANNEL_H
#define MUDUO_NET_CHANNEL_H

#include <boost/function.hpp>
#include <boost/noncopyable.hpp>
#include <boost/shared_ptr.hpp>
#include <boost/weak_ptr.hpp>

#include <muduo/base/Timestamp.h>

namespace muduo
{
    namespace net
    {
        class EventLoop;

        ///
        /// A selectable I/O channel.
        ///
        /// This class doesn't own the file descriptor.
        /// The file descriptor could be a socket,
        /// an eventfd, a timerfd, or a signalfd
        /// class Channel类的作用：
        /// （1）channel：通道，大管子，文件描述符（文件描述符：就是插在文件上的大管子）
        /// （2）记录：实际发生的事件的表的一个表项的类型（内容）
        /// （3）用一个class Channel类，来管理一个文件描述符
        class Channel : boost::noncopyable
        {
        public:
            typedef boost::function<void()> EventCallback;
            typedef boost::function<void(Timestamp)> ReadEventCallback;

            Channel(EventLoop *loop, int fd);
            ~Channel();

            /// revents_：里面存放着已经就绪(实际发生)的事件（由内核填充）
            /// 分发：调用某个socket文件描述符上所发生的事件，所对应的事件处理函数，处理发生的事件的这个过程，就是分发
            /// 实现事件分发机制：根据class Channel所管理的文件描述符上，实际发生（已经就绪）的事件revents_，调用相应的事件处理函数
            void handleEvent(Timestamp receiveTime);

            /// 设置：class Channel类，所管理的文件描述符fd_;上，有读事件发生时，需要调用的读事件处理函数
            void setReadCallback(const ReadEventCallback &cb)
            {
                readCallback_ = cb;
            }
             /// 设置：class Channel类，所管理的文件描述符fd_;上，有写事件发生时，需要调用的写事件处理函数
            void setWriteCallback(const EventCallback &cb)
            {
                writeCallback_ = cb;
            }
             /// 设置：class Channel类，所管理的文件描述符fd_;上，有关闭事件发生时，需要调用的关闭事件处理函数
            void setCloseCallback(const EventCallback &cb)
            {
                closeCallback_ = cb;
            }
             /// 设置：class Channel类，所管理的文件描述符fd_;上，有错误事件发生时，需要调用的错误事件处理函数
            void setErrorCallback(const EventCallback &cb)
            {
                errorCallback_ = cb;
            }
#ifdef __GXX_EXPERIMENTAL_CXX0X__
            void setReadCallback(ReadEventCallback &&cb)
            {
                readCallback_ = std::move(cb);
            }
            void setWriteCallback(EventCallback &&cb)
            {
                writeCallback_ = std::move(cb);
            }
            void setCloseCallback(EventCallback &&cb)
            {
                closeCallback_ = std::move(cb);
            }
            void setErrorCallback(EventCallback &&cb)
            {
                errorCallback_ = std::move(cb);
            }
#endif

            /// Tie this channel to the owner object managed by shared_ptr,
            /// prevent the owner object being destroyed in handleEvent.
            void tie(const boost::shared_ptr<void> &);

            /// 获得：class Channel类，所管理的文件描述符fd_
            int fd() const
            {
                return fd_;
            }

            /// 获得：关心的IO事件（用户注册的事件）events_
            int events() const
            {
                return events_;
            }

            /// 在class Channel中，设置（保存），其所管理的文件描述符上实际发生的事件（由内核填充）revents_
            void set_revents(int revt)
            {
                revents_ = revt;    // used by pollers
            }

            // int revents() const { return revents_; }
            /// 判断：用户是否没有在fd上，注册任何事件
            bool isNoneEvent() const
            {
                return events_ == kNoneEvent;
            }

            /// 在epoll的内核事件监听表中，注册class Channel类，所管理的文件描述符fd_;
            /// 并让epoll_wait关注其上是否有读事件发生
            void enableReading()
            {
                /// events_：关心的IO事件（用户注册的事件）
                /// 在epoll的内核事件监听表中，注册class Channel类，所管理的文件描述符fd_;
                /// 并让epoll_wait关注其上是否有读事件发生
                events_ |= kReadEvent;
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
                update();
            }

            /// 在epoll的内核事件监听表中，找到，class Channel类，所管理的文件描述符fd_;
            /// 并让epoll_wait取消关注其上是否有读事件发生
            void disableReading()
            {
                /// events_：关心的IO事件（用户注册的事件）
                events_ &= ~kReadEvent;
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
                update();
            }

            /// 在epoll的内核事件监听表中，注册class Channel类，所管理的文件描述符fd_;
            /// 并让epoll_wait关注其上是否有写事件发生
            void enableWriting()
            {
                /// events_：关心的IO事件（用户注册的事件）
                /// 在epoll的内核事件监听表中，注册class Channel类，所管理的文件描述符fd_;
                /// 并让epoll_wait关注其上是否有写事件发生
                events_ |= kWriteEvent;
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
                update();
            }

            /// 在epoll的内核事件监听表中，找到，class Channel类，所管理的文件描述符fd_;
            /// 并让epoll_wait取消关注其上是否有写事件发生
            void disableWriting()
            {
                /// events_：关心的IO事件（用户注册的事件）
                events_ &= ~kWriteEvent;
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
                update();
            }

            /// 不再关注class Channel类，所管理的文件描述符上的任何事件
            void disableAll()
            {
                /// events_：关心的IO事件（用户注册的事件）
                events_ = kNoneEvent;
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
                update();
            }

            bool isWriting() const
            {
                return events_ & kWriteEvent;
            }
            bool isReading() const
            {
                return events_ & kReadEvent;
            }

            // for Poller
            /// 获取：
            /// （1）class Channel类，所管理的文件描述符，对应的struct pollfd，
            /// 在struct pollfd结构类型数组PollFdList pollfds中的位置（下标）
            /// （2）class Channel类，所管理的文件描述符，在pollfds表（相当于epoll的内核事件监听表）中的位置
            int index()
            {
                return index_;
            }
            /// 设置：
            /// （1）class Channel类，所管理的文件描述符，对应的struct pollfd，
            /// 在struct pollfd结构类型数组PollFdList pollfds中的位置（下标）
            /// （2）class Channel类，所管理的文件描述符，在pollfds表（相当于epoll的内核事件监听表）中的位置
            void set_index(int idx)
            {
                index_ = idx;
            }

            // for debug
            string reventsToString() const;
            string eventsToString() const;

            void doNotLogHup()
            {
                logHup_ = false;
            }

            EventLoop *ownerLoop()
            {
                return loop_;
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
            void remove();

        private:
            static string eventsToString(int fd, int ev);
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
            void update();
            void handleEventWithGuard(Timestamp receiveTime);

            static const int kNoneEvent;
            static const int kReadEvent;
            static const int kWriteEvent;

            EventLoop *loop_;

            /// 以下3个数据成员，构成：记录实际发生的事件的表，的，一个表项的，表项内容
            /// class Channel类，所管理的文件描述符
            const int  fd_;
            /// 关心的IO事件（用户注册的事件）
            int        events_;
            /// 实际发生的事件（由内核填充）
            int        revents_; // it's the received event types of epoll or poll

            /// （1）class Channel类，所管理的文件描述符，对应的struct pollfd，
            /// 在struct pollfd结构类型数组PollFdList pollfds中的位置（下标）
            /// （2）class Channel类，所管理的文件描述符，在pollfds表（相当于epoll的内核事件监听表）中的位置
            int        index_; // used by Poller.

            bool       logHup_;

            boost::weak_ptr<void> tie_;
            bool tied_;
            bool eventHandling_;
            bool addedToLoop_;

            /// 存放：读事件的，事件回调函数
            ReadEventCallback readCallback_;
            /// 存放：写事件的，事件回调函数
            EventCallback writeCallback_;
            /// 存放：关闭事件的，事件回调函数
            EventCallback closeCallback_;
            /// 存放：错误事件的，事件回调函数
            EventCallback errorCallback_;
        };
    }
}
#endif  // MUDUO_NET_CHANNEL_H
