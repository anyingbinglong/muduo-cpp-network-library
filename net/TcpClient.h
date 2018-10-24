// Copyright 2010, Shuo Chen.  All rights reserved.
// http://code.google.com/p/muduo/
//
// Use of this source code is governed by a BSD-style license
// that can be found in the License file.

// Author: Shuo Chen (chenshuo at chenshuo dot com)
//
// This is a public header file, it must only include public header files.

#ifndef MUDUO_NET_TCPCLIENT_H
#define MUDUO_NET_TCPCLIENT_H

#include <boost/noncopyable.hpp>

#include <muduo/base/Mutex.h>
#include <muduo/net/TcpConnection.h>

namespace muduo
{
    namespace net
    {

        class Connector;
        typedef boost::shared_ptr<Connector> ConnectorPtr;

        class TcpClient : boost::noncopyable
        {
        public:
            // TcpClient(EventLoop* loop);
            // TcpClient(EventLoop* loop, const string& host, uint16_t port);
            TcpClient(EventLoop *loop,
                      const InetAddress &serverAddr,
                      const string &nameArg);
            ~TcpClient();  // force out-line dtor, for scoped_ptr members.
            /// 客户端进程主动开始，与服务端进程建立连接
            void connect();
            /// 客户端进程主动开始，与服务端进程断开连接
            void disconnect();

            // 定时器timerId_的作用：
            // 以系统当前时间为起点，经过retryDelayMs_/1000.0秒后，定时器超时，
            // 执行定时器回调函数Connector::startInLoop，使客户端进程，再次主动与服务端进程建立连接
            // retryDelayMs_记录：客户端进程重新创建一个新的socket，再次主动与服务端进程建立连接时，
            //                    重试的间隔时间的，起始时间
            // =====================================================================================
            // 注销定时器timerId_：
            // 实现，客户端进程，不再尝试主动与服务端进程建立连接
            void stop();

            TcpConnectionPtr connection() const
            {
                MutexLockGuard lock(mutex_);
                return connection_;
            }

            EventLoop *getLoop() const
            {
                return loop_;
            }
            bool retry() const
            {
                return retry_;
            }
            void enableRetry()
            {
                retry_ = true;
            }

            const string &name() const
            {
                return name_;
            }

            /// Set connection callback.
            /// Not thread safe.
            void setConnectionCallback(const ConnectionCallback &cb)
            {
                connectionCallback_ = cb;
            }

            /// Set message callback.
            /// Not thread safe.
            void setMessageCallback(const MessageCallback &cb)
            {
                messageCallback_ = cb;
            }

            /// Set write complete callback.
            /// Not thread safe.
            void setWriteCompleteCallback(const WriteCompleteCallback &cb)
            {
                writeCompleteCallback_ = cb;
            }

#ifdef __GXX_EXPERIMENTAL_CXX0X__
            void setConnectionCallback(ConnectionCallback &&cb)
            {
                connectionCallback_ = std::move(cb);
            }
            void setMessageCallback(MessageCallback &&cb)
            {
                messageCallback_ = std::move(cb);
            }
            void setWriteCompleteCallback(WriteCompleteCallback &&cb)
            {
                writeCompleteCallback_ = std::move(cb);
            }
#endif

        private:
            /// Not thread safe, but in loop
            /// ===============================================================================================================
            /// 函数的功能：客户端进程，对服务端进程发来的新的连接请求的处理，具体处理的内容如下
            /// （1）创建：服务端进程与客户端进程，所建立的连接的名字
            /// （2）获取sockfd对应的本端socket地址（IP地址 + 端口号）
            /// （3）创建TCP连接管理对象conn，管理服务端进程与客户端进程新建立的连接
            /// （4）将connName，此连接的信息保存到connections_中
            /// （5）设置连接回调函数
            ///      连接回调函数，的作用：
            ///      客户端进程，执行void connectEstablished()函数，使得客户端进程与服务端进程，真正建立起连接后，
            ///      客户进程，会执行，该连接回调函数connectionCallback_，通知服务端进程，连接建立成功
            /// （6）设置消息回调函数
            ///      消息回调函数，的作用：
            ///      客户端进程，读取，接收到的服务端进程发来的数据后，
            ///      客户端进程，会在这个函数中，处理，接收到的服务端进程发来的数据
            /// （7）使客户端进程与服务端进程，真正建立起连接：
            ///      在channel_（conn对象的成员变量）管理的socket文件描述符上注册读事件，并在pollfds_表（相当于epoll的内核事件表）中新增一个表项
            ///      实现：服务端进程，使用poll函数，监测channel_（conn对象的成员变量）管理的socket文件描述符上是否有读事件发生
            ///      读事件：客户端进程，接收到服务端进程发来的数据
            ///      并执行，连接回调函数connectionCallback_（conn对象的成员变量），通知服务端进程，连接建立成功
            /// ===============================================================================================================
            /// 函数参数的含义：
            /// 客户端进程创建的套接字sockfd。
            /// 客户端进程调用connect函数，来使该套接字sockfd
            /// 与服务端进程（客户端进程，要连接的服务端进程的socket地址（IP地址 + 端口号））的套接字进行连接
            /// peeraddr，存放客户端进程，要连接的服务端进程的socket地址（IP地址 + 端口号）
            /// sockfd，存放客户端进程的socket地址（IP地址 + 端口号）
            /// ===============================================================================================================
            /// 函数在哪里被使用：
            /// TcpClient::newConnection函数，在Connector.cc的void Connector::handleWrite()函数中被调用
            void newConnection(int sockfd);
            
            /// Not thread safe, but in loop
            // 函数参数含义：
            // const TcpConnectionPtr &conn：TCP连接管理对象，其中保存了服务端进程和客户端进程建立的连接信息
            // 函数功能：
            // 关闭（销毁）服务端和客户端建立的连接，具体处理的内容如下
            // （1）connection_记录：服务端进程和客户端进程建立的连接信息：
            //      连接的名字
            //      TCP连接管理对象TcpConnection
            //  从connections_中删除，conn->name()这条连接信息
            // （2）服务端进程，执行此函数：彻底断开客户端与服务端建立的TCP连接
            void removeConnection(const TcpConnectionPtr &conn);

            // 记录：TcpClient自己的EventLoop对象的地址
            EventLoop *loop_;

            // 客户端进程：TcpClient类，使用Connector类提供的功能，处理与服务端进程的通信过程
            ConnectorPtr connector_; // avoid revealing Connector

            // 存放服务端进程与客户端进程，所建立的连接的名字
            const string name_;
            /// 连接回调函数，的作用：
            /// 客户端进程，执行void connectEstablished()函数，使得客户端进程与服务端进程，真正建立起连接后，
            /// 客户进程，会执行，该连接回调函数connectionCallback_，通知服务端进程，连接建立成功
            ConnectionCallback connectionCallback_;

            /// 消息回调函数，的作用：
            /// 客户端进程，读取，接收到的服务端进程发来的数据后，
            /// 客户端进程，会在这个函数中，处理，接收到的服务端进程发来的数据
            MessageCallback messageCallback_;

            // 写完成回调函数的作用：
            // 1.）输出缓冲区outputBuffer_的作用：
            //     (1)服务端，将需要发送给客户端的数据，存放到这里，然后发送给客户端
            //     (2)客户端，将需要发送给服务端的数据，存放到这里，然后发送给服务端
            // 2.）输出缓冲区outputBuffer_中的数据，发送完毕后，会调用这个回调函数，提示数据发送完成
            WriteCompleteCallback writeCompleteCallback_;

            // 记录：客户端进程，是否需要，重新创建一个新的socket，再次主动与服务端进程建立连接
            bool retry_;   // atomic
            // 记录：客户端进程与其要连接的服务端进程，是否需要建立连接
            bool connect_; // atomic
            // always in loop thread
            int nextConnId_;

            // 互斥锁：保护下面的connection_
            mutable MutexLock mutex_;

            // class TcpConnection这个类的作用：
            // （1）管理客户端和服务端之间，建立的，TCP连接
            // （2）这个类所创建的一个对象，就是一个，TCP连接管理对象，这个对象中，保存着这个TCP连接的相关信息
            // 多线程共享资源
            // 记录：服务端进程和客户端进程建立的连接信息：
            // （1）连接的名字
            // （2）TCP连接管理对象TcpConnection
            TcpConnectionPtr connection_; // @GuardedBy mutex_
        };
    }
}

#endif  // MUDUO_NET_TCPCLIENT_H
