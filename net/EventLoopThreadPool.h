// Copyright 2010, Shuo Chen.  All rights reserved.
// http://code.google.com/p/muduo/
//
// Use of this source code is governed by a BSD-style license
// that can be found in the License file.

// Author: Shuo Chen (chenshuo at chenshuo dot com)
//
// This is an internal header file, you should not include this.

#ifndef MUDUO_NET_EVENTLOOPTHREADPOOL_H
#define MUDUO_NET_EVENTLOOPTHREADPOOL_H

#include <muduo/base/Types.h>

#include <vector>
#include <boost/function.hpp>
#include <boost/noncopyable.hpp>
#include <boost/ptr_container/ptr_vector.hpp>

namespace muduo
{
    namespace net
    {
        class EventLoop;
        class EventLoopThread;

        class EventLoopThreadPool : boost::noncopyable
        {
        public:
            typedef boost::function<void(EventLoop *)> ThreadInitCallback;

            EventLoopThreadPool(EventLoop *baseLoop, const string &nameArg);
            ~EventLoopThreadPool();

            // 设置：事件循环线程池中的线程的个数
            void setThreadNum(int numThreads)
            {
                numThreads_ = numThreads;
            }
            // 创建事件循环线程（IO线程）池中的线程
            // 并将多线程共享的EventLoop对象，放入到EventLoop对象缓冲区loops_中
            void start(const ThreadInitCallback &cb = ThreadInitCallback());

            // valid after calling start()
            /// round-robin
            // 从EventLoop对象缓冲区loops_中，获取一个EventLoop对象
            EventLoop *getNextLoop();

            /// with the same hash code, it will always return the same EventLoop
            EventLoop *getLoopForHash(size_t hashCode);

            std::vector<EventLoop *> getAllLoops();

            bool started() const
            {
                return started_;
            }

            const string &name() const
            {
                return name_;
            }

        private:

            // 记录：默认的EventLoop对象的地址
            EventLoop *baseLoop_;
            string name_;

            // 记录：事件循环线程池，是否已经创建
            bool started_;

            // 记录：事件循环线程池中的线程的个数
            int numThreads_;

            // 记录：EventLoop对象缓冲区loops_中，能被获取的EventLoop对象的位置
            int next_;

            /// class EventLoopThread这个类的作用：对事件循环线程（IO线程：创建了EventLoop对象的线程），进行管理
            /// （1）这个类创建的对象，就是事件循环线程管理对象
            /// （2）一个事件循环线程管理对象，就管理（代表）一个事件循环线程（IO线程）
            // 事件循环线程池：由事件循环线程（IO线程：创建了EventLoop对象的线程）构成
            boost::ptr_vector<EventLoopThread> threads_;

            // EventLoop对象缓冲区
            std::vector<EventLoop *> loops_;
        };
    }
}

#endif  // MUDUO_NET_EVENTLOOPTHREADPOOL_H
