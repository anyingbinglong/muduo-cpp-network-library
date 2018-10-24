// Copyright 2010, Shuo Chen.  All rights reserved.
// http://code.google.com/p/muduo/
//
// Use of this source code is governed by a BSD-style license
// that can be found in the License file.

// Author: Shuo Chen (chenshuo at chenshuo dot com)
//
// This is a public header file, it must only include public header files.

#ifndef MUDUO_NET_TCPCONNECTION_H
#define MUDUO_NET_TCPCONNECTION_H

#include <muduo/base/StringPiece.h>
#include <muduo/base/Types.h>
#include <muduo/net/Callbacks.h>
#include <muduo/net/Buffer.h>
#include <muduo/net/InetAddress.h>

#include <boost/any.hpp>
#include <boost/enable_shared_from_this.hpp>
#include <boost/noncopyable.hpp>
#include <boost/scoped_ptr.hpp>
#include <boost/shared_ptr.hpp>

// struct tcp_info is in <netinet/tcp.h>
struct tcp_info;

namespace muduo
{
    namespace net
    {

        class Channel;
        class EventLoop;
        class Socket;

        ///
        /// TCP connection, for both client and server usage.
        ///
        /// This is an interface class, so don't expose too much details.
        // 这个类的作用：
        // （1）管理客户端和服务端之间，建立的，TCP连接
        // （2）这个类所创建的一个对象，就是一个，TCP连接管理对象，这个对象中，保存着这个TCP连接的相关信息
        class TcpConnection : boost::noncopyable,
            public boost::enable_shared_from_this<TcpConnection>
        {
        public:
            /// Constructs a TcpConnection with a connected sockfd
            ///
            /// User should not create this object.
            TcpConnection(EventLoop *loop,
                          const string &name,
                          int sockfd,
                          const InetAddress &localAddr,
                          const InetAddress &peerAddr);
            ~TcpConnection();

            EventLoop *getLoop() const
            {
                return loop_;
            }
            const string &name() const
            {
                return name_;
            }
            const InetAddress &localAddress() const
            {
                return localAddr_;
            }
            const InetAddress &peerAddress() const
            {
                return peerAddr_;
            }
            bool connected() const
            {
                return state_ == kConnected;
            }
            bool disconnected() const
            {
                return state_ == kDisconnected;
            }
            // return true if success.
            bool getTcpInfo(struct tcp_info *) const;
            string getTcpInfoString() const;

            // void send(string&& message); // C++11
            // 函数参数含义：
            //    const void *data：需要发送的数据，存放到这里了
            //    size_t len：需要发送的数据的总长度
            // 函数功能：
            //  （1）客户端执行这个函数，将buf中的数据，发送给服务端
            //  （2）服务端执行这个函数，将buf中的数据，发送给客户端
            void send(const void *message, int len);

            // 函数参数含义：
            //    const StringPiece &message：需要发送的数据，都存放都这里了
            // 函数功能：
            //  （1）客户端执行这个函数，将buf中的数据，发送给服务端
            //  （2）服务端执行这个函数，将buf中的数据，发送给客户端
            void send(const StringPiece &message);

            // void send(Buffer&& message); // C++11
            // FIXME efficiency!!!
            // 函数参数含义：
            //    Buffer *buf：需要发送的数据，都存放都这里了
            // 函数功能：
            //  （1）客户端执行这个函数，将buf中的数据，发送给服务端
            //  （2）服务端执行这个函数，将buf中的数据，发送给客户端
            void send(Buffer *message);  // this one will swap data

            // （1）设置：服务端进程与客户端进程，所建立的连接的连接状态
            // 为：kDisconnecting，正在关闭服务端和客户端之间的TCP连接，状态
            // （2）关闭socket_上的写的这一半，应用程序不可再对该socket_执行写操作
            // socket_的作用：
            // 服务端进程，调用accept函数从处于监听状态的套接字的客户端进程连接请求队列中取出排在最前面的一个客户连接请求，
            // 并且服务端进程，会创建一个新的套接字，来与客户端进程的套接字，创建连接通道
            // socket_的作用，就是管理（用其内部成员变量保存）服务端进程新创建的套接字的socket文件描述符
            void shutdown(); // NOT thread safe, no simultaneous calling

            // void shutdownAndForceCloseAfter(double seconds); // NOT thread safe, no simultaneous calling
            // 服务端执行此函数：服务端主动关闭，和，客户端建立的连接
            // 客户端执行此函数：客户端主动关闭，和，服务端建立的连接
            void forceClose();
            
            // 函数参数含义：
            // double seconds：定时器的超时时间
            // TcpConnection::forceCloseWithDelay函数的功能：
            // （1）（TcpConnection::forceClose函数功能：
            //    服务端执行此函数：服务端主动关闭，和，客户端建立的连接
            //    客户端执行此函数：客户端主动关闭，和，服务端建立的连接
            // （2）以当前时间Timestamp::now()为起点，经过delay这么长的时间后，调用TcpConnection::forceClose函数
            void forceCloseWithDelay(double seconds);
            void setTcpNoDelay(bool on);
            // reading or not
            void startRead();
            void stopRead();
            bool isReading() const
            {
                return reading_;
            }; // NOT thread safe, may race with start/stopReadInLoop

            void setContext(const boost::any &context)
            {
                context_ = context;
            }

            const boost::any &getContext() const
            {
                return context_;
            }

            boost::any *getMutableContext()
            {
                return &context_;
            }

            void setConnectionCallback(const ConnectionCallback &cb)
            {
                connectionCallback_ = cb;
            }

            void setMessageCallback(const MessageCallback &cb)
            {
                messageCallback_ = cb;
            }

            void setWriteCompleteCallback(const WriteCompleteCallback &cb)
            {
                writeCompleteCallback_ = cb;
            }

            void setHighWaterMarkCallback(const HighWaterMarkCallback &cb, size_t highWaterMark)
            {
                highWaterMarkCallback_ = cb;
                highWaterMark_ = highWaterMark;
            }

            /// Advanced interface
            Buffer *inputBuffer()
            {
                return &inputBuffer_;
            }

            Buffer *outputBuffer()
            {
                return &outputBuffer_;
            }

            /// Internal use only.
            void setCloseCallback(const CloseCallback &cb)
            {
                closeCallback_ = cb;
            }

            // called when TcpServer accepts a new connection
            // 服务端执行这个函数：使客户端和服务端，真正建立起连接
            void connectEstablished();   // should be called only once

            // called when TcpServer has removed me from its map
            // （1）服务端进程，执行此函数：彻底断开客户端与服务端建立的TCP连接
            // （2）客户端进程，执行此函数：彻底断开客户端与服务端建立的TCP连接
            void connectDestroyed();  // should be called only once

        private:
            /// 客户端与服务端之间，所建立的连接的，状态
            enum StateE
            {
                // 正在建立服务端和客户端之间的TCP连接
                kConnecting,
                // 服务端和客户端之间的连接已经建立完毕
                kConnected,
                // 正在关闭服务端和客户端之间的TCP连接
                kDisconnecting
                // 服务端和客户端之间的TCP连接已关闭
                kDisconnected,
            };

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
            void handleRead(Timestamp receiveTime);

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
            void handleWrite();

            // （1）第一个作用
            // 服务端进程，从套接字socket_(其内部成员变量：sockfd_)中未读取到，客户端进程发来的数据
            // 意味着：客户端进程，主动关闭了TCP连接，
            // 因此，服务端进程，需要执行handleClose函数，进行被动关闭TCP连接
            // （2）第二个作用
            // 客户端进程，从套接字socket_(其内部成员变量：sockfd_)中未读取到，服务端进程发来的数据
            // 意味着：服务端进程，主动关闭了TCP连接，
            // 因此，客户端进程，需要执行handleClose函数，进行被动关闭TCP连接
            void handleClose();

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
            void handleError();
            // void sendInLoop(string&& message);

            // 函数参数的含义：
            //    const StringPiece &message：需要发送的数据，都存放都这里了
            // 函数功能：
            //  （1）客户端执行这个函数，将data中的数据，发送给服务端
            //  （2）服务端执行这个函数，将data中的数据，发送给客户端
            void sendInLoop(const StringPiece &message);

            // 函数参数的含义：
            //    const void *data：需要发送的数据，存放到这里了
            //    size_t len：需要发送的数据的总长度
            // 函数功能：
            //  （1）客户端执行这个函数，将data中的数据，发送给服务端
            //  （2）服务端执行这个函数，将data中的数据，发送给客户端
            void sendInLoop(const void *message, size_t len);

            // （1）设置：服务端进程与客户端进程，所建立的连接的连接状态
            // 为：kDisconnecting，正在关闭服务端和客户端之间的TCP连接，状态
            // （2）关闭socket_上的写的这一半，应用程序不可再对该socket_执行写操作
            // socket_的作用：
            // 服务端进程，调用accept函数从处于监听状态的套接字的客户端进程连接请求队列中取出排在最前面的一个客户连接请求，
            // 并且服务端进程，会创建一个新的套接字，来与客户端进程的套接字，创建连接通道
            // socket_的作用，就是管理（用其内部成员变量保存）服务端进程新创建的套接字的socket文件描述符
            void shutdownInLoop();
            // void shutdownAndForceCloseInLoop(double seconds);
            // 服务端执行此函数：服务端主动关闭，和，客户端建立的连接
            // 客户端执行此函数：客户端主动关闭，和，服务端建立的连接
            void forceCloseInLoop();
            void setState(StateE s)
            {
                state_ = s;
            }
            const char *stateToString() const;
            void startReadInLoop();
            void stopReadInLoop();

            EventLoop *loop_;
            const string name_;

            /// 记录：客户端与服务端之间，所建立的连接的，状态
            StateE state_;  // FIXME: use atomic variable
            bool reading_;

            // we don't expose those classes to client.
            // （1）第一个作用
            // 服务端进程，调用accept函数从处于监听状态的套接字的客户端进程连接请求队列中取出排在最前面的一个客户连接请求，
            // 并且服务端进程，会创建一个新的套接字，来与客户端进程的套接字，创建连接通道
            // socket_：管理（用其内部成员变量保存）服务端进程新创建的套接字的socket文件描述符
            // （2）第二个作用
            // 客户端进程，创建一个非阻塞的socket，用于与服务端进程进行通信
            // 客户端进程调用connect函数，来使客户端进程的套接字sockfd与服务端进程的套接字进行连接
            // socket_：管理（用其内部成员变量保存）客户端进程新创建的套接字的socket文件描述符sockfd
            boost::scoped_ptr<Socket> socket_;

            // （1）第一个作用
            // 管理socket_（其内部成员变量）：服务端进程新创建的套接字的socket文件描述符
            // （2）第二个作用
            // 管理socket_（其内部成员变量）：客户端进程新创建的套接字的socket文件描述符
            boost::scoped_ptr<Channel> channel_;

            // （1）第一个作用
            // 存放服务端进程新创建的套接字的socket地址（IP地址 + 端口号）
            // （2）第二个作用
            // 存放客户端进程新创建的套接字的socket地址（IP地址 + 端口号）
            const InetAddress localAddr_;

            // （1）第一个作用
            // 存放服务端进程新创建的套接字的socket地址（IP地址 + 端口号）
            // （2）第二个作用
            // 存放客户端进程新创建的套接字的socket地址（IP地址 + 端口号）
            const InetAddress peerAddr_;

            // 连接回调函数，的作用：
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
            ConnectionCallback connectionCallback_;

            // 消息回调函数的作用：
            // （1）第一个作用
            // 服务端进程，读取，接收到的客户端进程发来的数据后，
            // 服务端进程，会在这个函数中，处理，接收到的客户端进程发来的数据
            // （2）第二个作用
            // 客户端进程，读取，接收到的服务端进程发来的数据后，
            // 客户端进程，会在这个函数中，处理，接收到的服务端进程发来的数据
            MessageCallback messageCallback_;

            // 写完成回调函数的作用：
            // 1.）输出缓冲区outputBuffer_的作用：
            //     (1)服务端，将需要发送给客户端的数据，存放到这里，然后发送给客户端
            //     (2)客户端，将需要发送给服务端的数据，存放到这里，然后发送给服务端
            // 2.）输出缓冲区outputBuffer_中的数据，发送完毕后，会调用这个回调函数，提示数据发送完成
            WriteCompleteCallback writeCompleteCallback_;

            // 高水位回调函数的作用：
            // 1.）输出缓冲区outputBuffer_的作用：
            //     (1)服务端，将需要发送给客户端的数据，存放到这里，然后发送给客户端
            //     (2)客户端，将需要发送给服务端的数据，存放到这里，然后发送给服务端
            // 2.）输出缓冲区outputBuffer_中的待发送数据的长度（可读数据的长度：outputBuffer_.readableBytes()的返回值），
            //    超过用户指定的大小highWaterMark_，就会调用这个函数，提示发送的数据的数量太大
            HighWaterMarkCallback highWaterMarkCallback_;

            // 充当，被动关闭TCP连接回调函数，的作用：
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
            // =====================================================================================
            // 充当，主动关闭TCP连接回调函数，的作用：
            // （1）第一个作用
            // 作为，服务端进程，主动关闭TCP连接回调函数，的作用：
            // 服务端进程，想主动关闭与客户端进程，建立的连接
            // 因此，服务端进程，可以执行closeCallback_函数，进行主动关闭TCP连接
            // （2）第二个作用
            // 作为，客户端进程，主动关闭TCP连接回调函数，的作用：
            // 客户端进程，想主动关闭与服务端进程，建立的连接
            // 因此，客户端进程，可以执行closeCallback_函数，进行主动关闭TCP连接
            CloseCallback closeCallback_;

            // 输出缓冲区outputBuffer_中的待发送数据的长度（可读数据的长度：outputBuffer_.readableBytes()的返回值）的，最大值
            size_t highWaterMark_;

            // 输入缓冲区：
            // (1)服务端，用于接收客户端发送过来的数据
            // (2)客户端，用于接收服务端发送过来的数据
            Buffer inputBuffer_;

            // 输出缓冲区：
            // (1)服务端，将需要发送给客户端的数据，存放到这里，然后发送给客户端
            // (2)客户端，将需要发送给服务端的数据，存放到这里，然后发送给服务端
            Buffer outputBuffer_; // FIXME: use list<Buffer> as output buffer.

            // 相当于java netty中的ChannelHandlerContext
            // 在这里，实现对，收到的数据，进行进一步处理
            // 可以看下http文件夹中的代码，进行理解
            boost::any context_;
        };

        typedef boost::shared_ptr<TcpConnection> TcpConnectionPtr;
    }
}

#endif  // MUDUO_NET_TCPCONNECTION_H
