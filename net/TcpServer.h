// Copyright 2010, Shuo Chen.  All rights reserved.
// http://code.google.com/p/muduo/
//
// Use of this source code is governed by a BSD-style license
// that can be found in the License file.

// Author: Shuo Chen (chenshuo at chenshuo dot com)
//
// This is a public header file, it must only include public header files.

#ifndef MUDUO_NET_TCPSERVER_H
#define MUDUO_NET_TCPSERVER_H

#include <muduo/base/Atomic.h>
#include <muduo/base/Types.h>
#include <muduo/net/TcpConnection.h>

#include <map>
#include <boost/noncopyable.hpp>
#include <boost/scoped_ptr.hpp>
#include <boost/shared_ptr.hpp>

namespace muduo
{
    namespace net
    {

        class Acceptor;
        class EventLoop;
        class EventLoopThreadPool;

        ///
        /// TCP server, supports single-threaded and thread-pool models.
        ///
        /// This is an interface class, so don't expose too much details.
        /// 这个类的作用：供服务端程序使用
        class TcpServer : boost::noncopyable
        {
        public:
            typedef boost::function<void(EventLoop *)> ThreadInitCallback;
            enum Option
            {
                kNoReusePort,
                kReusePort,
            };

            //TcpServer(EventLoop* loop, const InetAddress& listenAddr);
            TcpServer(EventLoop *loop,
                      const InetAddress &listenAddr,
                      const string &nameArg,
                      Option option = kNoReusePort);
            ~TcpServer();  // force out-line dtor, for scoped_ptr members.

            const string &ipPort() const
            {
                return ipPort_;
            }
            const string &name() const
            {
                return name_;
            }
            EventLoop *getLoop() const
            {
                return loop_;
            }

            /// Set the number of threads for handling input.
            ///
            /// Always accepts new connection in loop's thread.
            /// Must be called before @c start
            /// @param numThreads
            /// - 0 means all I/O in loop's thread, no thread will created.
            ///   this is the default value.
            /// - 1 means all I/O in another thread.
            /// - N means a thread pool with N threads, new connections
            ///   are assigned on a round-robin basis.
            // 设置，事件循环线程池class EventLoopThreadPool中，线程的个数
            void setThreadNum(int numThreads);
            void setThreadInitCallback(const ThreadInitCallback &cb)
            {
                threadInitCallback_ = cb;
            }
            /// valid after calling start()
            boost::shared_ptr<EventLoopThreadPool> threadPool()
            {
                return threadPool_;
            }

            /// Starts the server if it's not listenning.
            ///
            /// It's harmless to call it multiple times.
            /// Thread safe.
            /// 启动服务端进程：
            /// 在acceptSocket_（acceptor_对象的成员变量）上注册读事件，并在pollfds_表（相当于epoll的内核事件表）中新增一个表项
            /// 实现：服务端进程，使用poll函数，监测acceptSocket_（acceptor_对象的成员变量）上是否有读事件发生
            /// 读事件：服务端进程，接收到客户端进程发来的数据
            void start();

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

        private:
            /// Not thread safe, but in loop
            /// ===============================================================================================================
            /// 函数参数的含义：
            /// 服务端进程，调用accept函数从处于监听状态的套接字的客户端进程连接请求队列中取出排在最前面的一个客户连接请求，
            /// 并且服务端进程，会创建一个新的套接字，来与客户端进程的套接字，创建连接通道
            /// peeraddr，存放服务端进程，获取到的客户端的socket地址（IP地址和端口号）
            /// sockfd，存放服务端进程新创建的套接字的socket文件描述符
            /// ===============================================================================================================
            /// 函数的功能：服务端进程，对客户端进程发来的新的连接请求的处理，具体处理的内容如下
            /// （1）创建：服务端进程与客户端进程，所建立的连接的名字
            /// （2）为服务端进程，分配socket地址（IP地址和端口号）
            /// （3）创建TCP连接管理对象conn，管理服务端进程与客户端进程新建立的连接
            /// （4）将connName，此连接的信息保存到connections_中
            /// （5）设置连接回调函数
            ///      连接回调函数，的作用：
            ///      服务端进程，执行void connectEstablished()函数，使得客户端进程与服务端进程，真正建立起连接后，
            ///      服务端进程，会执行，该连接回调函数connectionCallback_，通知客户端进程，连接建立成功
            /// （6）设置消息回调函数
            ///      消息回调函数，的作用：
            ///      服务端进程，读取，接收到的客户端进程发来的数据后，
            ///      服务端进程，会在这个函数中，处理，接收到的客户端进程发来的数据
            /// （7）使客户端进程与服务端进程，真正建立起连接：
            ///      在channel_（conn对象的成员变量）管理的socket文件描述符上注册读事件，并在pollfds_表（相当于epoll的内核事件表）中新增一个表项
            ///      实现：服务端进程，使用poll函数，监测channel_（conn对象的成员变量）管理的socket文件描述符上是否有读事件发生
            ///      读事件：服务端进程，接收到客户端进程发来的数据
            ///      并执行，连接回调函数connectionCallback_（conn对象的成员变量），通知客户端进程，连接建立成功
            void newConnection(int sockfd, const InetAddress &peerAddr);

            /// Thread safe.
            // 函数参数含义：
            // const TcpConnectionPtr &conn：TCP连接管理对象，其中保存了服务端进程和客户端进程建立的连接信息
            // 函数功能：
            // 关闭（销毁）服务端和客户端建立的连接，具体处理的内容如下
            // （1）connections_记录：服务端进程和客户端进程建立的连接信息：
            //      连接的名字
            //      TCP连接管理对象TcpConnection
            //  从connections_中删除，conn->name()这条连接信息
            // （2）服务端进程，执行此函数：彻底断开客户端与服务端建立的TCP连接
            void removeConnection(const TcpConnectionPtr &conn);
            /// Not thread safe, but in loop
            // 函数参数含义：
            // const TcpConnectionPtr &conn：TCP连接管理对象，其中保存了服务端进程和客户端进程建立的连接信息
            // 函数功能：
            // 关闭（销毁）服务端和客户端建立的连接，具体处理的内容如下
            // （1）connections_记录：服务端进程和客户端进程建立的连接信息：
            //      连接的名字
            //      TCP连接管理对象TcpConnection
            //  从connections_中删除，conn->name()这条连接信息
            // （2）服务端进程，执行此函数：彻底断开客户端与服务端建立的TCP连接
            void removeConnectionInLoop(const TcpConnectionPtr &conn);

            // class TcpConnection这个类的作用：
            // （1）管理客户端和服务端之间，建立的，TCP连接
            // （2）这个类所创建的一个对象，就是一个，TCP连接管理对象，这个对象中，保存着这个TCP连接的相关信息
            typedef std::map<string, TcpConnectionPtr> ConnectionMap;

            // 记录：TcpServer自己的EventLoop对象的地址
            EventLoop *loop_;  // the acceptor loop
            const string ipPort_;

            // 存放服务端进程与客户端进程，所建立的连接的名字
            const string name_;

            /// class Acceptor这个类的作用：
            /// （1）服务端进程，调用accept函数从处于监听状态的套接字acceptSocket_(其内部成员变量：sockfd_)的客户端进程连接请求队列中，
            /// 取出排在最前面的一个客户连接请求，
            /// 并且服务端进程，会创建一个新的套接字，来与客户端进程的套接字，创建连接通道
            /// （2）class Acceptor这个类，是内部类，供class TcpServer这个类使用
            // 用于accept新TCP连接，并通过回调通知使用者，
            // 即：服务端进程，调用accept函数从处于监听状态的套接字sockfd的客户端进程连接请求队列中取出排在最前面的一个客户连接请求，
            // 并且服务端进程，会创建一个新的套接字，来与客户端进程的套接字，创建连接通道
            boost::scoped_ptr<Acceptor> acceptor_; // avoid revealing Acceptor

            // 事件循环线程池
            boost::shared_ptr<EventLoopThreadPool> threadPool_;

            // 连接回调函数，的作用：
            // 服务端进程，执行void connectEstablished()函数，使得客户端进程与服务端进程，真正建立起连接后，
            // 服务端进程，会执行，该连接回调函数connectionCallback_，通知客户端进程，连接建立成功
            ConnectionCallback connectionCallback_;

            // 消息回调函数，的作用：
            // 服务端进程，读取，接收到的客户端进程发来的数据后，
            // 服务端进程，会在这个函数中，处理，接收到的客户端进程发来的数据
            MessageCallback messageCallback_;

            // 写完成回调函数的作用：
            // 1.）输出缓冲区outputBuffer_的作用：
            //     (1)服务端，将需要发送给客户端的数据，存放到这里，然后发送给客户端
            //     (2)客户端，将需要发送给服务端的数据，存放到这里，然后发送给服务端
            // 2.）输出缓冲区outputBuffer_中的数据，发送完毕后，会调用这个回调函数，提示数据发送完成
            WriteCompleteCallback writeCompleteCallback_;
            ThreadInitCallback threadInitCallback_;

            // 记录：服务端进程，是否已经启动
            AtomicInt32 started_;
            // always in loop thread
            int nextConnId_;

            // 记录：服务端进程和客户端进程建立的连接信息：
            // （1）连接的名字
            // （2）TCP连接管理对象TcpConnection
            ConnectionMap connections_;
        };
    }
}

#endif  // MUDUO_NET_TCPSERVER_H
