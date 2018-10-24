// Copyright 2010, Shuo Chen.  All rights reserved.
// http://code.google.com/p/muduo/
//
// Use of this source code is governed by a BSD-style license
// that can be found in the License file.

// Author: Shuo Chen (chenshuo at chenshuo dot com)
//
// This is an internal header file, you should not include this.

#ifndef MUDUO_NET_ACCEPTOR_H
#define MUDUO_NET_ACCEPTOR_H

#include <boost/function.hpp>
#include <boost/noncopyable.hpp>

#include <muduo/net/Channel.h>
#include <muduo/net/Socket.h>

namespace muduo
{
    namespace net
    {

        class EventLoop;
        class InetAddress;

        ///
        /// Acceptor of incoming TCP connections.
        /// 这个类的作用：
        /// （1）服务端进程，调用accept函数从处于监听状态的套接字acceptSocket_(其内部成员变量：sockfd_)的客户端进程连接请求队列中，
        /// 取出排在最前面的一个客户连接请求，
        /// 并且服务端进程，会创建一个新的套接字，来与客户端进程的套接字，创建连接通道
        /// （2）class Acceptor这个类，是内部类，供class TcpServer这个类使用
        class Acceptor : boost::noncopyable
        {
        public:
            typedef boost::function<void (int sockfd,
                                          const InetAddress &)> NewConnectionCallback;

            Acceptor(EventLoop *loop, const InetAddress &listenAddr, bool reuseport);
            ~Acceptor();

            // 新连接回调函数，的作用：
            // 在该函数内部，实现服务端进程，对客户端进程发来的新的连接请求的处理
            // 设置：新连接回调函数
            void setNewConnectionCallback(const NewConnectionCallback &cb)
            {
                newConnectionCallback_ = cb;
            }

            // 获取：套接字acceptSocket_(其内部成员变量：sockfd_)，是否处于监听状态
            bool listenning() const
            {
                return listenning_;
            }

            // 服务端进程，开始监听服务端socket -- acceptSocket_
            void listen();

        private:
            // 套接字acceptSocket_(其内部成员变量：sockfd_)，上有可读事件发生时，
            // 读事件的事件处理函数为Acceptor::handleRead
            // 读事件：服务端进程，接收到客户端进程发来的新的连接请求
            //         服务端进程，执行此函数对客户端进程发来的新的连接请求进行处理
            // 该函数，由服务端进程执行
            void handleRead();

            EventLoop *loop_;

            // 该对象内部存放的是：服务端进程，要监听的套接字acceptSocket_(其内部成员变量：sockfd_)
            Socket acceptSocket_;

            /// class Channel类的作用：
            /// （1）channel：通道，大管子，文件描述符（文件描述符：就是插在文件上的大管子）
            /// （2）记录：实际发生的事件的表的一个表项的类型（内容）
            /// （3）用一个class Channel类，来管理一个文件描述符
            /// acceptChannel_的作用：
            /// 对套接字acceptSocket_，其内部的成员变量：sockfd_ --> socket文件描述符，进行管理：
            /// 观察套接字acceptSocket_(其内部成员变量：sockfd_)，上是否有可读事件发生
            /// 读事件：服务端进程，接收到客户端进程发来的数据
            Channel acceptChannel_;

            // 新连接回调函数，的作用：
            // 在该函数内部，实现服务端进程，对客户端进程发来的新的连接请求的处理
            NewConnectionCallback newConnectionCallback_;

            // 记录：套接字acceptSocket_(其内部成员变量：sockfd_)，是否处于监听状态
            bool listenning_;
            int idleFd_;
        };
    }
}

#endif  // MUDUO_NET_ACCEPTOR_H
