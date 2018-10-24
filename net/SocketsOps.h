// Copyright 2010, Shuo Chen.  All rights reserved.
// http://code.google.com/p/muduo/
//
// Use of this source code is governed by a BSD-style license
// that can be found in the License file.

// Author: Shuo Chen (chenshuo at chenshuo dot com)
//
// This is an internal header file, you should not include this.

#ifndef MUDUO_NET_SOCKETSOPS_H
#define MUDUO_NET_SOCKETSOPS_H

#include <arpa/inet.h>

namespace muduo
{
    namespace net
    {
        namespace sockets
        {

            ///
            /// Creates a non-blocking socket file descriptor,
            /// abort if any error.
            /// 创建一个非阻塞的sock
            int createNonblockingOrDie(sa_family_t family);

            int  connect(int sockfd, const struct sockaddr *addr);

            // 给服务端进程的sockfd，绑定addr -- IP地址和端口号
            void bindOrDie(int sockfd, const struct sockaddr *addr);

            // 服务端进程，监听服务端socket -- sockfd
            void listenOrDie(int sockfd);

            // 服务端进程，调用accept函数从处于监听状态的套接字sockfd的客户端进程连接请求队列中取出排在最前面的一个客户连接请求，
            // 并且服务端进程，会创建一个新的套接字，来与客户端进程的套接字，创建连接通道
            // 服务端进程，获取到的客户端的socket地址（IP地址和端口号），将被存放到addr中
            int  accept(int sockfd, struct sockaddr_in6 *addr);
            ssize_t read(int sockfd, void *buf, size_t count);
            ssize_t readv(int sockfd, const struct iovec *iov, int iovcnt);
            ssize_t write(int sockfd, const void *buf, size_t count);

            // 关闭sockfd
            void close(int sockfd);
            void shutdownWrite(int sockfd);

            void toIpPort(char *buf, size_t size,
                          const struct sockaddr *addr);
            void toIp(char *buf, size_t size,
                      const struct sockaddr *addr);

            void fromIpPort(const char *ip, uint16_t port,
                            struct sockaddr_in *addr);
            void fromIpPort(const char *ip, uint16_t port,
                            struct sockaddr_in6 *addr);

            int getSocketError(int sockfd);

            const struct sockaddr *sockaddr_cast(const struct sockaddr_in *addr);
            const struct sockaddr *sockaddr_cast(const struct sockaddr_in6 *addr);
            struct sockaddr *sockaddr_cast(struct sockaddr_in6 *addr);
            const struct sockaddr_in *sockaddr_in_cast(const struct sockaddr *addr);
            const struct sockaddr_in6 *sockaddr_in6_cast(const struct sockaddr *addr);

            struct sockaddr_in6 getLocalAddr(int sockfd);
            struct sockaddr_in6 getPeerAddr(int sockfd);
            bool isSelfConnect(int sockfd);

        }
    }
}

#endif  // MUDUO_NET_SOCKETSOPS_H
