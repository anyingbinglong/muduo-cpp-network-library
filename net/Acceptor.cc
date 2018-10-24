// Copyright 2010, Shuo Chen.  All rights reserved.
// http://code.google.com/p/muduo/
//
// Use of this source code is governed by a BSD-style license
// that can be found in the License file.

// Author: Shuo Chen (chenshuo at chenshuo dot com)

#include <muduo/net/Acceptor.h>

#include <muduo/base/Logging.h>
#include <muduo/net/EventLoop.h>
#include <muduo/net/InetAddress.h>
#include <muduo/net/SocketsOps.h>

#include <boost/bind.hpp>

#include <errno.h>
#include <fcntl.h>
//#include <sys/types.h>
//#include <sys/stat.h>
#include <unistd.h>

using namespace muduo;
using namespace muduo::net;

Acceptor::Acceptor(EventLoop *loop, const InetAddress &listenAddr, bool reuseport)
    : loop_(loop),
      // 创建一个非阻塞的sock
      acceptSocket_(sockets::createNonblockingOrDie(listenAddr.family())),
      // 对套接字acceptSocket_，其内部的成员变量：sockfd_ --> socket文件描述符，进行管理
      acceptChannel_(loop, acceptSocket_.fd()),
      // 记录：套接字acceptSocket_(其内部成员变量：sockfd_)，未处于监听状态
      listenning_(false),
      idleFd_(::open("/dev/null", O_RDONLY | O_CLOEXEC))
{
    assert(idleFd_ >= 0);
    acceptSocket_.setReuseAddr(true);
    acceptSocket_.setReusePort(reuseport);
    // 给服务端进程的acceptSocket_(其内部成员变量：sockfd_)，绑定listenAddr -- IP地址和端口号
    acceptSocket_.bindAddress(listenAddr);
    // 设置：class Channel类，所管理的文件描述符fd_;上，有读事件发生时，需要调用的读事件处理函数
    // 设置：套接字acceptSocket_(其内部成员变量：sockfd_)，上有可读事件发生时，
    // 读事件的事件处理函数为Acceptor::handleRead
    // 读事件：服务端进程，接收到客户端进程发来的数据
    acceptChannel_.setReadCallback(
        boost::bind(&Acceptor::handleRead, this));
}

Acceptor::~Acceptor()
{
    /// 不再关注class Channel类，所管理的文件描述符上的任何事件
    acceptChannel_.disableAll();
    /// ====================================================================================================
    /// 在，class PollPoller IO复用的封装：封装了poll，中的功能
    /// ====================================================================================================
    /// 函数参数含义：
    /// Channel* channel：
    ///   （1）文件描述符fd管理类
    ///   （2）将要删除的，pollfds_表（相当于epoll的内核事件监听表）的，一个表项内容
    /// 函数功能：
    /// 这个操作，相当于epoll_ctl函数参数选项中的EPOLL_CTL_DEL
    /// 删除pollfds_表（相当于epoll的内核事件监听表）中的，某个表项
    /// ====================================================================================================
    /// 在，class EPollPoller IO复用的封装：封装了epoll中的功能
    /// ====================================================================================================
    /// 函数参数含义：
    /// Channel* channel：
    ///   （1）文件描述符fd管理类
    ///   （2）将要删除的，epoll的内核事件监听表epollfd_中的，一个表项内容
    /// 函数功能：
    /// 这个操作，相当于epoll_ctl函数参数选项中的EPOLL_CTL_DEL
    /// 删除epoll的内核事件监听表epollfd_中的，某个表项
    acceptChannel_.remove();
    ::close(idleFd_);
}

// 服务端进程，开始监听服务端socket -- acceptSocket_
void Acceptor::listen()
{
    // 确保：执行事件循环（EventLoop::loop()）的线程，是IO线程
    // 即：确保，执行void Acceptor::listen()函数的线程，是IO线程
    loop_->assertInLoopThread();
    // 记录：套接字acceptSocket_(其内部成员变量：sockfd_)，处于监听状态
    listenning_ = true;
    // 服务端进程，开始监听服务端socket -- acceptSocket_
    acceptSocket_.listen();
    // 在acceptSocket_上注册读事件，并在pollfds_表（相当于epoll的内核事件表）中新增一个表项
    // 实现：服务端进程，使用poll函数，监测acceptSocket_上是否有读事件发生
    // 读事件：服务端进程，接收到客户端进程发来的数据
    acceptChannel_.enableReading();
}

// 套接字acceptSocket_(其内部成员变量：sockfd_)，上有可读事件发生时，
// 读事件的事件处理函数为Acceptor::handleRead
// 读事件：服务端进程，接收到客户端进程发来的新的连接请求
//         服务端进程，执行此函数对客户端进程发来的新的连接请求进行处理
// 该函数，由服务端进程执行
void Acceptor::handleRead()
{
    // 确保：执行事件循环（EventLoop::loop()）的线程，是IO线程
    // 即：确保，执行void Acceptor::handleRead()函数的线程，是IO线程
    loop_->assertInLoopThread();
    // 用于存放服务端进程，获取到的客户端的IP地址和端口号
    InetAddress peerAddr;
    //FIXME loop until no more
    /// 服务端进程，调用accept函数从处于监听状态的套接字acceptSocket_(其内部成员变量：sockfd_)的客户端进程连接请求队列中取出排在最前面的一个客户连接请求，
    /// 并且服务端进程，会创建一个新的套接字，来与客户端进程的套接字，创建连接通道
    /// 服务端进程，获取到的客户端的socket地址（IP地址和端口号），将被存放到peeraddr中
    /// connfd，存放服务端进程新创建的套接字的socket文件描述符
    int connfd = acceptSocket_.accept(&peerAddr);
    if (connfd >= 0)
    {
        // string hostport = peerAddr.toIpPort();
        // LOG_TRACE << "Accepts of " << hostport;
        if (newConnectionCallback_)
        {
            // 执行新连接回调函数
            // 实际执行的是：TcpServer.cc中的void TcpServer::newConnection函数
            // 实现服务端进程，对客户端进程发来的新的连接请求的处理
            newConnectionCallback_(connfd, peerAddr);
        }
        else
        {
            sockets::close(connfd);
        }
    }
    else
    {
        LOG_SYSERR << "in Acceptor::handleRead";
        // Read the section named "The special problem of
        // accept()ing when you can't" in libev's doc.
        // By Marc Lehmann, author of libev.
        if (errno == EMFILE)
        {
            ::close(idleFd_);
            idleFd_ = ::accept(acceptSocket_.fd(), NULL, NULL);
            ::close(idleFd_);
            idleFd_ = ::open("/dev/null", O_RDONLY | O_CLOEXEC);
        }
    }
}

