// Copyright 2010, Shuo Chen.  All rights reserved.
// http://code.google.com/p/muduo/
//
// Use of this source code is governed by a BSD-style license
// that can be found in the License file.

// Author: Shuo Chen (chenshuo at chenshuo dot com)
//
// This is a public header file, it must only include public header files.

#ifndef MUDUO_NET_EVENTLOOP_H
#define MUDUO_NET_EVENTLOOP_H

#include <vector>

#include <boost/any.hpp>
#include <boost/function.hpp>
#include <boost/noncopyable.hpp>
#include <boost/scoped_ptr.hpp>

#include <muduo/base/Mutex.h>
#include <muduo/base/CurrentThread.h>
#include <muduo/base/Timestamp.h>
#include <muduo/net/Callbacks.h>
#include <muduo/net/TimerId.h>

namespace muduo
{
    namespace net
    {

        class Channel;
        class Poller;
        class TimerQueue;

        ///
        /// Reactor, at most one per thread.
        ///
        /// This is an interface class, so don't expose too much details.
        class EventLoop : boost::noncopyable
        {
        public:
            typedef boost::function<void()> Functor;

            EventLoop();
            ~EventLoop();  // force out-line dtor, for scoped_ptr members.

            ///
            /// Loops forever.
            ///
            /// Must be called in the same thread as creation of the object.
            ///
            /// 事件循环：必须在IO线程中执行（IO线程：创建了EventLoop对象的线程）
            /// (1)遍历epoll的内核事件监听表epollfd_，从中找出有事件发生的fd，并将该fd所对应的表项的内容，
            ///   填入到，记录实际发生的事件的表activeChannels和记录实际发生的事件的表events_中保存
            /// (2)本质上，EPollPoller::poll这个函数，就是epoll_wait函数所做的事
            /// (3)遍历，记录实际发生的事件的表activeChannels_，并调用相应的事件处理函数，对发生的事件进行处理
            void loop();

            /// Quits loop.
            ///
            /// This is not 100% thread safe, if you call through a raw pointer,
            /// better to call through shared_ptr<EventLoop> for 100% safety.
            void quit();

            ///
            /// Time when poll returns, usually means data arrival.
            ///
            Timestamp pollReturnTime() const
            {
                return pollReturnTime_;
            }

            int64_t iteration() const
            {
                return iteration_;
            }

            /// Runs callback immediately in the loop thread.
            /// It wakes up the loop, and run the cb.
            /// If in the same loop thread, cb is run within the function.
            /// Safe to call from other threads.
            /// 在IO线程中，执行用户任务回调函数cb
            /// (1)当前在IO线程中，所以，可以立即执行用户任务回调函数cb
            /// (2)当前不在IO线程中，所以，就不能立即执行用户任务回回调函数
            /// 将需要在IO线程中执行的用户回调函数cb，放入到队列中保存，并在必要时唤醒IO线程，执行这个用户任务回调函数
            void runInLoop(const Functor &cb);

            /// Queues callback in the loop thread.
            /// Runs after finish pooling.
            /// Safe to call from other threads.
            /// 将需要在IO线程中执行的用户回调函数cb，放入到队列中保存，并在必要时唤醒IO线程，执行这个用户任务回调函数
            void queueInLoop(const Functor &cb);

            size_t queueSize() const;

#ifdef __GXX_EXPERIMENTAL_CXX0X__
            void runInLoop(Functor &&cb);
            void queueInLoop(Functor &&cb);
#endif

            // timers
            ///
            /// Runs callback at 'time'.
            /// Safe to call from other threads.
            ///
            /// 函数参数含义：
            /// const Timestamp &time：定时器的超时时间
            /// TimerCallback &&cb：定时器的回调函数
            /// 函数功能：
            /// 在时间点time处，定时器超时，并调用定时器回调函数
            TimerId runAt(const Timestamp &time, const TimerCallback &cb);
            ///
            /// Runs callback after @c delay seconds.
            /// Safe to call from other threads.
            ///
            /// 函数参数含义：
            /// double delay：定时器的超时时间
            /// TimerCallback &&cb：定时器的回调函数
            /// 函数功能：
            /// 以当前时间Timestamp::now()为起点，经过delay这么长的时间后，定时器超时，并调用定时器回调函数
            TimerId runAfter(double delay, const TimerCallback &cb);
            ///
            /// Runs callback every @c interval seconds.
            /// Safe to call from other threads.
            ///
            /// 函数参数含义：
            /// double interval：定时器的超时时间
            /// TimerCallback &&cb：定时器的回调函数
            /// 函数功能：
            /// 以当前时间Timestamp::now()为起点，经过interval + interval这么长的时间后，定时器超时，并调用定时器回调函数
            TimerId runEvery(double interval, const TimerCallback &cb);
            ///
            /// Cancels the timer.
            /// Safe to call from other threads.
            ///
            void cancel(TimerId timerId);

#ifdef __GXX_EXPERIMENTAL_CXX0X__
            TimerId runAt(const Timestamp &time, TimerCallback &&cb);
            TimerId runAfter(double delay, TimerCallback &&cb);
            TimerId runEvery(double interval, TimerCallback &&cb);
#endif

            // internal usage
            /// 非IO线程执行此函数，向IO线程发送一个数据1，实现唤醒IO线程
            void wakeup();

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
            void updateChannel(Channel *channel);

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
            void removeChannel(Channel *channel);

            /// 函数参数含义：
            /// class Channel类的作用：
            /// （1）channel：通道，大管子，文件描述符（文件描述符：就是插在文件上的大管子）
            /// （2）记录：实际发生的事件的表的一个表项的类型（内容）
            /// （3）用一个class Channel类，来管理一个文件描述符
            /// 函数功能：
            /// 从fd（文件描述符）到Channel*（class Channel文件描述符fd管理类）的映射
            /// 网络库中，所有的文件描述符fd，其上注册的事件events，以及其上实际发生的事件revents，都保存在了，ChannelMap表中
            /// 判断，channel，在ChannelMap表 channels_中是否存在
            bool hasChannel(Channel *channel);

            /// pid_t threadId() const { return threadId_; }
            /// 确保:(1）执行事件循环（EventLoop::loop()）的线程，是IO线程
            ///      (2）执行某个函数的线程，是IO线程
            /// IO线程：创建了EventLoop对象的线程，就是IO线程
            void assertInLoopThread()
            {
                if (!isInLoopThread())/// 不在IO线程中
                {
                    /// 中断非IO线程的执行
                    abortNotInLoopThread();
                }
            }

            /// threadId_记录：IO线程的ID
            /// CurrentThread::tid()：获取当前线程的ID
            /// 判断当前是否在IO线程中执行
            bool isInLoopThread() const
            {
                return threadId_ == CurrentThread::tid();
            }
            // bool callingPendingFunctors() const { return callingPendingFunctors_; }
            bool eventHandling() const
            {
                return eventHandling_;
            }

            void setContext(const boost::any &context)
            {
                context_ = context;
            }

            const boost::any &getContext() const
            {
                return context_;
            }

            boost::any *getMutableContext()
            {
                return &context_;
            }

            /// 获得每个线程中仅有的那个EventLoop对象
            static EventLoop *getEventLoopOfCurrentThread();

        private:
            void abortNotInLoopThread();
            void handleRead();  // waked up
            /// 在IO线程中，执行某个用户任务回调函数
            void doPendingFunctors();

            void printActiveChannels() const; // DEBUG



            /// 记录：IO线程，是否正在执行事件循环EventLoop::loop()
            bool looping_; /* atomic */
            bool quit_; /* atomic and shared between threads, okay on x86, I guess. */
            bool eventHandling_; /* atomic */
            bool callingPendingFunctors_; /* atomic */
            int64_t iteration_;
            /// 记录：IO线程的ID
            const pid_t threadId_;
            Timestamp pollReturnTime_;

            /// class Poller IO复用的封装：封装了poll 和 epoll
            boost::scoped_ptr<Poller> poller_;

            /// 定时器容器
            boost::scoped_ptr<TimerQueue> timerQueue_;

            /// 用于唤醒IO线程的文件描述符
            int wakeupFd_;
            // unlike in TimerQueue, which is an internal class,
            // we don't expose Channel to client.
            /// class Channel类的作用：
            /// （1）channel：通道，大管子，文件描述符（文件描述符：就是插在文件上的大管子）
            /// （2）记录：实际发生的事件的表的一个表项的类型（内容）
            /// （3）用一个class Channel类，来管理一个文件描述符
            /// wakeupChannel_：管理，用于唤醒IO线程的文件描述符wakeupFd_
            boost::scoped_ptr<Channel> wakeupChannel_;
            boost::any context_;

            /// 用于定义：记录实际发生的事件的表
            typedef std::vector<Channel *> ChannelList;
            /// 记录实际发生的事件的表
            ChannelList activeChannels_;

            /// class Channel类的作用：
            /// （1）channel：通道，大管子，文件描述符（文件描述符：就是插在文件上的大管子）
            /// （2）记录：实际发生的事件的表的一个表项的类型（内容）
            /// （3）用一个class Channel类，来管理一个文件描述符
            /// currentActiveChannel_：管理(存放)，从记录实际发生的事件的表activeChannels_中，取出来的一个表项
            Channel *currentActiveChannel_;

            /// 保护下面的pendingFunctors_
            mutable MutexLock mutex_;
            /// 存放：需要延期执行的用户任务回调：这些回调函数，都是需要在IO线程中执行的用户任务回调
            /// 多线程共享资源
            std::vector<Functor> pendingFunctors_; // @GuardedBy mutex_
        };
    }
}
#endif  // MUDUO_NET_EVENTLOOP_H
