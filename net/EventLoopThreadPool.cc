// Copyright 2010, Shuo Chen.  All rights reserved.
// http://code.google.com/p/muduo/
//
// Use of this source code is governed by a BSD-style license
// that can be found in the License file.

// Author: Shuo Chen (chenshuo at chenshuo dot com)

#include <muduo/net/EventLoopThreadPool.h>

#include <muduo/net/EventLoop.h>
#include <muduo/net/EventLoopThread.h>

#include <boost/bind.hpp>

#include <stdio.h>

using namespace muduo;
using namespace muduo::net;


EventLoopThreadPool::EventLoopThreadPool(EventLoop *baseLoop, const string &nameArg)
    : baseLoop_(baseLoop),// 创建默认的EventLoop对象
      name_(nameArg),
      started_(false),// 记录：事件循环线程池，没有创建
      numThreads_(0),
      next_(0)
{
}

EventLoopThreadPool::~EventLoopThreadPool()
{
    // Don't delete loop, it's stack variable
}

// 创建事件循环线程（IO线程）池中的线程
// 并将多线程共享的EventLoop对象，放入到EventLoop对象缓冲区loops_中
void EventLoopThreadPool::start(const ThreadInitCallback &cb)
{
    assert(!started_);
    // 确保：执行事件循环（EventLoop::loop()）的线程，是IO线程
    // 即：确保，执行void EventLoopThreadPool::start()函数的线程，是IO线程
    baseLoop_->assertInLoopThread();

    // 记录：事件循环线程池，已经创建
    started_ = true;
    // 创建事件循环线程池中的线程
    // 并将将多线程共享的EventLoop对象，放入到EventLoop对象缓冲区loops_中
    for (int i = 0; i < numThreads_; ++i)
    {
        char buf[name_.size() + 32];
        snprintf(buf, sizeof buf, "%s%d", name_.c_str(), i);
        /// class EventLoopThread这个类的作用：对事件循环线程（IO线程：创建了EventLoop对象的线程），进行管理
        /// （1）这个类创建的对象，就是事件循环线程管理对象
        /// （2）一个事件循环线程管理对象，就管理（代表）一个事件循环线程（IO线程）
        EventLoopThread *t = new EventLoopThread(cb, buf);

        // boost::ptr_vector<EventLoopThread> threads_事件循环线程池：由事件循环线程（IO线程：创建了EventLoop对象的线程）构成
        // 将事件循环线程（IO线程）对象，放入到事件循环线程（IO线程）池（线程对象池）中
        threads_.push_back(t);

        // t->startLoop()，这句话的含义：
        // （1）创建并启动IO线程（IO线程：创建了EventLoop对象的线程）
        // 同时，使IO线程开始执行事件循环（即：执行poll函数，监测pollfds_表（相当于epoll的内核事件表）中的文件描述符上的各种事件，是否发生）
        // （2）相当于pop操作 -- 消费者执行的操作：
        // 执行EventLoopThread::startLoop()函数的线程，就是消费者线程
        // 从多线程共享的EventLoop对象地址缓冲区loop_中，取得void EventLoopThread::threadFunc()创建好的多线程共享的EventLoop对象
        // loops_.push_back()，这句话的含义：
        // 将多线程共享的EventLoop对象，放入到EventLoop对象缓冲区loops_中
        // （3）执行EventLoopThread::startLoop()函数的线程，就是消费者线程，
        // 也就是执行void EventLoopThreadPool::start()函数的线程，也就是主线程
        loops_.push_back(t->startLoop());
    }
    if (numThreads_ == 0 && cb)
    {
        cb(baseLoop_);
    }
}

// 从EventLoop对象缓冲区loops_中，获取一个EventLoop对象
EventLoop *EventLoopThreadPool::getNextLoop()
{
    baseLoop_->assertInLoopThread();
    assert(started_);
    // 使用默认的EventLoop对象
    EventLoop *loop = baseLoop_;
    // EventLoop对象缓冲区loops_，不为空
    if (!loops_.empty())
    {
        // round-robin
        // 从EventLoop对象缓冲区loops_中，获取一个EventLoop对象
        loop = loops_[next_];
        // 移动，EventLoop对象缓冲区loops_中，能被获取的EventLoop对象的位置
        ++next_;
        if (implicit_cast<size_t>(next_) >= loops_.size())
        {
            next_ = 0;
        }
    }
    return loop;
}

EventLoop *EventLoopThreadPool::getLoopForHash(size_t hashCode)
{
    baseLoop_->assertInLoopThread();
    EventLoop *loop = baseLoop_;

    if (!loops_.empty())
    {
        loop = loops_[hashCode % loops_.size()];
    }
    return loop;
}

std::vector<EventLoop *> EventLoopThreadPool::getAllLoops()
{
    baseLoop_->assertInLoopThread();
    assert(started_);
    if (loops_.empty())
    {
        return std::vector<EventLoop *>(1, baseLoop_);
    }
    else
    {
        return loops_;
    }
}