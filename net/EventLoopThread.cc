/// Copyright 2010, Shuo Chen.  All rights reserved.
/// http:///code.google.com/p/muduo/
///
/// Use of this source code is governed by a BSD-style license
/// that can be found in the License file.

/// Author: Shuo Chen (chenshuo at chenshuo dot com)

#include <muduo/net/EventLoopThread.h>

#include <muduo/net/EventLoop.h>

#include <boost/bind.hpp>

using namespace muduo;
using namespace muduo::net;


EventLoopThread::EventLoopThread(const ThreadInitCallback &cb,
                                 const string &name)
    : loop_(NULL),
      exiting_(false),/// 记录：IO线程，没有退出
      /// 指定，事件循环线程（IO线程）的，线程体，为：EventLoopThread::threadFunc
      thread_(boost::bind(&EventLoopThread::threadFunc, this), name),
      mutex_(),
      cond_(mutex_),
      callback_(cb)
{
}

EventLoopThread::~EventLoopThread()
{
    /// 记录：IO线程，正在退出
    exiting_ = true;
    if (loop_ != NULL) /// not 100% race-free, eg. threadFunc could be running callback_.
    {
        /// still a tiny chance to call destructed object, if threadFunc exits just now.
        /// but when EventLoopThread destructs, usually programming is exiting anyway.
        /// 结束事件循环的执行
        loop_->quit();
        /// 回收IO线程
        thread_.join();
    }
}

/// （1）创建并启动IO线程（IO线程：创建了EventLoop对象的线程）
/// 同时，使IO线程开始执行事件循环（即：执行poll函数，监测pollfds_表（相当于epoll的内核事件表）中的文件描述符上的各种事件，是否发生）
/// （2）相当于pop操作 -- 消费者执行的操作：
/// 执行EventLoopThread::startLoop()函数的线程，就是消费者线程
/// 从多线程共享的EventLoop对象地址缓冲区loop_中，取得void EventLoopThread::threadFunc()创建好的多线程共享的EventLoop对象
EventLoop *EventLoopThread::startLoop()
{
    assert(!thread_.started());
    /// 创建并启动IO线程（IO线程：创建了EventLoop对象的线程），
    /// void EventLoopThread::threadFunc()函数，就是IO线程需要执行的代码
    thread_.start();

    {
        MutexLockGuard lock(mutex_);
        while (loop_ == NULL)
        {
            /// 等待void EventLoopThread::threadFunc()函数，将EventLoop对象创建好
            cond_.wait();
        }
    }
    /// 从多线程共享的EventLoop对象地址缓冲区loop_中，
    /// 取得void EventLoopThread::threadFunc()创建好的多线程共享的EventLoop对象
    return loop_;
}

/// （1）IO线程需要执行的代码，就是这个函数的代码：
/// 创建EventLoop对象，
/// 并开始执行事件循环（即：执行poll函数，监测pollfds_表（相当于epoll的内核事件表）中的文件描述符上的各种事件，是否发生）
/// （2）相当于push操作 -- 生产者执行的操作：
/// 执行EventLoopThread::threadFunc()函数的线程，就是生产者线程
/// 向多线程共享的EventLoop对象地址缓冲区loop_中，放入void EventLoopThread::threadFunc()创建好的多线程共享的EventLoop对象
void EventLoopThread::threadFunc()
{
    /// （1）创建EventLoop对象（IO线程：创建了EventLoop对象的线程）
    /// 即：执行void EventLoopThread::threadFunc()函数代码的线程，是IO线程
    /// （2）创建的是：EventLoop* EventLoopThread::startLoop()函数，一直等待获得的EventLoop对象
    EventLoop loop;

    if (callback_)
    {
        callback_(&loop);
    }

    {
        MutexLockGuard lock(mutex_);
        /// 向多线程共享的EventLoop对象地址缓冲区loop_中，放入创建好的多线程共享的EventLoop对象
        loop_ = &loop;
        /// 通知EventLoop* EventLoopThread::startLoop()函数，它要的EventLoop对象已经创建好了
        cond_.notify();
    }

    /// 执行事件循环（即：执行poll函数，监测pollfds_表（相当于epoll的内核事件表）中的文件描述符上的各种事件，是否发生）
    loop.loop();
    ///assert(exiting_);
    loop_ = NULL;
}

