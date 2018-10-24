// Copyright 2010, Shuo Chen.  All rights reserved.
// http://code.google.com/p/muduo/
//
// Use of this source code is governed by a BSD-style license
// that can be found in the License file.

// Author: Shuo Chen (chenshuo at chenshuo dot com)

#ifndef __STDC_LIMIT_MACROS
#define __STDC_LIMIT_MACROS
#endif

#include <muduo/net/TimerQueue.h>

#include <muduo/base/Logging.h>
#include <muduo/net/EventLoop.h>
#include <muduo/net/Timer.h>
#include <muduo/net/TimerId.h>

#include <boost/bind.hpp>

#include <sys/timerfd.h>
#include <unistd.h>

namespace muduo
{
    namespace net
    {
        namespace detail
        {
            // 创建一个系统定时器
            int createTimerfd()
            {
                int timerfd = ::timerfd_create(CLOCK_MONOTONIC,
                                               TFD_NONBLOCK | TFD_CLOEXEC);
                if (timerfd < 0)
                {
                    LOG_SYSFATAL << "Failed in timerfd_create";
                }
                return timerfd;
            }
            // 以系统当前时间now为基准，计算，再过多久，到达指定的when绝对时间点
            struct timespec howMuchTimeFromNow(Timestamp when)
            {
                int64_t microseconds = when.microSecondsSinceEpoch()
                                       - Timestamp::now().microSecondsSinceEpoch();
                if (microseconds < 100)
                {
                    microseconds = 100;
                }
                //  struct timespec
                //  {
                //     time_t tv_sec;                /* Seconds */
                //     long   tv_nsec;               /* Nanoseconds */
                //  };
                struct timespec ts;
                ts.tv_sec = static_cast<time_t>(
                                microseconds / Timestamp::kMicroSecondsPerSecond);
                ts.tv_nsec = static_cast<long>(
                                 (microseconds % Timestamp::kMicroSecondsPerSecond) * 1000);
                return ts;
            }
            // 系统定时器timerfd，超时后，会向使用它的应用进程发送消息，来唤醒处于等待（睡眠）状态的进程
            // 这个函数的作用：读取系统定时器，向应用进程发来的消息
            void readTimerfd(int timerfd, Timestamp now)
            {
                // 存放，系统定时器，向应用进程发来的消息
                uint64_t howmany;
                // 读取系统定时器，向应用进程发来的消息
                ssize_t n = ::read(timerfd, &howmany, sizeof howmany);
                LOG_TRACE << "TimerQueue::handleRead() " << howmany << " at " << now.toString();
                if (n != sizeof howmany)
                {
                    LOG_ERROR << "TimerQueue::handleRead() reads " << n << " bytes instead of 8";
                }
            }
            // 为系统定时器timerfd，指定新的绝对超时时间expiration
            void resetTimerfd(int timerfd, Timestamp expiration)
            {
                // wake up loop by timerfd_settime()
                //             struct timespec
                //             {
                //                 time_t tv_sec;                /* Seconds */
                //                 long   tv_nsec;               /* Nanoseconds */
                //             };

                //             struct itimerspec
                //             {
                //                 struct timespec it_interval;  /* Interval for periodic timer （定时间隔周期）*/
                //                 struct timespec it_value;     /* Initial expiration (第一次超时时间)*/
                //             };
                //             int timerfd_settime(int fd, int flags, const struct itimerspec * new_value, struct itimerspec * old_value);

                //             timerfd_settime()此函数用于设置新的超时时间，并开始计时, 能够启动和停止定时器;
                // fd:
                //             参数fd，是timerfd_create函数返回的定时器文件句柄
                // flags：
                //             参数flags，为1代表设置的是绝对时间（TFD_TIMER_ABSTIME 表示绝对定时器）；
                //                        为0代表相对时间。
                // new_value:
                //             参数new_value指定定时器新的超时时间以及超时间隔时间
                // old_value:
                //             如果old_value不为NULL, old_vlaue返回之前定时器设置的超时时间

                //             it_interval不为0则表示是周期性定时器。
                //             it_value和it_interval都为0表示停止定时器

                // 定时器新的超时时间以及超时间隔时间
                struct itimerspec newValue;
                // 存放之前定时器设置的超时时间
                struct itimerspec oldValue;
                bzero(&newValue, sizeof newValue);
                bzero(&oldValue, sizeof oldValue);
                // expiration - now = newValue.it_value
                // 计算系统定时器timerfd，新的相对超时时间newValue.it_value
                newValue.it_value = howMuchTimeFromNow(expiration);
                // 系统定时器timerfd，再过newValue这么长的时间后，超时
                int ret = ::timerfd_settime(timerfd, 0, &newValue, &oldValue);
                if (ret)
                {
                    LOG_SYSERR << "timerfd_settime()";
                }
            }
        }
    }
}

using namespace muduo;
using namespace muduo::net;
using namespace muduo::net::detail;

TimerQueue::TimerQueue(EventLoop *loop)
    : loop_(loop),
      timerfd_(createTimerfd()),
      timerfdChannel_(loop, timerfd_),
      timers_(),
      callingExpiredTimers_(false)
{
    timerfdChannel_.setReadCallback(
        boost::bind(&TimerQueue::handleRead, this));
    // we are always reading the timerfd, we disarm it with timerfd_settime.
    timerfdChannel_.enableReading();
}

TimerQueue::~TimerQueue()
{
    timerfdChannel_.disableAll();
    timerfdChannel_.remove();
    ::close(timerfd_);
    // do not remove channel, since we're in EventLoop::dtor();
    for (TimerList::iterator it = timers_.begin();
            it != timers_.end(); ++it)
    {
        delete it->second;
    }
}

// 在定时器容器中，添加一个新的定时器：
// （1）定时器的回调函数为cb
// （2）定时器的超时时间为：以when时间点为起点，隔interval这么长的时间后，定时器超时
TimerId TimerQueue::addTimer(const TimerCallback &cb,
                             Timestamp when,
                             double interval)
{
    // 创建一个新的定时器：
    // （1）定时器的回调函数为cb
    // （2）定时器的超时时间为：以when时间点为起点，隔interval这么长的时间后，定时器超时
    Timer *timer = new Timer(cb, when, interval);
    loop_->runInLoop(
        boost::bind(&TimerQueue::addTimerInLoop, this, timer));
    // 返回新创建的定时器timer
    return TimerId(timer, timer->sequence());
}

#ifdef __GXX_EXPERIMENTAL_CXX0X__
TimerId TimerQueue::addTimer(TimerCallback &&cb,
                             Timestamp when,
                             double interval)
{
    Timer *timer = new Timer(std::move(cb), when, interval);
    loop_->runInLoop(
        boost::bind(&TimerQueue::addTimerInLoop, this, timer));
    return TimerId(timer, timer->sequence());
}
#endif

void TimerQueue::addTimerInLoop(Timer *timer)
{
    loop_->assertInLoopThread();
    // 向定时器容器timers_中，插入一个新的定时器timer
    bool earliestChanged = insert(timer);

    if (earliestChanged)
    {
        // 为系统定时器timerfd，指定新的绝对超时时间timer->expiration()
        resetTimerfd(timerfd_, timer->expiration());
    }
}

// 注销定时器timerId：从定时器器容器timers_中，删除定时器timerId
void TimerQueue::cancel(TimerId timerId)
{
    loop_->runInLoop(
        boost::bind(&TimerQueue::cancelInLoop, this, timerId));
}

void TimerQueue::cancelInLoop(TimerId timerId)
{
    loop_->assertInLoopThread();
    assert(timers_.size() == activeTimers_.size());
    ActiveTimer timer(timerId.timer_, timerId.sequence_);
    ActiveTimerSet::iterator it = activeTimers_.find(timer);
    if (it != activeTimers_.end())
    {
        // 从定时器器容器timers_中，删除定时器timerId
        size_t n = timers_.erase(Entry(it->first->expiration(), it->first));
        assert(n == 1);
        (void)n;
        delete it->first; // FIXME: no delete please
        activeTimers_.erase(it);
    }
    else if (callingExpiredTimers_)
    {
        cancelingTimers_.insert(timer);
    }
    assert(timers_.size() == activeTimers_.size());
}

void TimerQueue::handleRead()
{
    loop_->assertInLoopThread();
    // 获取系统当前时间
    Timestamp now(Timestamp::now());
    // 系统定时器timerfd，超时后，会向使用它的应用进程发送消息，来唤醒处于等待（睡眠）状态的进程
    // 这个函数的作用：读取系统定时器，向应用进程发来的消息
    readTimerfd(timerfd_, now);
    // 以系统当前时间now为基准，从定时器容器中，获取已超时的定时器
    std::vector<Entry> expired = getExpired(now);

    callingExpiredTimers_ = true;
    cancelingTimers_.clear();
    // safe to callback outside critical section
    // 遍历已超时的定时器expired，执行该定时器中的回调函数
    for (std::vector<Entry>::iterator it = expired.begin();
            it != expired.end(); ++it)
    {
        // 执行定时器中的回调函数
        it->second->run();
    }
    callingExpiredTimers_ = false;
    // 将所有的已超时的定时器expired，以系统当前时间now为基准，进行复位（重启）
    reset(expired, now);
}

// 以系统当前时间now为基准，从定时器容器中，获取已超时的定时器，并将这些已超时的定时器从定时器容器中删除
std::vector<TimerQueue::Entry> TimerQueue::getExpired(Timestamp now)
{
    assert(timers_.size() == activeTimers_.size());
    std::vector<Entry> expired;
    Entry sentry(now, reinterpret_cast<Timer *>(UINTPTR_MAX));
    TimerList::iterator end = timers_.lower_bound(sentry);
    assert(end == timers_.end() || now < end->first);
    std::copy(timers_.begin(), end, back_inserter(expired));
    timers_.erase(timers_.begin(), end);

    for (std::vector<Entry>::iterator it = expired.begin();
            it != expired.end(); ++it)
    {
        ActiveTimer timer(it->second, it->second->sequence());
        size_t n = activeTimers_.erase(timer);
        assert(n == 1);
        (void)n;
    }

    assert(timers_.size() == activeTimers_.size());
    return expired;
}

// 将所有的已超时的定时器expired，以系统当前时间now为基准，进行复位（重启）
void TimerQueue::reset(const std::vector<Entry> &expired, Timestamp now)
{
    // 存放，为系统定时器timerfd_，指定的新的绝对超时时间
    Timestamp nextExpire;
    // 遍历已超时的定时器expired，以当前系统时间now为基准，重启定时器
    for (std::vector<Entry>::const_iterator it = expired.begin();
            it != expired.end(); ++it)
    {
        ActiveTimer timer(it->second, it->second->sequence());
        /// 获取是否重启定时器：重启定时器
        if (it->second->repeat()
                && cancelingTimers_.find(timer) == cancelingTimers_.end())
        {
            /// 以当前系统时间now为基准，重启定时器
            it->second->restart(now);
            insert(it->second);
        }
        else// 不重启定时器
        {
            // FIXME move to a free list
            delete it->second; // FIXME: no delete please
        }
    }
    // 定时器容器timers，不为空
    if (!timers_.empty())
    {
        // 定时器容器timers中，第一个定时器的超时时间的绝对时间
        // 作为系统定时器timerfd_，指定新的绝对超时时间nextExpire
        nextExpire = timers_.begin()->second->expiration();
    }

    if (nextExpire.valid())
    {
        // 为系统定时器timerfd_，指定新的绝对超时时间nextExpire
        resetTimerfd(timerfd_, nextExpire);
    }
}

// 向定时器容器timers_中，插入一个新的定时器
bool TimerQueue::insert(Timer *timer)
{
    loop_->assertInLoopThread();
    assert(timers_.size() == activeTimers_.size());
    bool earliestChanged = false;
    /// 获取定时器超时的绝对时间
    Timestamp when = timer->expiration();
    TimerList::iterator it = timers_.begin();
    if (it == timers_.end() || when < it->first)
    {
        earliestChanged = true;
    }
    {
        std::pair<TimerList::iterator, bool> result
            = timers_.insert(Entry(when, timer));
        assert(result.second);
        (void)result;
    }
    {
        std::pair<ActiveTimerSet::iterator, bool> result
            = activeTimers_.insert(ActiveTimer(timer, timer->sequence()));
        assert(result.second);
        (void)result;
    }

    assert(timers_.size() == activeTimers_.size());
    return earliestChanged;
}

