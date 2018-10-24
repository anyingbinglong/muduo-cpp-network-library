// Copyright 2010, Shuo Chen.  All rights reserved.
// http://code.google.com/p/muduo/
//
// Use of this source code is governed by a BSD-style license
// that can be found in the License file.

// Author: Shuo Chen (chenshuo at chenshuo dot com)
//
// This is an internal header file, you should not include this.

#ifndef MUDUO_NET_TIMER_H
#define MUDUO_NET_TIMER_H

#include <boost/noncopyable.hpp>

#include <muduo/base/Atomic.h>
#include <muduo/base/Timestamp.h>
#include <muduo/net/Callbacks.h>

namespace muduo
{
    namespace net
    {
        ///
        /// 定时器类：一个对象，就是一个定时器，定时器需要放到定时器容器class TimerQueue中进行管理
        /// Internal class for timer event.
        ///
        class Timer : boost::noncopyable
        {
        public:
            Timer(const TimerCallback &cb, Timestamp when, double interval)
                : callback_(cb),
                  expiration_(when),
                  interval_(interval),
                  repeat_(interval > 0.0),
                  sequence_(s_numCreated_.incrementAndGet())
            { }

#ifdef __GXX_EXPERIMENTAL_CXX0X__
            Timer(TimerCallback &&cb, Timestamp when, double interval)
                : callback_(std::move(cb)),
                  expiration_(when),
                  interval_(interval),
                  repeat_(interval > 0.0),
                  sequence_(s_numCreated_.incrementAndGet())
            { }
#endif
            /// 执行定时器中的回调函数
            void run() const
            {
                callback_();
            }
            /// 获取定时器超时的绝对时间
            Timestamp expiration() const
            {
                return expiration_;
            }
            /// 获取是否重启定时器
            bool repeat() const
            {
                return repeat_;
            }
            int64_t sequence() const
            {
                return sequence_;
            }
            /// 以当前系统时间now为基准，重启定时器
            void restart(Timestamp now);

            static int64_t numCreated()
            {
                return s_numCreated_.get();
            }

        private:
            // 存放定时器中的回调函数
            const TimerCallback callback_;

            // 以下2个定时器超时时间，要配合使用，才能实现，定时器超时时间的准确设置
            // 例如：void restart(Timestamp now)，
            // 该函数的实现中：expiration_ = addTime(now, interval_);
            // 即：now + interval = expiration // 完成定时器超时时间（绝对时间）的准确设置
            Timestamp expiration_;// 定时器超时的绝对时间：指定在某个时间点，定时器超时
            const double interval_;// 定时器超时的相对时间：以当前时间点，为起点，隔interval_时间后，定时器超时

            // 设置是否重启定时器
            const bool repeat_;
            const int64_t sequence_;

            static AtomicInt64 s_numCreated_;
        };
    }
}
#endif  // MUDUO_NET_TIMER_H
