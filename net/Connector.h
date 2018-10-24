// Copyright 2010, Shuo Chen.  All rights reserved.
// http://code.google.com/p/muduo/
//
// Use of this source code is governed by a BSD-style license
// that can be found in the License file.

// Author: Shuo Chen (chenshuo at chenshuo dot com)
//
// This is an internal header file, you should not include this.

#ifndef MUDUO_NET_CONNECTOR_H
#define MUDUO_NET_CONNECTOR_H

#include <muduo/net/InetAddress.h>

#include <boost/enable_shared_from_this.hpp>
#include <boost/function.hpp>
#include <boost/noncopyable.hpp>
#include <boost/scoped_ptr.hpp>

namespace muduo
{
    namespace net
    {
        class Channel;
        class EventLoop;

        class Connector : boost::noncopyable,
            public boost::enable_shared_from_this<Connector>
        {
        public:
            typedef boost::function<void (int sockfd)> NewConnectionCallback;

            Connector(EventLoop *loop, const InetAddress &serverAddr);
            ~Connector();

            // 设置新连接回调函数
            void setNewConnectionCallback(const NewConnectionCallback &cb)
            {
                newConnectionCallback_ = cb;
            }

            /// 客户端进程主动开始，与服务端进程建立连接：
            /// 客户端进程调用connect函数，来使客户端进程的套接字sockfd
            /// 与服务端进程serverAddr_（客户端进程，要连接的服务端进程的socket地址（IP地址 + 端口号））的套接字进行连接
            void start();  // can be called in any thread

            // 使客户端进程，重新与其要连接的服务端进程，建立连接
            void restart();  // must be called in loop thread

            // 设置客户端和服务端之间的连接状态为--kDisconnected：服务端和客户端之间的TCP连接已关闭
            // ===================================================================================================
            // （1）不再关注：channel_所管理的文件描述符上，所发生的任何事件
            // （2）从pollfds_表（相当于epoll的内核事件表）中删除表项channel_
            // 实现：客户端进程，在使用poll函数时，不再监测channel_所管理的socket文件描述符上，是否有任何事件发生
            // （3） 清空channel_中保存的，Channel对象指针变量的值
            // 即：channel不再管理任何Channel对象，
            // 也就是，不再管理任何客户端进程创建的socket文件描述符
            // ====================================================================================================
            // 客户端进程重新创建一个新的socket，再次主动与服务端进程建立连接：
            //（1）新创建一个定时器timerId_：
            // 以系统当前时间为起点，经过retryDelayMs_/1000.0秒后，定时器超时，
            // 执行定时器回调函数Connector::startInLoop，使客户端进程，再次主动与服务端进程建立连接
            // retryDelayMs_记录：客户端进程重新创建一个新的socket，再次主动与服务端进程建立连接时，
            //                    重试的间隔时间的，起始时间
            //（2）客户端进程重新创建一个新的socket，再次主动与服务端进程建立连接时，
            // 重试的间隔时间，按照2倍的方式逐渐延长，即：0.5s, 1s, 2s, 4s, ......，直至kMaxRetryDelayMs（30s）
            void stop();  // can be called in any thread

            // 设置：客户端进程，要连接的服务端进程的socket地址serverAddr_（IP地址 + 端口号）
            const InetAddress &serverAddress() const
            {
                return serverAddr_;
            }

        private:
            // 标识：服务端进程与客户端进程，所建立的连接的连接状态
            // kDisconnected：服务端和客户端之间的TCP连接已关闭
            // kConnecting：正在建立服务端和客户端之间的TCP连接
            // kConnected：服务端和客户端之间的TCP连接已建立
            enum States { kDisconnected, kConnecting, kConnected };

            // 客户端进程重新创建一个新的socket，再次主动与服务端进程建立连接时，
            // 重试的最大间隔时间
            static const int kMaxRetryDelayMs = 30 * 1000;

            // retryDelayMs_成员变量的初始值
            // retryDelayMs_的作用：客户端进程重新创建一个新的socket，再次主动与服务端进程建立连接时，
            //                      重试的间隔时间的，起始时间
            static const int kInitRetryDelayMs = 500;

            // 设置：服务端进程与客户端进程，所建立的连接的连接状态
            void setState(States s)
            {
                state_ = s;
            }

            /// 客户端进程调用connect函数，来使客户端进程的套接字sockfd
            /// 与服务端进程serverAddr_（客户端进程，要连接的服务端进程的socket地址（IP地址 + 端口号））的套接字进行连接
            void startInLoop();

            // 设置客户端和服务端之间的连接状态为--kDisconnected：服务端和客户端之间的TCP连接已关闭
            // ===================================================================================================
            // （1）不再关注：channel_所管理的文件描述符上，所发生的任何事件
            // （2）从pollfds_表（相当于epoll的内核事件表）中删除表项channel_
            // 实现：客户端进程，在使用poll函数时，不再监测channel_所管理的socket文件描述符上，是否有任何事件发生
            // （3） 清空channel_中保存的，Channel对象指针变量的值
            // 即：channel不再管理任何Channel对象，
            // 也就是，不再管理任何客户端进程创建的socket文件描述符
            // ====================================================================================================
            // 客户端进程重新创建一个新的socket，再次主动与服务端进程建立连接：
            //（1）新创建一个定时器timerId_：
            // 以系统当前时间为起点，经过retryDelayMs_/1000.0秒后，定时器超时，
            // 执行定时器回调函数Connector::startInLoop，使客户端进程，再次主动与服务端进程建立连接
            // retryDelayMs_记录：客户端进程重新创建一个新的socket，再次主动与服务端进程建立连接时，
            //                    重试的间隔时间的，起始时间
            //（2）客户端进程重新创建一个新的socket，再次主动与服务端进程建立连接时，
            // 重试的间隔时间，按照2倍的方式逐渐延长，即：0.5s, 1s, 2s, 4s, ......，直至kMaxRetryDelayMs（30s）
            void stopInLoop();

            /// 客户端进程调用connect函数，来使客户端进程的套接字sockfd
            /// 与服务端进程serverAddr_（客户端进程，要连接的服务端进程的socket地址（IP地址 + 端口号））的套接字进行连接
            void connect();

            // （1）让channel_管理新的new Channel(loop_, sockfd) -- socket文件描述符sockfd
            // （2）在channel_所管理的socket文件描述符上注册读事件，并在pollfds_表（相当于epoll的内核事件表）中新增一个表项
            // 实现：客户端进程，使用poll函数，监测channel_所管理的socket文件描述符上是否有写事件发生
            // 写事件：客户端进程，准备向服务端进程发送新的连接请求
            //         客户端进程，执行此函数与服务端进程建立新的连接
            void connecting(int sockfd);

            // 套接字sockfd，上有可写事件发生时，
            // 写事件的事件处理函数为Connector::handleWrite
            // 写事件：客户端进程，准备向服务端进程发送新的连接请求
            //         客户端进程，执行此函数与服务端进程建立新的连接
            // 该函数，由客户端进程执行
            void handleWrite();

            // 写事件的事件处理函数为Connector::handleWrite
            // 写事件：客户端进程，准备向服务端进程发送新的连接请求
            //         客户端进程，执行Connector::handleWrite函数与服务端进程建立新的连接
            // 在建立连接的过程中，如果出现错误，会执行Connector::handleError函数进行处理
            // 该函数，由客户端进程执行
            void handleError();

            // 客户端进程重新创建一个新的socket，再次主动与服务端进程建立连接：
            //（1）新创建一个定时器timerId_：
            // 以系统当前时间为起点，经过retryDelayMs_/1000.0秒后，定时器超时，
            // 执行定时器回调函数Connector::startInLoop，使客户端进程，再次主动与服务端进程建立连接
            // retryDelayMs_记录：客户端进程重新创建一个新的socket，再次主动与服务端进程建立连接时，
            //                    重试的间隔时间的，起始时间
            //（2）客户端进程重新创建一个新的socket，再次主动与服务端进程建立连接时，
            // 重试的间隔时间，按照2倍的方式逐渐延长，即：0.5s, 1s, 2s, 4s, ......，直至kMaxRetryDelayMs（30s）
            void retry(int sockfd);

            // （1）不再关注：channel_所管理的文件描述符上，所发生的任何事件
            // （2）从pollfds_表（相当于epoll的内核事件表）中删除表项channel_
            // 实现：客户端进程，在使用poll函数时，不再监测channel_所管理的socket文件描述符上，是否有任何事件发生
            // （3） 清空channel_中保存的，Channel对象指针变量的值
            // 即：channel不再管理任何Channel对象，
            // 也就是，不再管理任何客户端进程创建的socket文件描述符
            int removeAndResetChannel();

            // 清空channel_中保存的，Channel对象指针变量的值
            // 即：channel不再管理任何Channel对象，
            // 也就是，不再管理任何客户端进程创建的socket文件描述符
            void resetChannel();

            EventLoop *loop_;

            // 记录：客户端进程调用connect函数，要连接的服务端进程的socket地址（IP地址 + 端口号）
            InetAddress serverAddr_;

            // 记录：客户端进程与其要连接的服务端进程，是否需要建立连接
            bool connect_; // atomic
            
            // 记录：服务端进程与客户端进程，所建立的连接的连接状态
            States state_;  // FIXME: use atomic variable

            /// class Channel类的作用：
            /// （1）channel：通道，大管子，文件描述符（文件描述符：就是插在文件上的大管子）
            /// （2）记录：实际发生的事件的表的一个表项的类型（内容）
            /// （3）用一个class Channel类，来管理一个文件描述符
            // 管理：客户端进程创建的socket文件描述符
            boost::scoped_ptr<Channel> channel_;

            // 客户端进程，使用此socket文件描述符，与服务端进程进行通信
            // 新连接回调函数，的作用：
            // 在该函数内部，实现客户端进程，主动向服务端进程发送的新的连接请求的处理
            // 使客户端进程，与服务端进程（客户端进程，要连接的服务端进程的socket地址（IP地址 + 端口号））的套接字彻底建立连接
            NewConnectionCallback newConnectionCallback_;

            // 记录：客户端进程重新创建一个新的socket，再次主动与服务端进程建立连接时，
            //       重试的间隔时间的，起始时间
            int retryDelayMs_;
        };
    }
}

#endif  // MUDUO_NET_CONNECTOR_H
