// Copyright 2010, Shuo Chen.  All rights reserved.
// http://code.google.com/p/muduo/
//
// Use of this source code is governed by a BSD-style license
// that can be found in the License file.

// Author: Shuo Chen (chenshuo at chenshuo dot com)

#include <muduo/net/TcpServer.h>

#include <muduo/base/Logging.h>
#include <muduo/net/Acceptor.h>
#include <muduo/net/EventLoop.h>
#include <muduo/net/EventLoopThreadPool.h>
#include <muduo/net/SocketsOps.h>

#include <boost/bind.hpp>

#include <stdio.h>  // snprintf

using namespace muduo;
using namespace muduo::net;

TcpServer::TcpServer(EventLoop *loop,
                     const InetAddress &listenAddr,
                     const string &nameArg,
                     Option option)
    : loop_(CHECK_NOTNULL(loop)),
      ipPort_(listenAddr.toIpPort()),
      name_(nameArg),
      acceptor_(new Acceptor(loop, listenAddr, option == kReusePort)),
      threadPool_(new EventLoopThreadPool(loop, name_)),
      connectionCallback_(defaultConnectionCallback),
      messageCallback_(defaultMessageCallback),
      nextConnId_(1)
{
    acceptor_->setNewConnectionCallback(
        boost::bind(&TcpServer::newConnection, this, _1, _2));
}

TcpServer::~TcpServer()
{
    loop_->assertInLoopThread();
    LOG_TRACE << "TcpServer::~TcpServer [" << name_ << "] destructing";

    for (ConnectionMap::iterator it(connections_.begin());
            it != connections_.end(); ++it)
    {
        TcpConnectionPtr conn(it->second);
        it->second.reset();
        conn->getLoop()->runInLoop(
            boost::bind(&TcpConnection::connectDestroyed, conn));
    }
}

// 设置，事件循环线程池class EventLoopThreadPool中，线程的个数
void TcpServer::setThreadNum(int numThreads)
{
    assert(0 <= numThreads);
    threadPool_->setThreadNum(numThreads);
}

// 启动服务端进程：
// 在acceptSocket_（acceptor_对象的成员变量）上注册读事件，并在pollfds_表（相当于epoll的内核事件表）中新增一个表项
// 实现：服务端进程，使用poll函数，监测acceptSocket_（acceptor_对象的成员变量）上是否有读事件发生
// 读事件：服务端进程，接收到客户端进程发来的数据
void TcpServer::start()
{
    if (started_.getAndSet(1) == 0)
    {
        // 创建事件循环线程（IO线程）池中的线程
        // 并将多线程共享的EventLoop对象，放入到EventLoop对象缓冲区loops_中
        threadPool_->start(threadInitCallback_);

        // 套接字acceptSocket_(其内部成员变量：sockfd_)，未处于监听状态
        assert(!acceptor_->listenning());

        // 在IO线程（创建了EventLoop对象的线程）中，执行Acceptor::listen函数,
        // 实现：服务端进程，开始监听服务端socket -- acceptSocket_，即：
        // 在acceptSocket_（acceptor_对象的成员变量）上注册读事件，并在pollfds_表（相当于epoll的内核事件表）中新增一个表项
        // 实现：服务端进程，使用poll函数，监测acceptSocket_（acceptor_对象的成员变量）上是否有读事件发生
        // 读事件：服务端进程，接收到客户端进程发来的数据
        loop_->runInLoop(
            boost::bind(&Acceptor::listen, get_pointer(acceptor_)));
    }
}

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
void TcpServer::newConnection(int sockfd, const InetAddress &peerAddr)
{
    // 确保：执行事件循环（EventLoop::loop()）的线程，是IO线程
    // 即：确保，执行void TcpServer::newConnection函数的线程，是IO线程
    // 也就是确保，执行void TcpServer::newConnection函数的线程，是服务端线程
    loop_->assertInLoopThread();
    EventLoop *ioLoop = threadPool_->getNextLoop();
    char buf[64];
    snprintf(buf, sizeof buf, "-%s#%d", ipPort_.c_str(), nextConnId_);
    ++nextConnId_;
    // 创建：服务端进程与客户端进程，所建立的连接的名字
    string connName = name_ + buf;

    LOG_INFO << "TcpServer::newConnection [" << name_
             << "] - new connection [" << connName
             << "] from " << peerAddr.toIpPort();
    // 为服务端进程，分配socket地址（IP地址和端口号）
    InetAddress localAddr(sockets::getLocalAddr(sockfd));
    // FIXME poll with zero timeout to double confirm the new connection
    // FIXME use make_shared if necessary
    // 创建TCP连接管理对象conn，管理服务端进程与客户端进程新建立的连接
    TcpConnectionPtr conn(new TcpConnection(ioLoop,
                                            connName,
                                            sockfd,
                                            localAddr,
                                            peerAddr));

    // connections_记录：服务端进程和客户端进程建立的连接信息：
    // （1）连接的名字
    // （2）TCP连接管理对象TcpConnection
    // 将connName，此连接的信息保存到connections_中
    connections_[connName] = conn;
    // 设置连接回调函数
    conn->setConnectionCallback(connectionCallback_);
    // 设置消息回调函数
    conn->setMessageCallback(messageCallback_);
    // 设置写完成回调函数
    conn->setWriteCompleteCallback(writeCompleteCallback_);
    // 设置关闭连接回到函数
    conn->setCloseCallback(
        boost::bind(&TcpServer::removeConnection, this, _1)); // FIXME: unsafe

    /// 使客户端进程与服务端进程，真正建立起连接：
    /// 在channel_（conn对象的成员变量）管理的socket文件描述符上注册读事件，并在pollfds_表（相当于epoll的内核事件表）中新增一个表项
    /// 实现：服务端进程，使用poll函数，监测channel_（conn对象的成员变量）管理的socket文件描述符上是否有读事件发生
    /// 读事件：服务端进程，接收到客户端进程发来的数据
    /// 并执行，连接回调函数connectionCallback_（conn对象的成员变量），通知客户端进程，连接建立成功
    ioLoop->runInLoop(boost::bind(&TcpConnection::connectEstablished, conn));
}

// 关闭（销毁）服务端和客户端建立的连接
void TcpServer::removeConnection(const TcpConnectionPtr &conn)
{
    // FIXME: unsafe
    // 将需要在IO线程中执行的用户回调函数TcpServer::removeConnectionInLoop，放入到队列中保存，并在必要时唤醒IO线程，执行这个用户任务回调函数
    loop_->runInLoop(boost::bind(&TcpServer::removeConnectionInLoop, this, conn));
}

// 函数参数含义：
// const TcpConnectionPtr &conn：TCP连接管理对象，其中保存了服务端进程和客户端进程建立的连接信息
// 函数功能：
// 关闭（销毁）服务端和客户端建立的连接，具体处理的内容如下
// （1）connections_记录：服务端进程和客户端进程建立的连接信息：
//      连接的名字
//      TCP连接管理对象TcpConnection
//  从connections_中删除，conn->name()这条连接信息
// （2）服务端进程，执行此函数：彻底断开客户端与服务端建立的TCP连接
void TcpServer::removeConnectionInLoop(const TcpConnectionPtr &conn)
{
    // 确保：执行事件循环（EventLoop::loop()）的线程，是IO线程
    // 即：确保，执行void TcpServer::removeConnectionInLoop函数的线程，是IO线程
    loop_->assertInLoopThread();
    LOG_INFO << "TcpServer::removeConnectionInLoop [" << name_
             << "] - connection " << conn->name();

    // connections_记录：服务端进程和客户端进程建立的连接信息：
    // （1）连接的名字
    // （2）TCP连接管理对象TcpConnection
    // 从connections_中删除，conn->name()这条连接信息
    size_t n = connections_.erase(conn->name());
    (void)n;
    assert(n == 1);
    // 获取class TcpConnection类中，保存的，EventLoop对象
    EventLoop *ioLoop = conn->getLoop();
    // TcpConnection::connectDestroyed:
    // 服务端进程，执行此函数：彻底断开客户端与服务端建立的TCP连接
    // 将需要在IO线程中执行的用户回调函数TcpConnection::connectDestroyed，放入到队列中保存，并在必要时唤醒IO线程，执行这个用户任务回调函数
    ioLoop->queueInLoop(
        boost::bind(&TcpConnection::connectDestroyed, conn));
}