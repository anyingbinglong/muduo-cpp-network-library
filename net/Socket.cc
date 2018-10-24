// Copyright 2010, Shuo Chen.  All rights reserved.
// http://code.google.com/p/muduo/
//
// Use of this source code is governed by a BSD-style license
// that can be found in the License file.

// Author: Shuo Chen (chenshuo at chenshuo dot com)

#include <muduo/net/Socket.h>

#include <muduo/base/Logging.h>
#include <muduo/net/InetAddress.h>
#include <muduo/net/SocketsOps.h>

#include <netinet/in.h>
#include <netinet/tcp.h>
#include <strings.h>  // bzero
#include <stdio.h>  // snprintf

using namespace muduo;
using namespace muduo::net;

Socket::~Socket()
{
    // 关闭sockfd
    sockets::close(sockfd_);
}

bool Socket::getTcpInfo(struct tcp_info *tcpi) const
{
    socklen_t len = sizeof(*tcpi);
    bzero(tcpi, len);
    return ::getsockopt(sockfd_, SOL_TCP, TCP_INFO, tcpi, &len) == 0;
}

bool Socket::getTcpInfoString(char *buf, int len) const
{
    struct tcp_info tcpi;
    bool ok = getTcpInfo(&tcpi);
    if (ok)
    {
        snprintf(buf, len, "unrecovered=%u "
                 "rto=%u ato=%u snd_mss=%u rcv_mss=%u "
                 "lost=%u retrans=%u rtt=%u rttvar=%u "
                 "sshthresh=%u cwnd=%u total_retrans=%u",
                 tcpi.tcpi_retransmits,  // Number of unrecovered [RTO] timeouts
                 tcpi.tcpi_rto,          // Retransmit timeout in usec
                 tcpi.tcpi_ato,          // Predicted tick of soft clock in usec
                 tcpi.tcpi_snd_mss,
                 tcpi.tcpi_rcv_mss,
                 tcpi.tcpi_lost,         // Lost packets
                 tcpi.tcpi_retrans,      // Retransmitted packets out
                 tcpi.tcpi_rtt,          // Smoothed round trip time in usec
                 tcpi.tcpi_rttvar,       // Medium deviation
                 tcpi.tcpi_snd_ssthresh,
                 tcpi.tcpi_snd_cwnd,
                 tcpi.tcpi_total_retrans);  // Total retransmits for entire connection
    }
    return ok;
}

/// 给服务端进程的sockfd，绑定localaddr -- IP地址和端口号
void Socket::bindAddress(const InetAddress &addr)
{
    sockets::bindOrDie(sockfd_, addr.getSockAddr());
}

/// 服务端进程，监听服务端socket -- sockfd
void Socket::listen()
{
    sockets::listenOrDie(sockfd_);
}

/// 服务端进程，调用accept函数从处于监听状态的套接字sockfd_的客户端进程连接请求队列中取出排在最前面的一个客户连接请求，
/// 并且服务端进程，会创建一个新的套接字，来与客户端进程的套接字，创建连接通道
/// 服务端进程，获取到的客户端的socket地址（IP地址和端口号），将被存放到peeraddr中
int Socket::accept(InetAddress *peeraddr)
{
    struct sockaddr_in6 addr;
    bzero(&addr, sizeof addr);
    // 服务端进程，调用accept函数从处于监听状态的套接字sockfd的客户端进程连接请求队列中取出排在最前面的一个客户连接请求，
    // 并且服务端进程，会创建一个新的套接字，来与客户端进程的套接字，创建连接通道
    // 服务端进程，获取到的客户端的socket地址（IP地址和端口号），将被存放到addr中
    int connfd = sockets::accept(sockfd_, &addr);
    if (connfd >= 0)
    {
        peeraddr->setSockAddrInet6(addr);
    }
    return connfd;
}

void Socket::shutdownWrite()
{
    sockets::shutdownWrite(sockfd_);
}

void Socket::setTcpNoDelay(bool on)
{
    int optval = on ? 1 : 0;
    ::setsockopt(sockfd_, IPPROTO_TCP, TCP_NODELAY,
                 &optval, static_cast<socklen_t>(sizeof optval));
    // FIXME CHECK
}

void Socket::setReuseAddr(bool on)
{
    int optval = on ? 1 : 0;
    ::setsockopt(sockfd_, SOL_SOCKET, SO_REUSEADDR,
                 &optval, static_cast<socklen_t>(sizeof optval));
    // FIXME CHECK
}

void Socket::setReusePort(bool on)
{
#ifdef SO_REUSEPORT
    int optval = on ? 1 : 0;
    int ret = ::setsockopt(sockfd_, SOL_SOCKET, SO_REUSEPORT,
                           &optval, static_cast<socklen_t>(sizeof optval));
    if (ret < 0 && on)
    {
        LOG_SYSERR << "SO_REUSEPORT failed.";
    }
#else
    if (on)
    {
        LOG_ERROR << "SO_REUSEPORT is not supported.";
    }
#endif
}

void Socket::setKeepAlive(bool on)
{
    int optval = on ? 1 : 0;
    ::setsockopt(sockfd_, SOL_SOCKET, SO_KEEPALIVE,
                 &optval, static_cast<socklen_t>(sizeof optval));
    // FIXME CHECK
}

