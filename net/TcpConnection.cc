// Copyright 2010, Shuo Chen.  All rights reserved.
// http://code.google.com/p/muduo/
//
// Use of this source code is governed by a BSD-style license
// that can be found in the License file.

// Author: Shuo Chen (chenshuo at chenshuo dot com)

#include <muduo/net/TcpConnection.h>

#include <muduo/base/Logging.h>
#include <muduo/base/WeakCallback.h>
#include <muduo/net/Channel.h>
#include <muduo/net/EventLoop.h>
#include <muduo/net/Socket.h>
#include <muduo/net/SocketsOps.h>

#include <boost/bind.hpp>

#include <errno.h>

using namespace muduo;
using namespace muduo::net;

void muduo::net::defaultConnectionCallback(const TcpConnectionPtr &conn)
{
    LOG_TRACE << conn->localAddress().toIpPort() << " -> "
              << conn->peerAddress().toIpPort() << " is "
              << (conn->connected() ? "UP" : "DOWN");
    // do not call conn->forceClose(), because some users want to register message callback only.
}

void muduo::net::defaultMessageCallback(const TcpConnectionPtr &,
                                        Buffer *buf,
                                        Timestamp)
{
    buf->retrieveAll();
}

TcpConnection::TcpConnection(EventLoop *loop,
                             const string &nameArg,
                             int sockfd,
                             const InetAddress &localAddr,
                             const InetAddress &peerAddr)
    : loop_(CHECK_NOTNULL(loop)),
      // 存放服务端进程与客户端进程，所建立的连接的名字
      name_(nameArg),
      // 存放服务端进程与客户端进程，所建立的连接的连接状态
      state_(kConnecting),
      reading_(true),
      // （1）第一个作用
      // 服务端进程，调用accept函数从处于监听状态的套接字的客户端进程连接请求队列中取出排在最前面的一个客户连接请求，
      // 并且服务端进程，会创建一个新的套接字，来与客户端进程的套接字，创建连接通道
      // socket_：管理（用其内部成员变量保存）服务端进程新创建的套接字的socket文件描述符
      // （2）第二个作用
      // 客户端进程，创建一个非阻塞的socket，用于与服务端进程进行通信
      // 客户端进程调用connect函数，来使客户端进程的套接字sockfd与服务端进程的套接字进行连接
      // socket_：管理（用其内部成员变量保存）客户端进程新创建的套接字的socket文件描述符sockfd
      socket_(new Socket(sockfd)),
      channel_(new Channel(loop, sockfd)),
      localAddr_(localAddr),
      peerAddr_(peerAddr),
      highWaterMark_(64 * 1024 * 1024)
{
    // 设置：套接字socket_(其内部成员变量：sockfd_)，上有可读事件发生时，
    // 读事件的事件处理函数为void TcpConnection::handleRead()
    // 读事件：服务端进程，接收到客户端进程发来的数据
    channel_->setReadCallback(
        boost::bind(&TcpConnection::handleRead, this, _1));

    // 设置：套接字socket_(其内部成员变量：sockfd_)，上有可写事件发生时，
    // 写事件的事件处理函数为void TcpConnection::handleWrite()
    // 写事件：服务端进程，准备向客户端进程发送数据
    channel_->setWriteCallback(
        boost::bind(&TcpConnection::handleWrite, this));

    // 设置：服务端进程，从套接字socket_(其内部成员变量：sockfd_)中未读取到，客户端进程发来的数据
    // 意味着：客户端进程，主动关闭了TCP连接，
    // 因此，服务端进程，需要执行handleClose函数，进行被动关闭TCP连接
    channel_->setCloseCallback(
        boost::bind(&TcpConnection::handleClose, this));

    // 设置：服务端进程，从套接字socket_(其内部成员变量：sockfd_)中读取，客户端进程发来的数据时，出现了错误
    // 执行此函数，进行错误处理
    channel_->setErrorCallback(
        boost::bind(&TcpConnection::handleError, this));
    LOG_DEBUG << "TcpConnection::ctor[" <<  name_ << "] at " << this
              << " fd=" << sockfd;
    socket_->setKeepAlive(true);
}

TcpConnection::~TcpConnection()
{
    LOG_DEBUG << "TcpConnection::dtor[" <<  name_ << "] at " << this
              << " fd=" << channel_->fd()
              << " state=" << stateToString();
    assert(state_ == kDisconnected);
}

bool TcpConnection::getTcpInfo(struct tcp_info *tcpi) const
{
    return socket_->getTcpInfo(tcpi);
}

string TcpConnection::getTcpInfoString() const
{
    char buf[1024];
    buf[0] = '\0';
    socket_->getTcpInfoString(buf, sizeof buf);
    return buf;
}

// 函数参数含义：
//    const void *data：需要发送的数据，存放到这里了
//    size_t len：需要发送的数据的总长度
// 函数功能：
//  （1）客户端执行这个函数，将buf中的数据，发送给服务端
//  （2）服务端执行这个函数，将buf中的数据，发送给客户端
void TcpConnection::send(const void *data, int len)
{
    send(StringPiece(static_cast<const char *>(data), len));
}

// 函数参数含义：
//    const StringPiece &message：需要发送的数据，都存放都这里了
// 函数功能：
//  （1）客户端执行这个函数，将buf中的数据，发送给服务端
//  （2）服务端执行这个函数，将buf中的数据，发送给客户端
void TcpConnection::send(const StringPiece &message)
{
    if (state_ == kConnected)
    {
        // 正在执行TcpConnection::send的函数的线程，是IO线程
        if (loop_->isInLoopThread())
        {
            sendInLoop(message);
        }
        else// 正在执行TcpConnection::send的函数的线程，不是IO线程
        {
            /// 在IO线程中，执行用户任务回调函数TcpConnection::sendInLoop
            /// (1)当前在IO线程中，所以，可以立即执行用户任务回调函数TcpConnection::sendInLoop
            /// (2)当前不在IO线程中，所以，就不能立即执行用户任务回回调函数
            /// 将需要在IO线程中执行的用户回调函数TcpConnection::sendInLoop，放入到队列中保存，并在必要时唤醒IO线程，执行这个用户任务回调函数
            loop_->runInLoop(
                boost::bind(&TcpConnection::sendInLoop,
                            this,     // FIXME
                            message.as_string()));
            //std::forward<string>(message)));
        }
    }
}

// FIXME efficiency!!!
// 函数参数含义：
//    Buffer *buf：需要发送的数据，都存放都这里了
// 函数功能：
//  （1）客户端执行这个函数，将buf中的数据，发送给服务端
//  （2）服务端执行这个函数，将buf中的数据，发送给客户端
void TcpConnection::send(Buffer *buf)
{
    if (state_ == kConnected)
    {
        // 正在执行TcpConnection::send的函数的线程，是IO线程
        if (loop_->isInLoopThread())
        {
            sendInLoop(buf->peek(), buf->readableBytes());
            buf->retrieveAll();
        }
        else // 正在执行TcpConnection::send的函数的线程，不是IO线程
        {
            /// 在IO线程中，执行用户任务回调函数TcpConnection::sendInLoop
            /// (1)当前在IO线程中，所以，可以立即执行用户任务回调函数TcpConnection::sendInLoop
            /// (2)当前不在IO线程中，所以，就不能立即执行用户任务回回调函数
            /// 将需要在IO线程中执行的用户回调函数TcpConnection::sendInLoop，放入到队列中保存，并在必要时唤醒IO线程，执行这个用户任务回调函数
            loop_->runInLoop(
                boost::bind(&TcpConnection::sendInLoop,
                            this,     // FIXME
                            buf->retrieveAllAsString()));
            //std::forward<string>(message)));
        }
    }
}

// 函数参数的含义：
//    const StringPiece &message：需要发送的数据，都存放都这里了
// 函数功能：
//  （1）客户端执行这个函数，将data中的数据，发送给服务端
//  （2）服务端执行这个函数，将data中的数据，发送给客户端
void TcpConnection::sendInLoop(const StringPiece &message)
{
    sendInLoop(message.data(), message.size());
}

// 函数参数的含义：
//    const void *data：需要发送的数据，存放到这里了
//    size_t len：需要发送的数据的总长度
// 函数功能：
//  （1）客户端执行这个函数，将data中的数据，发送给服务端
//  （2）服务端执行这个函数，将data中的数据，发送给客户端
void TcpConnection::sendInLoop(const void *data, size_t len)
{
    // 确保：执行事件循环（EventLoop::loop()）的线程，是IO线程
    // 即：确保，执行TcpConnection::sendInLoop函数的线程，是IO线程
    loop_->assertInLoopThread();
    ssize_t nwrote = 0;
    size_t remaining = len;
    bool faultError = false;
    if (state_ == kDisconnected)
    {
        LOG_WARN << "disconnected, give up writing";
        return;
    }

    // if no thing in output queue, try writing directly
    // !channel_->isWriting()：channel_上，此时并未正在进行发送数据
    // outputBuffer_.readableBytes() == 0：outputBuffer_中，没有待发送的数据
    if (!channel_->isWriting() && outputBuffer_.readableBytes() == 0)
    {
        // 实施发送数据
        // nwrote：记录本次执行sockets::write时，总共发送了多少数据
        nwrote = sockets::write(channel_->fd(), data, len);
        if (nwrote >= 0)
        {
            // size_t len：需要发送的数据的总长度
            // nwrote：记录本次执行sockets::write时，总共发送了多少数据
            // remaining：记录，还剩下多少数据没有被发送
            remaining = len - nwrote;
            // 写完成回调函数writeCompleteCallback_的作用：
            // 1.）输出缓冲区outputBuffer_的作用：
            //     (1)服务端，将需要发送给客户端的数据，存放到这里，然后发送给客户端
            //     (2)客户端，将需要发送给服务端的数据，存放到这里，然后发送给服务端
            // 2.）输出缓冲区outputBuffer_中的数据，发送完毕后，会调用这个回调函数，提示数据发送完成
            if (remaining == 0 && writeCompleteCallback_)
            {
                /// 将需要在IO线程中执行的用户回调函数writeCompleteCallback_，放入到队列中保存，并在必要时唤醒IO线程，执行这个用户任务回调函数
                loop_->queueInLoop(boost::bind(writeCompleteCallback_, shared_from_this()));
            }
        }
        else // nwrote < 0
        {
            nwrote = 0;
            if (errno != EWOULDBLOCK)
            {
                LOG_SYSERR << "TcpConnection::sendInLoop";
                if (errno == EPIPE || errno == ECONNRESET) // FIXME: any others?
                {
                    faultError = true;
                }
            }
        }
    }

    // 代码执行到这里，意味着：outputBuffer_.readableBytes() != 0 ，也就是，outputBuffer_中有待发送的数据
    // 那就不能执行上面的if语句内部的逻辑，进行先发送数据，因为这会造成数据乱序，
    // 所以，需要将本次发送的数据，也执行outputBuffer_.append(static_cast<const char *>(data) + nwrote, remaining)，
    // 保存到outputBuffer_中
    assert(remaining <= len);
    if (!faultError && remaining > 0)
    {
        // 获取outputBuffer_中待发送的数据的长度oldLen
        size_t oldLen = outputBuffer_.readableBytes();

        // 高水位回调函数highWaterMarkCallback_的作用：
        // 1.）输出缓冲区outputBuffer_的作用：
        //     (1)服务端，将需要发送给客户端的数据，存放到这里，然后发送给客户端
        //     (2)客户端，将需要发送给服务端的数据，存放到这里，然后发送给服务端
        // 2.）输出缓冲区outputBuffer_中的待发送数据的长度（可读数据的长度：outputBuffer_.readableBytes()的返回值），
        //    超过用户指定的大小highWaterMark_，就会调用这个函数，提示发送的数据的数量太大
        if (oldLen + remaining >= highWaterMark_
                && oldLen < highWaterMark_
                && highWaterMarkCallback_)
        {
            /// 将需要在IO线程中执行的用户回调函数highWaterMarkCallback_，放入到队列中保存，并在必要时唤醒IO线程，执行这个用户任务回调函数
            loop_->queueInLoop(boost::bind(highWaterMarkCallback_, shared_from_this(), oldLen + remaining));
        }
        // 将本次发送的数据data，保存到outputBuffer_中
        outputBuffer_.append(static_cast<const char *>(data) + nwrote, remaining);
        // !channel_->isWriting()：channel_上，此时并未正在进行发送数据
        if (!channel_->isWriting())
        {
            /// 在epoll的内核事件监听表中，注册class Channel类，所管理的文件描述符fd_;
            /// 并让epoll_wait关注其上是否有写事件发生
            channel_->enableWriting();
        }
    }
}

// （1）设置：服务端进程与客户端进程，所建立的连接的连接状态
// 为：kDisconnecting，正在关闭服务端和客户端之间的TCP连接，状态
// （2）关闭socket_上的写的这一半，应用程序不可再对该socket_执行写操作
// socket_的作用：
// 服务端进程，调用accept函数从处于监听状态的套接字的客户端进程连接请求队列中取出排在最前面的一个客户连接请求，
// 并且服务端进程，会创建一个新的套接字，来与客户端进程的套接字，创建连接通道
// socket_的作用，就是管理（用其内部成员变量保存）服务端进程新创建的套接字的socket文件描述符
void TcpConnection::shutdown()
{
    // FIXME: use compare and swap
    // 服务端进程与客户端进程，所建立的连接的连接状态
    // 为：kConnected，服务端和客户端之间的TCP连接已建立，状态
    if (state_ == kConnected)
    {
        // 设置：服务端进程与客户端进程，所建立的连接的连接状态
        // 为：kDisconnecting，正在关闭服务端和客户端之间的TCP连接，状态
        setState(kDisconnecting);
        // FIXME: shared_from_this()?
        // 在IO线程中，执行TcpConnection::shutdownInLoop
        loop_->runInLoop(boost::bind(&TcpConnection::shutdownInLoop, this));
    }
}

// （1）设置：服务端进程与客户端进程，所建立的连接的连接状态
// 为：kDisconnecting，正在关闭服务端和客户端之间的TCP连接，状态
// （2）关闭socket_上的写的这一半，应用程序不可再对该socket_执行写操作
// socket_的作用：
// 服务端进程，调用accept函数从处于监听状态的套接字的客户端进程连接请求队列中取出排在最前面的一个客户连接请求，
// 并且服务端进程，会创建一个新的套接字，来与客户端进程的套接字，创建连接通道
// socket_的作用，就是管理（用其内部成员变量保存）服务端进程新创建的套接字的socket文件描述符
void TcpConnection::shutdownInLoop()
{
    // 确保：执行事件循环（EventLoop::loop()）的线程，是IO线程
    // 即：确保，执行void TcpConnection::shutdownInLoop()函数的线程，是IO线程
    loop_->assertInLoopThread();
    // （1）socket_的作用：
    // 服务端进程，调用accept函数从处于监听状态的套接字的客户端进程连接请求队列中取出排在最前面的一个客户连接请求，
    // 并且服务端进程，会创建一个新的套接字，来与客户端进程的套接字，创建连接通道
    // socket_的作用，就是管理（用其内部成员变量保存）服务端进程新创建的套接字的socket文件描述符
    // （2）channel_的作用：
    // 管理socket_（其内部成员变量）：服务端进程新创建的套接字的socket文件描述符
    // （3）!channel_->isWriting();这句话的作用：
    // 不再关注：channel_所管理的服务端进程新创建的套接字的socket文件描述符上，写事件是否发生
    // 写事件：
    // 1.服务端进程，准备向客户端进程发送数据
    // 即：服务端进程，不再向客户端进程，发送数据
    // 2.客户端进程，准备向服务端进程发送数据
    // 即：客户端进程，不再向服务端进程，发送数据
    if (!channel_->isWriting())
    {
        // we are not writing
        // 见，游双P81：关闭socket_上的写的这一半，应用程序不可再对该socket_执行写操作
        socket_->shutdownWrite();
    }
}

// void TcpConnection::shutdownAndForceCloseAfter(double seconds)
// {
//   // FIXME: use compare and swap
//   if (state_ == kConnected)
//   {
//     setState(kDisconnecting);
//     loop_->runInLoop(boost::bind(&TcpConnection::shutdownAndForceCloseInLoop, this, seconds));
//   }
// }

// void TcpConnection::shutdownAndForceCloseInLoop(double seconds)
// {
//   loop_->assertInLoopThread();
//   if (!channel_->isWriting())
//   {
//     // we are not writing
//     socket_->shutdownWrite();
//   }
//   loop_->runAfter(
//       seconds,
//       makeWeakCallback(shared_from_this(),
//                        &TcpConnection::forceCloseInLoop));
// }

// 服务端执行此函数：服务端主动关闭，和，客户端建立的连接
// 客户端执行此函数：客户端主动关闭，和，服务端建立的连接
void TcpConnection::forceClose()
{
    // FIXME: use compare and swap
    /// 客户端与服务端之间，所建立的连接的，状态，为：
    // kConnected：服务端和客户端之间的连接已经建立完毕
    // kDisconnecting：正在关闭服务端和客户端之间的TCP连接
    if (state_ == kConnected || state_ == kDisconnecting)
    {
        // 将服务端进程与客户端进程，所建立的连接的连接状态，改为kDisconnecting状态（连接已关闭）
        setState(kDisconnecting);
        /// 将需要在IO线程中执行的用户回调函数TcpConnection::forceCloseInLoop，放入到队列中保存，并在必要时唤醒IO线程，执行这个用户任务回调函数
        loop_->queueInLoop(boost::bind(&TcpConnection::forceCloseInLoop, shared_from_this()));
    }
}

// 函数参数含义：
// double seconds：定时器的超时时间
// TcpConnection::forceCloseWithDelay函数的功能：
// （1）（TcpConnection::forceClose函数功能：
//    服务端执行此函数：服务端主动关闭，和，客户端建立的连接
//    客户端执行此函数：客户端主动关闭，和，服务端建立的连接
// （2）以当前时间Timestamp::now()为起点，经过delay这么长的时间后，调用TcpConnection::forceClose函数
void TcpConnection::forceCloseWithDelay(double seconds)
{
    /// 客户端与服务端之间，所建立的连接的，状态，为：
    // kConnected：服务端和客户端之间的连接已经建立完毕
    // kDisconnecting：正在关闭服务端和客户端之间的TCP连接
    if (state_ == kConnected || state_ == kDisconnecting)
    {
        // 将服务端进程与客户端进程，所建立的连接的连接状态，改为kDisconnecting状态（连接已关闭）
        setState(kDisconnecting);
        /// runAfter函数参数含义：
        /// double delay：定时器的超时时间
        /// TimerCallback &&cb：定时器的回调函数
        /// 函数功能：
        /// 以当前时间Timestamp::now()为起点，经过delay这么长的时间后，定时器超时，并调用定时器回调函数
        loop_->runAfter(
            seconds,
            makeWeakCallback(shared_from_this(),
                             // TcpConnection::forceClose函数功能：
                             // 服务端执行此函数：服务端主动关闭，和，客户端建立的连接
                             // 客户端执行此函数：客户端主动关闭，和，服务端建立的连接
                             &TcpConnection::forceClose));  // not forceCloseInLoop to avoid race condition
    }
}

// 服务端执行此函数：服务端主动关闭，和，客户端建立的连接
// 客户端执行此函数：客户端主动关闭，和，服务端建立的连接
void TcpConnection::forceCloseInLoop()
{
    // 确保：执行事件循环（EventLoop::loop()）的线程，是IO线程
    // 即：确保，执行void TcpConnection::forceCloseInLoop()函数的线程，是IO线程
    loop_->assertInLoopThread();

    /// 客户端与服务端之间，所建立的连接的，状态，为：
    // kConnected：服务端和客户端之间的连接已经建立完毕
    // kDisconnecting：正在关闭服务端和客户端之间的TCP连接
    if (state_ == kConnected || state_ == kDisconnecting)
    {
        // as if we received 0 byte in handleRead();
        // （1）第一个作用
        // 服务端进程，从套接字socket_(其内部成员变量：sockfd_)中未读取到，客户端进程发来的数据
        // 意味着：客户端进程，主动关闭了TCP连接，
        // 因此，服务端进程，需要执行handleClose函数，进行被动关闭TCP连接
        // （2）第二个作用
        // 客户端进程，从套接字socket_(其内部成员变量：sockfd_)中未读取到，服务端进程发来的数据
        // 意味着：服务端进程，主动关闭了TCP连接，
        // 因此，客户端进程，需要执行handleClose函数，进行被动关闭TCP连接
        handleClose();
    }
}

const char *TcpConnection::stateToString() const
{
    switch (state_)
    {
    case kDisconnected:
        return "kDisconnected";
    case kConnecting:
        return "kConnecting";
    case kConnected:
        return "kConnected";
    case kDisconnecting:
        return "kDisconnecting";
    default:
        return "unknown state";
    }
}

void TcpConnection::setTcpNoDelay(bool on)
{
    socket_->setTcpNoDelay(on);
}

void TcpConnection::startRead()
{
    loop_->runInLoop(boost::bind(&TcpConnection::startReadInLoop, this));
}

void TcpConnection::startReadInLoop()
{
    loop_->assertInLoopThread();
    if (!reading_ || !channel_->isReading())
    {
        channel_->enableReading();
        reading_ = true;
    }
}

void TcpConnection::stopRead()
{
    loop_->runInLoop(boost::bind(&TcpConnection::stopReadInLoop, this));
}

void TcpConnection::stopReadInLoop()
{
    loop_->assertInLoopThread();
    if (reading_ || channel_->isReading())
    {
        channel_->disableReading();
        reading_ = false;
    }
}

/// 服务端执行这个函数：使客户端和服务端，真正建立起连接
/// 在channel_管理的socket文件描述符上注册读事件，并在pollfds_表（相当于epoll的内核事件表）中新增一个表项
/// 实现：服务端进程，使用poll函数，监测channel_管理的socket文件描述符上是否有读事件发生
/// 读事件：服务端进程，接收到客户端进程发来的数据
/// 并执行，连接回调函数connectionCallback_，通知客户端进程，连接建立成功
void TcpConnection::connectEstablished()
{
    // 确保：执行事件循环（EventLoop::loop()）的线程，是IO线程
    // 即：确保，执行void TcpConnection::connectEstablished()函数的线程，是IO线程
    loop_->assertInLoopThread();
    // 服务端进程与客户端进程，所建立的连接的连接状态，在连接建立之前，为kConnecting正在建立连接状态
    assert(state_ == kConnecting);

    // 将服务端进程与客户端进程，所建立的连接的连接状态，改为kConnected状态（连接已建立）
    setState(kConnected);
    channel_->tie(shared_from_this());

    /// 在epoll的内核事件监听表中，注册class Channel类，所管理的文件描述符fd_;
    /// 并让epoll_wait关注其上是否有读事件发生
    // ===================================================================================================
    /// channel_管理的socket文件描述符，是服务端程序，使用accept函数新创建的
    /// 所以，此处要执行如下操作：
    /// 在channel_管理的socket文件描述符上注册读事件，并在pollfds_表（相当于epoll的内核事件表）中新增一个表项
    /// 实现：服务端进程，使用poll函数，监测channel_管理的socket文件描述符上是否有读事件发生
    /// 读事件：服务端进程，接收到客户端进程发来的数据
    /// 完成这步操作后，客户端进程与服务端进程，才真正建立起连接
    channel_->enableReading();

    // 连接回调函数connectionCallback_，的作用：
    // （1）第一个作用
    // 服务端进程，执行void connectEstablished()函数，使得客户端进程与服务端进程，真正建立起连接后，
    // 服务端进程，会执行，该连接回调函数connectionCallback_，通知客户端进程，连接建立成功
    // 同时，可以在这个函数里，实现，将数据，由服务端发送到客户端
    // （2）第二个作用
    // 服务端进程，执行void TcpConnection::connectDestroyed()函数，使得客户端进程与服务端进程，彻底断开连接后，
    // 服务端进程，会执行，该连接回调函数connectionCallback_，通知客户端进程，连接已成功断开
    // （3）第三个作用
    // 客户端进程，执行void connectEstablished()函数，使得客户端进程与服务端进程，真正建立起连接后，
    // 客户端进程，会执行，该连接回调函数connectionCallback_，通知服务端进程，连接建立成功
    // 同时，可以在这个函数里，实现，将数据，由客户端发送到服务端
    // （4）第四个作用
    // 客户端进程，执行void TcpConnection::connectDestroyed()函数，使得客户端进程与服务端进程，彻底断开连接后，
    // 客户端进程，会执行，该连接回调函数connectionCallback_，通知服务端进程，连接已成功断开
    connectionCallback_(shared_from_this());
}

// （1）服务端进程，执行此函数：彻底断开客户端与服务端建立的TCP连接
// （2）客户端进程，执行此函数：彻底断开客户端与服务端建立的TCP连接
void TcpConnection::connectDestroyed()
{
    // 确保：执行事件循环（EventLoop::loop()）的线程，是IO线程
    // 即：确保，执行void TcpConnection::connectDestroyed()函数的线程，是IO线程
    loop_->assertInLoopThread();
    // 服务端进程与客户端进程，所建立的连接的连接状态，在连接断开之前，为kConnected已连接状态
    if (state_ == kConnected)
    {
        // 将服务端进程与客户端进程，所建立的连接的连接状态，改为kDisconnected状态（连接已断开）
        setState(kDisconnected);

        // （1）socket_的作用：
        //      1.1）第一个作用
        //           服务端进程，调用accept函数从处于监听状态的套接字的客户端进程连接请求队列中取出排在最前面的一个客户连接请求，
        //           并且服务端进程，会创建一个新的套接字，来与客户端进程的套接字，创建连接通道
        //           socket_：管理（用其内部成员变量保存）服务端进程新创建的套接字的socket文件描述符
        //      1.2）第二个作用
        //           客户端进程，创建一个非阻塞的socket，用于与服务端进程进行通信
        //           客户端进程调用connect函数，来使客户端进程的套接字sockfd与服务端进程的套接字进行连接
        //           socket_：管理（用其内部成员变量保存）客户端进程新创建的套接字的socket文件描述符sockfd
        // （2）channel_的作用：
        //      2.1）第一个作用
        //           管理socket_（其内部成员变量）：服务端进程新创建的套接字的socket文件描述符
        //      2.2）第二个作用
        //           管理socket_（其内部成员变量）：客户端进程新创建的套接字的socket文件描述符
        // （3）channel_->disableAll();这句话的作用：
        //      3.1）第一个作用
        //           不再关注，channel_所管理的服务端进程新创建的套接字的socket文件描述符上，所发生的任何事件
        //      3.2）第二个作用
        //           不再关注，channel_所管理的客户端进程新创建的套接字的socket文件描述符上，所发生的任何事件
        channel_->disableAll();

        // 执行连接回调函数：
        // （1）第一个作用
        // 服务端进程，执行void TcpConnection::connectDestroyed()函数，使得客户端进程与服务端进程，彻底断开连接后，
        // 服务端进程，会执行，该连接回调函数connectionCallback_，通知客户端进程，连接已成功断开
        // （2）第二个作用
        // 客户端进程，执行void TcpConnection::connectDestroyed()函数，使得客户端进程与服务端进程，彻底断开连接后，
        // 客户端进程，会执行，该连接回调函数connectionCallback_，通知服务端进程，连接已成功断开
        connectionCallback_(shared_from_this());
    }

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
    /// ====================================================================================================
    // （1）第一个作用 -- 服务端进程执行此函数loop_->removeChannel(get_pointer(channel_));时，作用如下：
    // 从pollfds_表（相当于epoll的内核事件表）中，删除channel_这个表项
    // 意味着：服务端进程，不再使用poll函数，监测channel_这个表项中的socket文件描述符上发生的任何事件
    // （2）第二个作用 -- 客户端进程执行此函数loop_->removeChannel(get_pointer(channel_));时，作用如下：
    // 从pollfds_表（相当于epoll的内核事件表）中，删除channel_这个表项
    // 意味着：客户端进程，不再使用poll函数，监测channel_这个表项中的socket文件描述符上发生的任何事件
    channel_->remove();
}

// （1）第一个作用
// 套接字socket_(其内部成员变量：sockfd_)，上有可读事件发生时，
// 读事件的事件处理函数为void TcpConnection::handleRead()
// 读事件：服务端进程，接收到客户端进程发来的数据
// 该函数，由服务端进程执行
// （2）第二个作用
// 套接字socket_(其内部成员变量：sockfd_)，上有可读事件发生时，
// 读事件的事件处理函数为void TcpConnection::handleRead()
// 读事件：客户端进程，接收到服务端进程发来的数据
// 该函数，由客户端进程执行
void TcpConnection::handleRead(Timestamp receiveTime)
{
    // 确保：执行事件循环（EventLoop::loop()）的线程，是IO线程
    // 即：确保，执行void TcpConnection::handleRead()函数的线程，是IO线程
    loop_->assertInLoopThread();
    int savedErrno = 0;
    // inputBuffer_输入缓冲区：
    // (1)服务端，用于接收客户端发送过来的数据
    // (2)客户端，用于接收服务端发送过来的数据
    // （1）服务端进程，读取，接收到的客户端进程发来的数据，
    //      并将读取到的数据，存放到inputBuffer_中
    // （2）客户端进程，读取，接收到的服务端进程发来的数据，
    //      并将读取到的数据，存放到inputBuffer_中
    ssize_t n = inputBuffer_.readFd(channel_->fd(), &savedErrno);
    if (n > 0)// （1）服务端进程，从channel_->fd中读取到，客户端进程发来的数据
    {
        // （2）客户端进程，从channel_->fd中读取到，服务端进程发来的数据
        // （1）服务端进程，使用消息回调函数messageCallback_，处理inputBuffer_中存放的，
        //      服务端进程，接收到的客户端进程发来的数据
        // （2）客户端进程，使用消息回调函数messageCallback_，处理inputBuffer_中存放的，
        //      客户端进程，接收到的服务端进程发来的数据
        messageCallback_(shared_from_this(), &inputBuffer_, receiveTime);
    }
    else if (n == 0)// （1）服务端进程，从channel_->fd中未读取到，客户端进程发来的数据
    {
        //      意味着：客户端进程，主动关闭了TCP连接，
        //      因此，服务端进程，需要执行handleClose函数，进行被动关闭TCP连接
        // （2）客户端进程，从channel_->fd中未读取到，服务端进程发来的数据
        //      意味着：服务端进程，主动关闭了TCP连接，
        //      因此，客户端进程，需要执行handleClose函数，进行被动关闭TCP连接

        // （1）第一个作用
        // 服务端进程，从套接字socket_(其内部成员变量：sockfd_)中未读取到，客户端进程发来的数据
        // 意味着：客户端进程，主动关闭了TCP连接，
        // 因此，服务端进程，需要执行handleClose函数，进行被动关闭TCP连接
        // （2）第二个作用
        // 客户端进程，从套接字socket_(其内部成员变量：sockfd_)中未读取到，服务端进程发来的数据
        // 意味着：服务端进程，主动关闭了TCP连接，
        // 因此，客户端进程，需要执行handleClose函数，进行被动关闭TCP连接
        handleClose();
    }
    else
    {
        errno = savedErrno;
        LOG_SYSERR << "TcpConnection::handleRead";
        // （1）服务端进程，从channel_->fd中读取，客户端进程发来的数据时，出现了错误
        // （2）客户端进程，从channel_->fd中读取，服务端进程发来的数据时，出现了错误
        handleError();
    }
}

// （1）第一个作用
// 套接字socket_(其内部成员变量：sockfd_)，上有可写事件发生时，
// 读事件的事件处理函数为void TcpConnection::handleWrite()
// 写事件：服务端进程，准备向客户端进程发送数据
// 该函数，由服务端进程执行
// （2）第二个作用
// 套接字socket_(其内部成员变量：sockfd_)，上有可写事件发生时，
// 读事件的事件处理函数为void TcpConnection::handleWrite()
// 写事件：客户端进程，准备向服务端进程发送数据
// 该函数，由客户端进程执行
void TcpConnection::handleWrite()
{
    // 确保：执行事件循环（EventLoop::loop()）的线程，是IO线程
    // 即：确保，执行void TcpConnection::handleWrite()函数的线程，是IO线程
    loop_->assertInLoopThread();
    if (channel_->isWriting())// 可以发送数据
    {
        // outputBuffer_输出缓冲区：
        // (1)服务端，将需要发送给客户端的数据，存放到这里，然后发送给客户端
        // (2)客户端，将需要发送给服务端的数据，存放到这里，然后发送给服务端
        ssize_t n = sockets::write(channel_->fd(),
                                   outputBuffer_.peek(),
                                   outputBuffer_.readableBytes());
        if (n > 0)// outputBuffer_中存放的剩余数据（sendInLoop函数执行后，未发送完成的数据），发送成功
        {
            outputBuffer_.retrieve(n);
            // outputBuffer_中的所有的数据，都发送完毕
            if (outputBuffer_.readableBytes() == 0)
            {
                /// 在epoll的内核事件监听表中，找到，class Channel类，所管理的文件描述符fd_;
                /// 并让epoll_wait取消关注其上是否有写事件发生
                channel_->disableWriting();
                // 写完成回调函数writeCompleteCallback_的作用：
                // 1.）输出缓冲区outputBuffer_的作用：
                //     (1)服务端，将需要发送给客户端的数据，存放到这里，然后发送给客户端
                //     (2)客户端，将需要发送给服务端的数据，存放到这里，然后发送给服务端
                // 2.）输出缓冲区outputBuffer_中的数据，发送完毕后，会调用这个回调函数，提示数据发送完成
                if (writeCompleteCallback_)
                {
                    /// 将需要在IO线程中执行的用户回调函数writeCompleteCallback_，放入到队列中保存，并在必要时唤醒IO线程，执行这个用户任务回调函数
                    loop_->queueInLoop(boost::bind(writeCompleteCallback_, shared_from_this()));
                }
                // 正在关闭服务端和客户端之间的TCP连接
                if (state_ == kDisconnecting)
                {
                    // 关闭连接
                    shutdownInLoop();
                }
            }
        }
        else
        {
            LOG_SYSERR << "TcpConnection::handleWrite";
            // if (state_ == kDisconnecting)
            // {
            //   shutdownInLoop();
            // }
        }
    }
    else
    {
        LOG_TRACE << "Connection fd = " << channel_->fd()
                  << " is down, no more writing";
    }
}

// （1）第一个作用
// 服务端进程，从套接字socket_(其内部成员变量：sockfd_)中未读取到，客户端进程发来的数据
// 意味着：客户端进程，主动关闭了TCP连接，
// 因此，服务端进程，需要执行handleClose函数，进行被动关闭TCP连接
// （2）第二个作用
// 客户端进程，从套接字socket_(其内部成员变量：sockfd_)中未读取到，服务端进程发来的数据
// 意味着：服务端进程，主动关闭了TCP连接，
// 因此，客户端进程，需要执行handleClose函数，进行被动关闭TCP连接
void TcpConnection::handleClose()
{
    // 确保：执行事件循环（EventLoop::loop()）的线程，是IO线程
    // 即：确保，执行void TcpConnection::handleClose()函数的线程，是IO线程
    loop_->assertInLoopThread();
    LOG_TRACE << "fd = " << channel_->fd() << " state = " << stateToString();
    assert(state_ == kConnected || state_ == kDisconnecting);
    // we don't close fd, leave it to dtor, so we can find leaks easily.
    setState(kDisconnected);
    // （1）socket_的作用：
    //      1.1）第一个作用
    //           服务端进程，调用accept函数从处于监听状态的套接字的客户端进程连接请求队列中取出排在最前面的一个客户连接请求，
    //           并且服务端进程，会创建一个新的套接字，来与客户端进程的套接字，创建连接通道
    //           socket_：管理（用其内部成员变量保存）服务端进程新创建的套接字的socket文件描述符
    //      1.2）第二个作用
    //           客户端进程，创建一个非阻塞的socket，用于与服务端进程进行通信
    //           客户端进程调用connect函数，来使客户端进程的套接字sockfd与服务端进程的套接字进行连接
    //           socket_：管理（用其内部成员变量保存）客户端进程新创建的套接字的socket文件描述符sockfd
    // （2）channel_的作用：
    //      2.1）第一个作用
    //           管理socket_（其内部成员变量）：服务端进程新创建的套接字的socket文件描述符
    //      2.2）第二个作用
    //           管理socket_（其内部成员变量）：客户端进程新创建的套接字的socket文件描述符
    // （3）channel_->disableAll();这句话的作用：
    //      3.1）第一个作用
    //           不再关注，channel_所管理的服务端进程新创建的套接字的socket文件描述符上，所发生的任何事件
    //      3.2）第二个作用
    //           不再关注，channel_所管理的客户端进程新创建的套接字的socket文件描述符上，所发生的任何事件
    channel_->disableAll();

    TcpConnectionPtr guardThis(shared_from_this());
    connectionCallback_(guardThis);
    // must be the last line
    // 被动关闭TCP连接回调函数，的作用：
    // （1）第一个作用
    // 作为，服务端进程，被动关闭TCP连接回调函数，的作用：
    // 服务端进程，从套接字socket_(其内部成员变量：sockfd_)中未读取到，客户端进程发来的数据
    // 意味着：客户端进程，主动关闭了TCP连接，
    // 因此，服务端进程，需要执行closeCallback_函数，进行被动关闭TCP连接
    // （2）第二个作用
    // 作为，客户端进程，被动关闭TCP连接回调函数，的作用：
    // 客户端进程，从套接字socket_(其内部成员变量：sockfd_)中未读取到，服务端进程发来的数据
    // 意味着：服务端进程，主动关闭了TCP连接，
    // 因此，客户端进程，需要执行closeCallback_函数，进行被动关闭TCP连接
    closeCallback_(guardThis);
}

// （1）第一个作用
// 服务端进程，调用accept函数从处于监听状态的套接字sockfd的客户端进程连接请求队列中取出排在最前面的一个客户连接请求，
// 并且服务端进程，会创建一个新的套接字，来与客户端进程的套接字，创建连接通道
// 该socket文件描述符sockfd会被加入到pollfds_表（相当于epoll的内核事件表）中，服务端进程会使用poll函数监测其上所发生的事件，
// 当该socket文件描述符sockfd上，发生错误事件POLLERR时，会调用此函数进行处理，
// 此时，该函数，由服务端进程执行
// （2）第二个作用
// 客户端进程创建套接字，与服务端进程进行通信，
// 客户端进程调用connect函数，来使该套接字
// 与服务端进程（客户端进程，要连接的服务端进程的socket地址（IP地址 + 端口号））的套接字进行连接，
// 该socket文件描述符sockfd会被加入到pollfds_表（相当于epoll的内核事件表）中，客户端进程会使用poll函数监测其上所发生的事件，
// 当该socket文件描述符sockfd上，发生错误事件POLLERR时，会调用此函数进行处理
// 此时，该函数，由客户端进程执行
void TcpConnection::handleError()
{
    int err = sockets::getSocketError(channel_->fd());
    LOG_ERROR << "TcpConnection::handleError [" << name_
              << "] - SO_ERROR = " << err << " " << strerror_tl(err);
}