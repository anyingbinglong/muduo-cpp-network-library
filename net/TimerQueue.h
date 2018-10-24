// Copyright 2010, Shuo Chen.  All rights reserved.
// http://code.google.com/p/muduo/
//
// Use of this source code is governed by a BSD-style license
// that can be found in the License file.

// Author: Shuo Chen (chenshuo at chenshuo dot com)
//
// This is an internal header file, you should not include this.

#ifndef MUDUO_NET_TIMERQUEUE_H
#define MUDUO_NET_TIMERQUEUE_H

#include <set>
#include <vector>

#include <boost/noncopyable.hpp>

#include <muduo/base/Mutex.h>
#include <muduo/base/Timestamp.h>
#include <muduo/net/Callbacks.h>
#include <muduo/net/Channel.h>

namespace muduo
{
    namespace net
    {
        class EventLoop;
        class Timer;
        class TimerId;

        ///
        /// A best efforts timer queue.
        /// No guarantee that the callback will be on time.
        ///
        class TimerQueue : boost::noncopyable
        {
        public:
            explicit TimerQueue(EventLoop *loop);
            ~TimerQueue();

            ///
            /// Schedules the callback to be run at given time,
            /// repeats if @c interval > 0.0.
            ///
            /// Must be thread safe. Usually be called from other threads.
            /// 在定时器容器中，添加一个新的定时器：
            ///（1）定时器的回调函数为cb
            ///（2）定时器的超时时间为：以when时间点为起点，隔interval这么长的时间后，定时器超时
            TimerId addTimer(const TimerCallback &cb,
                             Timestamp when,
                             double interval);
#ifdef __GXX_EXPERIMENTAL_CXX0X__
            TimerId addTimer(TimerCallback &&cb,
                             Timestamp when,
                             double interval);
#endif
            // 注销定时器timerId：从定时器器容器timers_中，删除定时器timerId
            void cancel(TimerId timerId);

        private:

            // FIXME: use unique_ptr<Timer> instead of raw pointers.
            // This requires heterogeneous comparison lookup (N3465) from C++14
            // so that we can find an T* in a set<unique_ptr<T>>.
            typedef std::pair<Timestamp, Timer *> Entry;
            typedef std::set<Entry> TimerList;
            typedef std::pair<Timer *, int64_t> ActiveTimer;
            typedef std::set<ActiveTimer> ActiveTimerSet;

            void addTimerInLoop(Timer *timer);
            void cancelInLoop(TimerId timerId);

            // called when timerfd alarms
            // 定时器到期时，会调用这个函数
            // 执行与定时器相关联的定时器回调函数，处理定时器超时事件
            void handleRead();

            // move out all expired timers
            // 以系统当前时间now为基准，从定时器容器中，获取已超时的定时器，并将这些已超时的定时器从定时器容器中删除
            std::vector<Entry> getExpired(Timestamp now);

            // 将所有的已超时的定时器expired，以系统当前时间now为基准，进行复位（重启）
            void reset(const std::vector<Entry> &expired, Timestamp now);

            // 向定时器容器timers_中，插入一个新的定时器
            bool insert(Timer *timer);

            EventLoop *loop_;

            // 系统定时器文件描述符
            const int timerfd_;

            /// class Channel类的作用：
            /// （1）channel：通道，大管子，文件描述符（文件描述符：就是插在文件上的大管子）
            /// （2）记录：实际发生的事件的表的一个表项的类型（内容）
            /// （3）用一个class Channel类，来管理一个文件描述符
            // 管理上面的系统定时器文件描述符timerfd_
            Channel timerfdChannel_;
            
            // Timer list sorted by expiration
            // 定时器容器
            TimerList timers_;

            // for cancel()
            ActiveTimerSet activeTimers_;
            bool callingExpiredTimers_; /* atomic */
            ActiveTimerSet cancelingTimers_;
        };
    }
}
#endif  // MUDUO_NET_TIMERQUEUE_H
