// Copyright 2010, Shuo Chen.  All rights reserved.
// http://code.google.com/p/muduo/
//
// Use of this source code is governed by a BSD-style license
// that can be found in the License file.

// Author: Shuo Chen (chenshuo at chenshuo dot com)
//
// This is an internal header file, you should not include this.

#ifndef MUDUO_NET_SOCKET_H
#define MUDUO_NET_SOCKET_H

#include <boost/noncopyable.hpp>

// struct tcp_info is in <netinet/tcp.h>
struct tcp_info;

namespace muduo
{
    ///
    /// TCP networking.
    ///
    namespace net
    {

        class InetAddress;

        ///
        /// Wrapper of socket file descriptor.
        ///
        /// It closes the sockfd when desctructs.
        /// It's thread safe, all operations are delegated to OS.
        class Socket : boost::noncopyable
        {
        public:
            explicit Socket(int sockfd)
                : sockfd_(sockfd)
            { }

            // Socket(Socket&&) // move constructor in C++11
            ~Socket();

            int fd() const
            {
                return sockfd_;
            }
            // return true if success.
            bool getTcpInfo(struct tcp_info *) const;
            bool getTcpInfoString(char *buf, int len) const;

            /// abort if address in use
            /// 给服务端进程的sockfd，绑定localaddr -- IP地址和端口号
            void bindAddress(const InetAddress &localaddr);
            
            /// abort if address in use
            /// 服务端进程，监听服务端socket -- sockfd
            void listen();

            /// On success, returns a non-negative integer that is
            /// a descriptor for the accepted socket, which has been
            /// set to non-blocking and close-on-exec. *peeraddr is assigned.
            /// On error, -1 is returned, and *peeraddr is untouched.
            /// 服务端进程，调用accept函数从处于监听状态的套接字sockfd_的客户端进程连接请求队列中取出排在最前面的一个客户连接请求，
            /// 并且服务端进程，会创建一个新的套接字，来与客户端进程的套接字，创建连接通道
            /// 服务端进程，获取到的客户端的socket地址（IP地址和端口号），将被存放到peeraddr中
            int accept(InetAddress *peeraddr);

            void shutdownWrite();

            ///
            /// Enable/disable TCP_NODELAY (disable/enable Nagle's algorithm).
            ///
            void setTcpNoDelay(bool on);

            ///
            /// Enable/disable SO_REUSEADDR
            ///
            void setReuseAddr(bool on);

            ///
            /// Enable/disable SO_REUSEPORT
            ///
            void setReusePort(bool on);

            ///
            /// Enable/disable SO_KEEPALIVE
            ///
            void setKeepAlive(bool on);

        private:
            // 管理的socket文件描述符
            const int sockfd_;
        };

    }
}
#endif  // MUDUO_NET_SOCKET_H
