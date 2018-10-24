// Copyright 2010, Shuo Chen.  All rights reserved.
// http://code.google.com/p/muduo/
//
// Use of this source code is governed by a BSD-style license
// that can be found in the License file.

// Author: Shuo Chen (chenshuo at chenshuo dot com)

#include <muduo/net/Timer.h>

using namespace muduo;
using namespace muduo::net;

AtomicInt64 Timer::s_numCreated_;
/// 以当前系统时间now为基准，重启定时器
void Timer::restart(Timestamp now)
{
    if (repeat_)// 判断是否重启定时器：重新启动定时器
    {
        // 更新定时器超时的绝对时间expiration：
        // now + interval = expiration
        expiration_ = addTime(now, interval_);
    }
    else// 不重新启动定时器
    {
        expiration_ = Timestamp::invalid();
    }
}
