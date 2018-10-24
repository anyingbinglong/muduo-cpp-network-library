/// Copyright 2010, Shuo Chen.  All rights reserved.
/// http:///code.google.com/p/muduo/
///
/// Use of this source code is governed by a BSD-style license
/// that can be found in the License file.

/// Author: Shuo Chen (chenshuo at chenshuo dot com)
///
/// This is a public header file, it must only include public header files.

#ifndef MUDUO_NET_EVENTLOOPTHREAD_H
#define MUDUO_NET_EVENTLOOPTHREAD_H

#include <muduo/base/Condition.h>
#include <muduo/base/Mutex.h>
#include <muduo/base/Thread.h>

#include <boost/noncopyable.hpp>

namespace muduo
{
    namespace net
    {

        class EventLoop;

        /// 这个类的作用：对事件循环线程（IO线程：创建了EventLoop对象的线程），进行管理
        /// （1）这个类创建的对象，就是事件循环线程管理对象
        /// （2）一个事件循环线程管理对象，就管理（代表）一个事件循环线程（IO线程）
        class EventLoopThread : boost::noncopyable
        {
        public:
            typedef boost::function<void(EventLoop *)> ThreadInitCallback;

            EventLoopThread(const ThreadInitCallback &cb = ThreadInitCallback(),
                            const string &name = string());
            ~EventLoopThread();
            /// （1）创建并启动IO线程（IO线程：创建了EventLoop对象的线程）
            /// 同时，使IO线程开始执行事件循环（即：执行poll函数，监测pollfds_表（相当于epoll的内核事件表）中的文件描述符上的各种事件，是否发生）
            /// （2）相当于pop操作 -- 消费者执行的操作：
            /// 执行EventLoopThread::startLoop()函数的线程，就是消费者线程
            /// 从多线程共享的EventLoop对象地址缓冲区loop_中，取得void EventLoopThread::threadFunc()创建好的多线程共享的EventLoop对象
            EventLoop *startLoop();

        private:
            /// （1）IO线程需要执行的代码，就是这个函数的代码：
            /// 创建EventLoop对象，
            /// 并开始执行事件循环（即：执行poll函数，监测pollfds_表（相当于epoll的内核事件表）中的文件描述符上的各种事件，是否发生）
            /// （2）相当于push操作 -- 生产者执行的操作：
            /// 执行EventLoopThread::threadFunc()函数的线程，就是生产者线程
            /// 向多线程共享的EventLoop对象地址缓冲区loop_中，放入void EventLoopThread::threadFunc()创建好的多线程共享的EventLoop对象
            void threadFunc();

            /// 多线程共享的EventLoop对象地址缓冲区：记录多线程共享的EventLoop对象的地址
            /// 多线程共享资源
            EventLoop *loop_;

            /// 记录：IO线程，是否正在退出
            bool exiting_;

            /// 线程对象：用于管理（创建，回收...）线程
            Thread thread_;

            /// 使用互斥锁和条件变量，实现：
            /// 执行EventLoop* startLoop();函数的线程 -- 消费者线程
            /// 和
            /// 执行void threadFunc();函数的线程 -- 生产者线程
            /// 这2种线程，同步与互斥地，访问（操作），多线程共享的EventLoop对象地址缓冲区EventLoop* loop_;
            MutexLock mutex_;/// 互斥锁：入口等待队列
            Condition cond_;/// 条件变量：条件变量等待队列
            ThreadInitCallback callback_;
        };

    }
}

#endif  /// MUDUO_NET_EVENTLOOPTHREAD_H

