// Copyright 2010, Shuo Chen.  All rights reserved.
// http://code.google.com/p/muduo/
//
// Use of this source code is governed by a BSD-style license
// that can be found in the License file.

// Author: Shuo Chen (chenshuo at chenshuo dot com)
//

#include <muduo/net/Connector.h>

#include <muduo/base/Logging.h>
#include <muduo/net/Channel.h>
#include <muduo/net/EventLoop.h>
#include <muduo/net/SocketsOps.h>

#include <boost/bind.hpp>

#include <errno.h>

using namespace muduo;
using namespace muduo::net;

const int Connector::kMaxRetryDelayMs;

Connector::Connector(EventLoop *loop, const InetAddress &serverAddr)
    : loop_(loop),
      // serverAddr记录：客户端进程调用connect函数，要连接的服务端进程的socket地址（IP地址 + 端口号）
      serverAddr_(serverAddr),
      // 记录：客户端进程与其要连接的服务端进程，不需要建立连接
      connect_(false),
      // state_记录：服务端进程与客户端进程，所建立的连接的连接状态
      // kDisconnected：服务端和客户端之间的TCP连接已关闭
      state_(kDisconnected),
      // retryDelayMs_记录：客户端进程重新创建一个新的socket，再次主动与服务端进程建立连接时，
      //       重试的间隔时间的，起始时间
      retryDelayMs_(kInitRetryDelayMs)
{
    LOG_DEBUG << "ctor[" << this << "]";
}

Connector::~Connector()
{
    LOG_DEBUG << "dtor[" << this << "]";
    assert(!channel_);
}

// 客户端进程主动开始，与服务端进程建立连接：
/// 客户端进程调用connect函数，来使客户端进程的套接字sockfd
/// 与服务端进程serverAddr_（客户端进程，要连接的服务端进程的socket地址（IP地址 + 端口号））的套接字进行连接
void Connector::start()
{
    // 记录：客户端进程与其要连接的服务端进程，需要建立连接
    connect_ = true;
    /// 将需要在IO线程中执行的用户回调函数Connector::startInLoop，放入到队列中保存，并在必要时唤醒IO线程，执行这个用户任务回调函数
    loop_->runInLoop(boost::bind(&Connector::startInLoop, this)); // FIXME: unsafe
}

/// 客户端进程调用connect函数，来使客户端进程的套接字sockfd
/// 与服务端进程serverAddr_（客户端进程，要连接的服务端进程的socket地址（IP地址 + 端口号））的套接字进行连接
void Connector::startInLoop()
{
    // 确保：执行事件循环（EventLoop::loop()）的线程，是IO线程
    // 即：确保，执行void Connector::startInLoop()函数的线程，是IO线程
    // 也就是确保，执行void Connector::startInLoop()函数的线程，是客户端进程中的IO线程
    loop_->assertInLoopThread();
    assert(state_ == kDisconnected);
    // connect_记录：客户端进程与其要连接的服务端进程，需要建立连接
    if (connect_)
    {
        /// 客户端进程调用connect函数，来使客户端进程的套接字sockfd
        /// 与服务端进程serverAddr_（客户端进程，要连接的服务端进程的socket地址（IP地址 + 端口号））的套接字进行连接
        connect();
    }
    else
    {
        LOG_DEBUG << "do not connect";
    }
}

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
void Connector::stop()
{
    // 记录：客户端进程与其要连接的服务端进程，不再需要建立连接
    connect_ = false;
    /// 将需要在IO线程中执行的用户回调函数Connector::stopInLoop，放入到队列中保存，并在必要时唤醒IO线程，执行这个用户任务回调函数
    loop_->queueInLoop(boost::bind(&Connector::stopInLoop, this)); // FIXME: unsafe
    // FIXME: cancel timer
}

void Connector::stopInLoop()
{
    // 确保：执行事件循环（EventLoop::loop()）的线程，是IO线程
    // 即：确保，执行void Connector::stopInLoop()函数的线程，是IO线程
    // 也就是确保，执行void Connector::stopInLoop()函数的线程，是客户端进程中的IO线程
    loop_->assertInLoopThread();
    // kConnecting：正在建立服务端和客户端之间的TCP连接
    if (state_ == kConnecting)
    {
        // 设置客户端和服务端之间的连接状态为--kDisconnected：服务端和客户端之间的TCP连接已关闭
        setState(kDisconnected);
        // （1）不再关注：channel_所管理的文件描述符上，所发生的任何事件
        // （2）从pollfds_表（相当于epoll的内核事件表）中删除表项channel_
        // 实现：客户端进程，在使用poll函数时，不再监测channel_所管理的socket文件描述符上，是否有任何事件发生
        // （3） 清空channel_中保存的，Channel对象指针变量的值
        // 即：channel不再管理任何Channel对象，
        // 也就是，不再管理任何客户端进程创建的socket文件描述符
        int sockfd = removeAndResetChannel();

        // 客户端进程重新创建一个新的socket，再次主动与服务端进程建立连接：
        //（1）新创建一个定时器timerId_：
        // 以系统当前时间为起点，经过retryDelayMs_/1000.0秒后，定时器超时，
        // 执行定时器回调函数Connector::startInLoop，使客户端进程，再次主动与服务端进程建立连接
        // retryDelayMs_记录：客户端进程重新创建一个新的socket，再次主动与服务端进程建立连接时，
        //                    重试的间隔时间的，起始时间
        //（2）客户端进程重新创建一个新的socket，再次主动与服务端进程建立连接时，
        // 重试的间隔时间，按照2倍的方式逐渐延长，即：0.5s, 1s, 2s, 4s, ......，直至kMaxRetryDelayMs（30s）
        retry(sockfd);
    }
}

/// 客户端进程，创建一个非阻塞的socket，用于与服务端进程进行通信
/// 客户端进程调用connect函数，来使客户端进程的套接字sockfd与服务端进程addr的套接字进行连接
void Connector::connect()
{
    /// 客户端进程，创建一个非阻塞的socket，用于与服务端进程进行通信
    int sockfd = sockets::createNonblockingOrDie(serverAddr_.family());
    /// 客户端进程调用connect函数，来使客户端进程的套接字sockfd与服务端进程addr的套接字进行连接
    /// sockfd：客户端进程套接字
    /// addr：客户端进程，要连接的服务端进程的socket地址（IP地址 + 端口号）
    int ret = sockets::connect(sockfd, serverAddr_.getSockAddr());
    int savedErrno = (ret == 0) ? 0 : errno;
    switch (savedErrno)
    {
    case 0:
    case EINPROGRESS:
    case EINTR:
    case EISCONN:
        // （1）让channel_管理新的new Channel(loop_, sockfd) -- socket文件描述符sockfd
        // （2）在channel_所管理的socket文件描述符上注册读事件，并在pollfds_表（相当于epoll的内核事件表）中新增一个表项
        // 实现：客户端进程，使用poll函数，监测channel_所管理的socket文件描述符上是否有写事件发生
        // 写事件：客户进程，准备向服务端进程发送数据
        connecting(sockfd);
        break;

    case EAGAIN:
    case EADDRINUSE:
    case EADDRNOTAVAIL:
    case ECONNREFUSED:
    case ENETUNREACH:
        // 客户端进程重新创建一个新的socket，再次主动与服务端进程建立连接：
        //（1）新创建一个定时器timerId_：
        // 以系统当前时间为起点，经过retryDelayMs_/1000.0秒后，定时器超时，
        // 执行定时器回调函数Connector::startInLoop，使客户端进程，再次主动与服务端进程建立连接
        // retryDelayMs_记录：客户端进程重新创建一个新的socket，再次主动与服务端进程建立连接时，
        //                    重试的间隔时间的，起始时间
        //（2）客户端进程重新创建一个新的socket，再次主动与服务端进程建立连接时，
        // 重试的间隔时间，按照2倍的方式逐渐延长，即：0.5s, 1s, 2s, 4s, ......，直至kMaxRetryDelayMs（30s）
        retry(sockfd);
        break;

    case EACCES:
    case EPERM:
    case EAFNOSUPPORT:
    case EALREADY:
    case EBADF:
    case EFAULT:
    case ENOTSOCK:
        LOG_SYSERR << "connect error in Connector::startInLoop " << savedErrno;
        sockets::close(sockfd);
        break;

    default:
        LOG_SYSERR << "Unexpected error in Connector::startInLoop " << savedErrno;
        sockets::close(sockfd);
        // connectErrorCallback_();
        break;
    }
}

// 使客户端进程，重新与其要连接的服务端进程，建立连接
void Connector::restart()
{
    // 确保：执行事件循环（EventLoop::loop()）的线程，是IO线程
    // 即：确保，执行void Connector::startInLoop()函数的线程，是IO线程
    // 也就是确保，执行void Connector::startInLoop()函数的线程，是客户端进程中的IO线程
    loop_->assertInLoopThread();

    // 设置：服务端进程与客户端进程，所建立的连接的连接状态，为kDisconnected状态
    // kDisconnected：服务端和客户端之间的TCP连接已关闭
    setState(kDisconnected);

    // retryDelayMs_记录：客户端进程重新创建一个新的socket，再次主动与服务端进程建立连接时，
    //                    重试的间隔时间的，起始时间
    retryDelayMs_ = kInitRetryDelayMs;

    // 记录：客户端进程与其要连接的服务端进程，需要建立连接
    connect_ = true;

    /// 客户端进程调用connect函数，来使客户端进程的套接字sockfd
    /// 与服务端进程serverAddr_（客户端进程，要连接的服务端进程的socket地址（IP地址 + 端口号））的套接字进行连接
    startInLoop();
}

// （1）让channel_管理新的new Channel(loop_, sockfd) -- socket文件描述符sockfd
// （2）在channel_所管理的socket文件描述符上注册读事件，并在pollfds_表（相当于epoll的内核事件表）中新增一个表项
// 实现：客户端进程，使用poll函数，监测channel_所管理的socket文件描述符上是否有写事件发生
// 写事件：客户端进程，准备向服务端进程发送新的连接请求
//         客户端进程，执行此函数与服务端进程建立新的连接
void Connector::connecting(int sockfd)
{
    // 设置：服务端进程与客户端进程，所建立的连接的连接状态，为kConnecting状态
    // kConnecting：正在建立服务端和客户端之间的TCP连接
    setState(kConnecting);
    assert(!channel_);
    // （1）boost::scoped_ptr的成员函数reset()的功能是重置scoped_ptr；它删除原来保存的指针变量的值，再保存新的指针变量的值p。
    // 如果p是空指针，那么scoped_ptr将不能持有任何指针变量的值
    // void reset(_Ty * p = 0)  //never throw
    // {
    //     this_type(p).swap(*this);
    // }
    // （2）boost::scoped_ptr<Channel> channel_;// 管理：客户端进程创建的socket文件描述符
    //                                             客户端进程，使用此socket文件描述符，与服务端进程进行通信
    // （3）channel_.reset(new Channel(loop_, sockfd));这句话的作用：
    // 让channel_管理新的new Channel(loop_, sockfd) -- socket文件描述符sockfd
    channel_.reset(new Channel(loop_, sockfd));

    // 套接字sockfd，上有可写事件发生时，
    // 写事件的事件处理函数为Connector::handleWrite
    // 写事件：客户端进程，准备向服务端进程发送新的连接请求
    //         客户端进程，执行此函数与服务端进程建立新的连接
    // 该函数，由客户端进程执行
    channel_->setWriteCallback(
        boost::bind(&Connector::handleWrite, this)); // FIXME: unsafe

    // 写事件的事件处理函数为Connector::handleWrite
    // 写事件：客户端进程，准备向服务端进程发送新的连接请求
    //         客户端进程，执行Connector::handleWrite函数与服务端进程建立新的连接
    // 在建立连接的过程中，如果出现错误，会执行Connector::handleError函数进行处理
    // 该函数，由客户端进程执行
    channel_->setErrorCallback(
        boost::bind(&Connector::handleError, this)); // FIXME: unsafe

    // channel_->tie(shared_from_this()); is not working,
    // as channel_ is not managed by shared_ptr
    // channel_->tie(shared_from_this()); is not working,
    // as channel_ is not managed by shared_ptr
    // 在channel_所管理的socket文件描述符上注册读事件，并在pollfds_表（相当于epoll的内核事件表）中新增一个表项
    // 实现：客户端进程，使用poll函数，监测channel_所管理的socket文件描述符上是否有写事件发生
    // 写事件：客户端进程，准备向服务端进程发送新的连接请求
    //         客户端进程，执行此函数与服务端进程建立新的连接
    channel_->enableWriting();
}

// （1）不再关注：channel_所管理的文件描述符上，所发生的任何事件
// （2）从pollfds_表（相当于epoll的内核事件表）中删除表项channel_
// 实现：客户端进程，在使用poll函数时，不再监测channel_所管理的socket文件描述符上，是否有任何事件发生
// （3） 清空channel_中保存的，Channel对象指针变量的值
// 即：channel不再管理任何Channel对象，
// 也就是，不再管理任何客户端进程创建的socket文件描述符
int Connector::removeAndResetChannel()
{
    // 不再关注：channel_所管理的文件描述符上，所发生的任何事件
    channel_->disableAll();

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
    // =====================================================================================================
    // 从pollfds_表（相当于epoll的内核事件表）中删除表项channel_
    // 实现：客户端进程，在使用poll函数时，不再监测channel_所管理的socket文件描述符上，是否有任何事件发生
    channel_->remove();
    int sockfd = channel_->fd();

    // Can't reset channel_ here, because we are inside Channel::handleEvent
    // （1）把Connector::resetChannel函数，放入到需要
    // 延期执行的用户任务回调函数队列中，并在必要时，唤醒IO线程
    // 执行该函数
    // （2）Connector::resetChannel的作用：
    // 客户端进程，执行此函数：
    // 清空channel_中保存的，Channel对象指针变量的值
    // 即：channel不再管理任何Channel对象，
    // 也就是，不再管理任何客户端进程创建的socket文件描述符
    loop_->queueInLoop(boost::bind(&Connector::resetChannel, this)); // FIXME: unsafe
    return sockfd;
}

// 清空channel_中保存的，Channel指针变量的值
// 即：channel不再管理任何Channel对象，
// 也就是，不再管理任何客户端进程创建的socket文件描述符
void Connector::resetChannel()
{
    // boost::scoped_ptr的成员函数reset()的功能是重置scoped_ptr；它删除原来保存的指针变量的值，再保存新的指针变量的值p。
    // 如果p是空指针，那么scoped_ptr将不能持有任何指针变量的值
    // void reset(_Ty * p = 0)  //never throw
    // {
    //     this_type(p).swap(*this);
    // }
    // ================================================================================================================
    // 清空channel_中保存的，Channel指针变量的值
    // 即：channel不再管理任何Channel对象，
    // 也就是，不再管理任何客户端进程创建的socket文件描述符
    channel_.reset();
}

// 套接字sockfd，上有可写事件发生时，
// 写事件的事件处理函数为Connector::handleWrite
// 写事件：客户端进程，准备向服务端进程发送新的连接请求
//         客户端进程，执行此函数与服务端进程建立新的连接
// 该函数，由客户端进程执行
void Connector::handleWrite()
{
    LOG_TRACE << "Connector::handleWrite " << state_;

    // 服务端进程与客户端进程，所建立的连接的连接状态，为kConnecting状态
    if (state_ == kConnecting)
    {
        // kConnecting：正在建立服务端和客户端之间的TCP连接
        // （1）不再关注：channel_所管理的文件描述符上，所发生的任何事件
        // （2）从pollfds_表（相当于epoll的内核事件表）中删除表项channel_
        // 实现：客户端进程，在使用poll函数时，不再监测channel_所管理的socket文件描述符上，是否有任何事件发生
        // （3） 清空channel_中保存的，Channel对象指针变量的值
        // 即：channel不再管理任何Channel对象，
        // 也就是，不再管理任何客户端进程创建的socket文件描述符
        int sockfd = removeAndResetChannel();
        int err = sockets::getSocketError(sockfd);
        if (err)
        {
            LOG_WARN << "Connector::handleWrite - SO_ERROR = "
                     << err << " " << strerror_tl(err);

            // 客户端进程重新创建一个新的socket，再次主动与服务端进程建立连接：
            //（1）新创建一个定时器timerId_：
            // 以系统当前时间为起点，经过retryDelayMs_/1000.0秒后，定时器超时，
            // 执行定时器回调函数Connector::startInLoop，使客户端进程，再次主动与服务端进程建立连接
            // retryDelayMs_记录：客户端进程重新创建一个新的socket，再次主动与服务端进程建立连接时，
            //                    重试的间隔时间的，起始时间
            //（2）客户端进程重新创建一个新的socket，再次主动与服务端进程建立连接时，
            // 重试的间隔时间，按照2倍的方式逐渐延长，即：0.5s, 1s, 2s, 4s, ......，直至kMaxRetryDelayMs（30s）
            retry(sockfd);
        }
        else if (sockets::isSelfConnect(sockfd))
        {
            LOG_WARN << "Connector::handleWrite - Self connect";

            // 客户端进程重新创建一个新的socket，再次主动与服务端进程建立连接：
            //（1）新创建一个定时器timerId_：
            // 以系统当前时间为起点，经过retryDelayMs_/1000.0秒后，定时器超时，
            // 执行定时器回调函数Connector::startInLoop，使客户端进程，再次主动与服务端进程建立连接
            // retryDelayMs_记录：客户端进程重新创建一个新的socket，再次主动与服务端进程建立连接时，
            //                    重试的间隔时间的，起始时间
            //（2）客户端进程重新创建一个新的socket，再次主动与服务端进程建立连接时，
            // 重试的间隔时间，按照2倍的方式逐渐延长，即：0.5s, 1s, 2s, 4s, ......，直至kMaxRetryDelayMs（30s）
            retry(sockfd);
        }
        else
        {
            // 设置：服务端进程与客户端进程，所建立的连接的连接状态，为kConnected状态
            // kConnected：服务端和客户端之间的TCP连接已建立
            setState(kConnected);
            // connect_记录：客户端进程与其要连接的服务端进程，需要建立连接
            if (connect_)
            {
                // 客户端进程，执行新连接回调函数newConnectionCallback_
                // 本质上，执行的是void TcpClient::newConnection(int sockfd)
                // 实现：使客户端进程，与服务端进程（客户端进程，要连接的服务端进程的socket地址（IP地址 + 端口号））的套接字彻底建立连接
                newConnectionCallback_(sockfd);
            }
            else
            {
                sockets::close(sockfd);
            }
        }
    }
    else// 服务端进程与客户端进程，所建立的连接的连接状态，为kDisconnected状态
    {
        // what happened?
        // kDisconnected：服务端和客户端之间的TCP连接已关闭
        assert(state_ == kDisconnected);
    }
}

// 写事件的事件处理函数为Connector::handleWrite
// 写事件：客户端进程，准备向服务端进程发送新的连接请求
//         客户端进程，执行Connector::handleWrite函数与服务端进程建立新的连接
// 在建立连接的过程中，如果出现错误，会执行Connector::handleError函数进行处理
// 该函数，由客户端进程执行
void Connector::handleError()
{
    LOG_ERROR << "Connector::handleError state=" << state_;
    if (state_ == kConnecting)
    {
        // （1）不再关注：channel_所管理的文件描述符上，所发生的任何事件
        // （2）从pollfds_表（相当于epoll的内核事件表）中删除表项channel_
        // 实现：客户端进程，在使用poll函数时，不再监测channel_所管理的socket文件描述符上，是否有任何事件发生
        // （3） 清空channel_中保存的，Channel对象指针变量的值
        // 即：channel不再管理任何Channel对象，
        // 也就是，不再管理任何客户端进程创建的socket文件描述符
        int sockfd = removeAndResetChannel();
        int err = sockets::getSocketError(sockfd);
        LOG_TRACE << "SO_ERROR = " << err << " " << strerror_tl(err);
        // 客户端进程重新创建一个新的socket，再次主动与服务端进程建立连接：
        //（1）新创建一个定时器timerId_：
        // 以系统当前时间为起点，经过retryDelayMs_/1000.0秒后，定时器超时，
        // 执行定时器回调函数Connector::startInLoop，使客户端进程，再次主动与服务端进程建立连接
        // retryDelayMs_记录：客户端进程重新创建一个新的socket，再次主动与服务端进程建立连接时，
        //                    重试的间隔时间的，起始时间
        //（2）客户端进程重新创建一个新的socket，再次主动与服务端进程建立连接时，
        // 重试的间隔时间，按照2倍的方式逐渐延长，即：0.5s, 1s, 2s, 4s, ......，直至kMaxRetryDelayMs（30s）
        retry(sockfd);
    }
}

// 客户端进程重新创建一个新的socket，再次主动与服务端进程建立连接：
//（1）新创建一个定时器timerId_：
// 以系统当前时间为起点，经过retryDelayMs_/1000.0秒后，定时器超时，
// 执行定时器回调函数Connector::startInLoop，使客户端进程，再次主动与服务端进程建立连接
// retryDelayMs_记录：客户端进程重新创建一个新的socket，再次主动与服务端进程建立连接时，
//                    重试的间隔时间的，起始时间
//（2）客户端进程重新创建一个新的socket，再次主动与服务端进程建立连接时，
// 重试的间隔时间，按照2倍的方式逐渐延长，即：0.5s, 1s, 2s, 4s, ......，直至kMaxRetryDelayMs（30s）
void Connector::retry(int sockfd)
{
    // 关闭，上次客户端进程与服务端进程进行通信的socket --sockfd
    // 这样做的原因：
    // 因为，上次客户端进程，主动与服务端进程进行建立连接时，连接建立失败了
    // 而socket是一次性的，一旦出错（比如：对方拒绝连接），就无法恢复，只能关闭重来
    // 所以，只能关闭，上次客户端进程与服务端进程进行通信的socket，
    // 并且，客户端进程需要重新创建一个新的socket，再次主动与服务端进程建立连接
    sockets::close(sockfd);
    // 设置：服务端进程与客户端进程，所建立的连接的连接状态，为kDisconnected状态
    // kDisconnected：服务端和客户端之间的TCP连接已关闭
    setState(kDisconnected);
    // connect_记录：客户端进程与其要连接的服务端进程，需要建立连接
    if (connect_)
    {
        LOG_INFO << "Connector::retry - Retry connecting to " << serverAddr_.toIpPort()
                 << " in " << retryDelayMs_ << " milliseconds. ";
        // 新创建一个定时器：
        // 以系统当前时间为起点，经过retryDelayMs_/1000.0秒后，定时器超时，
        // 执行定时器回调函数Connector::startInLoop，使客户端进程，再次主动与服务端进程建立连接
        // retryDelayMs_记录：客户端进程重新创建一个新的socket，再次主动与服务端进程建立连接时，
        //                    重试的间隔时间的，起始时间
        loop_->runAfter(retryDelayMs_ / 1000.0,
                        boost::bind(&Connector::startInLoop, shared_from_this()));

        // 客户端进程重新创建一个新的socket，再次主动与服务端进程建立连接时，
        // 重试的间隔时间，按照2倍的方式逐渐延长，即：0.5s, 1s, 2s, 4s, ......，直至kMaxRetryDelayMs（30s）
        retryDelayMs_ = std::min(retryDelayMs_ * 2, kMaxRetryDelayMs);
    }
    else
    {
        LOG_DEBUG << "do not connect";
    }
}

