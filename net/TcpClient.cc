// Copyright 2010, Shuo Chen.  All rights reserved.
// http://code.google.com/p/muduo/
//
// Use of this source code is governed by a BSD-style license
// that can be found in the License file.

// Author: Shuo Chen (chenshuo at chenshuo dot com)
//

#include <muduo/net/TcpClient.h>

#include <muduo/base/Logging.h>
#include <muduo/net/Connector.h>
#include <muduo/net/EventLoop.h>
#include <muduo/net/SocketsOps.h>

#include <boost/bind.hpp>

#include <stdio.h>  // snprintf

using namespace muduo;
using namespace muduo::net;

// TcpClient::TcpClient(EventLoop* loop)
//   : loop_(loop)
// {
// }

// TcpClient::TcpClient(EventLoop* loop, const string& host, uint16_t port)
//   : loop_(CHECK_NOTNULL(loop)),
//     serverAddr_(host, port)
// {
// }

namespace muduo
{
    namespace net
    {
        namespace detail
        {

            void removeConnection(EventLoop *loop, const TcpConnectionPtr &conn)
            {
                loop->queueInLoop(boost::bind(&TcpConnection::connectDestroyed, conn));
            }

            void removeConnector(const ConnectorPtr &connector)
            {
                //connector->
            }

        }
    }
}

TcpClient::TcpClient(EventLoop *loop,
                     const InetAddress &serverAddr,
                     const string &nameArg)
    : loop_(CHECK_NOTNULL(loop)),
      connector_(new Connector(loop, serverAddr)),
      name_(nameArg),
      connectionCallback_(defaultConnectionCallback),
      messageCallback_(defaultMessageCallback),
      retry_(false),
      connect_(true),
      nextConnId_(1)
{
    // 设置新连接回调函数为：TcpClient::newConnection
    connector_->setNewConnectionCallback(
        boost::bind(&TcpClient::newConnection, this, _1));
    // FIXME setConnectFailedCallback
    LOG_INFO << "TcpClient::TcpClient[" << name_
             << "] - connector " << get_pointer(connector_);
}

TcpClient::~TcpClient()
{
    LOG_INFO << "TcpClient::~TcpClient[" << name_
             << "] - connector " << get_pointer(connector_);
    TcpConnectionPtr conn;
    bool unique = false;
    {
        MutexLockGuard lock(mutex_);
        // class TcpConnection这个类的作用：
        // （1）管理客户端和服务端之间，建立的，TCP连接
        // （2）这个类所创建的一个对象，就是一个，TCP连接管理对象，这个对象中，保存着这个TCP连接的相关信息
        // 多线程共享资源
        // 记录：服务端进程和客户端进程建立的连接信息：
        // （1）连接的名字
        // （2）TCP连接管理对象TcpConnection
        unique = connection_.unique();
        conn = connection_;
    }
    if (conn)
    {
        assert(loop_ == conn->getLoop());
        // FIXME: not 100% safe, if we are in different thread
        CloseCallback cb = boost::bind(&detail::removeConnection, loop_, _1);

        // TcpConnection::setCloseCallback充当，主动关闭TCP连接回调函数，的作用：
        // 作为，客户端进程，主动关闭TCP连接回调函数，的作用：
        // 客户端进程，想主动关闭与服务端进程，建立的连接
        // 因此，客户端进程，可以执行TcpConnection::setCloseCallback函数，进行主动关闭TCP连接
        loop_->runInLoop(
            boost::bind(&TcpConnection::setCloseCallback, conn, cb));
        if (unique)
        {
            // 客户端执行此函数：客户端主动关闭，和，服务端建立的连接
            conn->forceClose();
        }
    }
    else
    {
        // 定时器timerId_的作用：
        // 以系统当前时间为起点，经过retryDelayMs_/1000.0秒后，定时器超时，
        // 执行定时器回调函数Connector::startInLoop，使客户端进程，再次主动与服务端进程建立连接
        // retryDelayMs_记录：客户端进程重新创建一个新的socket，再次主动与服务端进程建立连接时，
        //                    重试的间隔时间的，起始时间
        // =====================================================================================
        // 注销定时器timerId_：
        // 实现，客户端进程，不再尝试主动与服务端进程建立连接
        connector_->stop();
        // FIXME: HACK
        loop_->runAfter(1, boost::bind(&detail::removeConnector, connector_));
    }
}

/// 客户端进程主动开始，与服务端进程建立连接
void TcpClient::connect()
{
    // FIXME: check state
    LOG_INFO << "TcpClient::connect[" << name_ << "] - connecting to "
             << connector_->serverAddress().toIpPort();
    // 记录：客户端进程与其要连接的服务端进程，需要建立连接
    connect_ = true;
    /// 客户端进程主动开始，与服务端进程建立连接：
    /// 客户端进程调用connect函数，来使客户端进程的套接字sockfd
    /// 与服务端进程serverAddr_（客户端进程，要连接的服务端进程的socket地址（IP地址 + 端口号））的套接字进行连接
    connector_->start();
}

/// 客户端进程主动开始，与服务端进程断开连接
void TcpClient::disconnect()
{
    // 记录：客户端进程与其要连接的服务端进程，不再需要建立连接
    connect_ = false;

    {
        MutexLockGuard lock(mutex_);
        if (connection_)
        {
            // （1）不再关注：channel_（connection_对象的成员变量）所管理的服务端进程新创建的套接字的socket文件描述符上，写事件是否发生
            // 写事件：客户端进程，准备向服务端进程发送数据
            // 即：客户端进程，不再向服务端进程，发送数据
            // （2）关闭socket_（connection_对象的成员变量）上的写的这一半，应用程序不可再对该socket_执行写操作
            connection_->shutdown();
        }
    }
}

// 定时器timerId_的作用：
// 以系统当前时间为起点，经过retryDelayMs_/1000.0秒后，定时器超时，
// 执行定时器回调函数Connector::startInLoop，使客户端进程，再次主动与服务端进程建立连接
// retryDelayMs_记录：客户端进程重新创建一个新的socket，再次主动与服务端进程建立连接时，
//                    重试的间隔时间的，起始时间
// =====================================================================================
// 注销定时器timerId_：
// 实现，客户端进程，不再尝试主动与服务端进程建立连接
void TcpClient::stop()
{
    // 记录：客户端进程与其要连接的服务端进程，不再需要建立连接
    connect_ = false;
    // 定时器timerId_的作用：
    // 以系统当前时间为起点，经过retryDelayMs_/1000.0秒后，定时器超时，
    // 执行定时器回调函数Connector::startInLoop，使客户端进程，再次主动与服务端进程建立连接
    // retryDelayMs_记录：客户端进程重新创建一个新的socket，再次主动与服务端进程建立连接时，
    //                    重试的间隔时间的，起始时间
    // =====================================================================================
    // 注销定时器timerId_：
    // 实现，客户端进程，不再尝试主动与服务端进程建立连接
    connector_->stop();
}

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
void TcpClient::newConnection(int sockfd)
{
    // 确保：执行事件循环（EventLoop::loop()）的线程，是IO线程
    // 即：确保，执行TcpClient::newConnection函数的线程，是IO线程
    loop_->assertInLoopThread();
    // 获取sockfd对应的远端socket地址（IP地址 + 端口号）
    // 即：客户端进程执行此函数，获取该客户端进程要连接的服务端进程的socket地址（IP地址 + 端口号）
    InetAddress peerAddr(sockets::getPeerAddr(sockfd));
    char buf[32];
    snprintf(buf, sizeof buf, ":%s#%d", peerAddr.toIpPort().c_str(), nextConnId_);
    ++nextConnId_;
    // 创建：服务端进程与客户端进程，所建立的连接的名字
    string connName = name_ + buf;

    // 获取sockfd对应的本端socket地址（IP地址 + 端口号）
    // 即：获取客户端进程的socket地址（IP地址 + 端口号）
    InetAddress localAddr(sockets::getLocalAddr(sockfd));
    // FIXME poll with zero timeout to double confirm the new connection
    // FIXME use make_shared if necessary
    // 创建TCP连接管理对象conn，管理服务端进程与客户端进程新建立的连接
    // 一个TcpClinet，只管理一个TcpConnection（TCP连接管理对象conn）
    TcpConnectionPtr conn(new TcpConnection(loop_,
                                            connName,
                                            sockfd,
                                            localAddr,
                                            peerAddr));
    // 设置连接回调函数
    conn->setConnectionCallback(connectionCallback_);
    // 设置消息回调函数
    conn->setMessageCallback(messageCallback_);
    // 设置写完成回调函数
    conn->setWriteCompleteCallback(writeCompleteCallback_);
    // 设置：客户端进程，被动关闭TCP连接回调函数closeCallback_
    // 客户端进程，从套接字socket_(其内部成员变量：sockfd_)中未读取到，服务端进程发来的数据
    // 意味着：服务端进程，主动关闭了TCP连接，
    // 因此，客户端进程，需要执行TcpClient::removeConnection函数，进行被动关闭TCP连接
    conn->setCloseCallback(
        boost::bind(&TcpClient::removeConnection, this, _1)); // FIXME: unsafe
    {
        MutexLockGuard lock(mutex_);
        // class TcpConnection这个类的作用：
        // （1）管理客户端和服务端之间，建立的，TCP连接
        // （2）这个类所创建的一个对象，就是一个，TCP连接管理对象，这个对象中，保存着这个TCP连接的相关信息
        // ===============================================================================================
        // 一个TcpClinet，只管理一个TcpConnection（TCP连接管理对象conn），就体现在这里：
        // （1）TcpServer中的connections_的定义：
        // typedef std::map<std::string, TcpConnectionPtr> ConnectionMap;
        // ConnectionMap connections_;
        // 意味着：一个TcpServer，管理多个TcpConnection（TCP连接管理对象conn）
        // （2）TcpConnection中的connections_的定义：
        // TcpConnectionPtr connection_;
        // 意味着：一个TcpClinet，只管理一个TcpConnection（TCP连接管理对象conn）
        // ===============================================================================================
        // 将conn，此连接的信息保存到connection_中
        // ===============================================================================================
        connection_ = conn;
    }
    /// 使客户端进程与服务端进程，真正建立起连接：
    /// 在channel_（conn对象的成员变量）管理的socket文件描述符上注册读事件，并在pollfds_表（相当于epoll的内核事件表）中新增一个表项
    /// 实现：客户端进程，使用poll函数，监测channel_（conn对象的成员变量）管理的socket文件描述符上是否有读事件发生
    /// 读事件：客户端进程，接收到服务端进程发来的数据
    /// 并执行，连接回调函数connectionCallback_（conn对象的成员变量），通知服务端进程，连接建立成功
    conn->connectEstablished();
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
void TcpClient::removeConnection(const TcpConnectionPtr &conn)
{
    // 确保：执行事件循环（EventLoop::loop()）的线程，是IO线程
    // 即：确保，执行void TcpClient::removeConnectionInLoop函数的线程，是IO线程
    loop_->assertInLoopThread();
    assert(loop_ == conn->getLoop());

    {
        MutexLockGuard lock(mutex_);
        assert(connection_ == conn);
        // boost::scoped_ptr的成员函数reset()的功能是重置scoped_ptr；它删除原来保存的指针变量的值，再保存新的指针变量的值p。
        // 如果p是空指针，那么scoped_ptr将不能持有任何指针变量的值
        // void reset(_Ty * p = 0)  //never throw
        // {
        //     this_type(p).swap(*this);
        // }
        // 1.）connection_：的作用
        // class TcpConnection这个类的作用：
        // （1）管理客户端和服务端之间，建立的，TCP连接
        // （2）这个类所创建的一个对象，就是一个，TCP连接管理对象，这个对象中，保存着这个TCP连接的相关信息
        // ===============================================================================================
        // 2.）connection_.reset();这句话的作用：
        // 删除，服务端进程和客户端进程建立的conn连接信息
        connection_.reset();
    }

    // （1）把TcpConnection::connectDestroyed(conn)函数，放入到需要
    // 延期执行的用户任务回调函数队列中，并在必要时，唤醒IO线程，执行该函数
    // （2）TcpConnection::connectDestroyed的作用：
    // 客户端进程，执行此函数：彻底断开客户端与服务端建立的TCP连接
    loop_->queueInLoop(boost::bind(&TcpConnection::connectDestroyed, conn));

    // 记录：客户端进程，是否需要，重新创建一个新的socket，再次主动与服务端进程建立连接
    // bool retry_; 
    // 记录：客户端进程与其要连接的服务端进程，是否需要建立连接
    // bool connect_;
    if (retry_ && connect_)
    {
        LOG_INFO << "TcpClient::connect[" << name_ << "] - Reconnecting to "
                 << connector_->serverAddress().toIpPort();
        // 使客户端进程，重新与其要连接的服务端进程，建立连接
        connector_->restart();
    }
}