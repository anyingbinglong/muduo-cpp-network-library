// Copyright 2010, Shuo Chen.  All rights reserved.
// http://code.google.com/p/muduo/
//
// Use of this source code is governed by a BSD-style license
// that can be found in the License file.

// Author: Shuo Chen (chenshuo at chenshuo dot com)

#include <muduo/net/EventLoop.h>

#include <muduo/base/Logging.h>
#include <muduo/base/Mutex.h>
#include <muduo/net/Channel.h>
#include <muduo/net/Poller.h>
#include <muduo/net/SocketsOps.h>
#include <muduo/net/TimerQueue.h>

#include <boost/bind.hpp>

#include <signal.h>
#include <sys/eventfd.h>
#include <unistd.h>

using namespace muduo;
using namespace muduo::net;

namespace
{
    __thread EventLoop *t_loopInThisThread = 0;

    const int kPollTimeMs = 10000;

    int createEventfd()
    {
        int evtfd = ::eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
        if (evtfd < 0)
        {
            LOG_SYSERR << "Failed in eventfd";
            abort();
        }
        return evtfd;
    }

#pragma GCC diagnostic ignored "-Wold-style-cast"
    class IgnoreSigPipe
    {
    public:
        IgnoreSigPipe()
        {
            ::signal(SIGPIPE, SIG_IGN);
            // LOG_TRACE << "Ignore SIGPIPE";
        }
    };
#pragma GCC diagnostic error "-Wold-style-cast"

    IgnoreSigPipe initObj;
}

EventLoop *EventLoop::getEventLoopOfCurrentThread()
{
    return t_loopInThisThread;
}

EventLoop::EventLoop()
    : looping_(false),
      quit_(false),
      eventHandling_(false),
      callingPendingFunctors_(false),
      iteration_(0),
      /// 记录：IO线程的ID
      threadId_(CurrentThread::tid()),
      poller_(Poller::newDefaultPoller(this)),
      timerQueue_(new TimerQueue(this)),
      wakeupFd_(createEventfd()),
      wakeupChannel_(new Channel(this, wakeupFd_)),
      currentActiveChannel_(NULL)
{
    LOG_DEBUG << "EventLoop created " << this << " in thread " << threadId_;
    /// 当前线程，已经创建了其他的EventLoop对象
    if (t_loopInThisThread)
    {
        LOG_FATAL << "Another EventLoop " << t_loopInThisThread
                  << " exists in this thread " << threadId_;
    }
    else/// 当前线程，未创建其他的EventLoop对象
    {
        /// 记录线程内部唯一的一个EventLoop对象
        t_loopInThisThread = this;
    }
    /// 设置：class Channel类对象wakeupChannel_，所管理的文件描述符fd_;上，有读事件发生时，需要调用的读事件处理函数
    wakeupChannel_->setReadCallback(
        boost::bind(&EventLoop::handleRead, this));
    // we are always reading the wakeupfd
    /// 在epoll的内核事件监听表中，注册class Channel类，所管理的文件描述符fd_;
    /// 并让epoll_wait关注其上是否有读事件发生
    wakeupChannel_->enableReading();
}

EventLoop::~EventLoop()
{
    LOG_DEBUG << "EventLoop " << this << " of thread " << threadId_
              << " destructs in thread " << CurrentThread::tid();
    /// 不再关注class Channel类对象wakeupChannel_，所管理的文件描述符上的任何事件
    wakeupChannel_->disableAll();
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
    wakeupChannel_->remove();
    ::close(wakeupFd_);
    t_loopInThisThread = NULL;
}

/// 事件循环：必须在IO线程中执行（IO线程：创建了EventLoop对象的线程）
/// (1)遍历epoll的内核事件监听表epollfd_，从中找出有事件发生的fd，并将该fd所对应的表项的内容，
///   填入到，记录实际发生的事件的表activeChannels和记录实际发生的事件的表events_中保存
/// (2)本质上，EPollPoller::poll这个函数，就是epoll_wait函数所做的事
/// (3)遍历，记录实际发生的事件的表activeChannels_，并调用相应的事件处理函数，对发生的事件进行处理
void EventLoop::loop()
{
    assert(!looping_);
    /// 确保：执行时事件循环（EventLoop::loop()）的线程，是IO线程
    assertInLoopThread();
    /// 记录：IO线程，正在执行事件循环（EventLoop::loop()）
    looping_ = true;
    quit_ = false;  // FIXME: what if someone calls quit() before loop() ?
    LOG_TRACE << "EventLoop " << this << " start looping";

    while (!quit_)
    {
        activeChannels_.clear();
        /// poller_：class Poller IO复用的封装：封装了poll 和 epoll
        /// ====================================================================================================
        /// 在，class PollPoller IO复用的封装：封装了poll，中的功能
        /// ====================================================================================================
        /// 函数参数含义：
        /// int timeoutMs：poll函数的超时时间
        /// ChannelList* activeChannels：记录实际发生的事件的表
        /// 函数功能：
        /// (1) numEvents：存放，有事件发生的文件描述符的总数
        ///   监听pollfds_表（相当于epoll的内核事件监听表）中，存放的文件描述符上，是否有相应的事件发生
        /// (2)遍历pollfds_表（相当于epoll的内核事件监听表），从中找出有事件发生的fd，并将该fd所对应的表项的内容，
        ///   填入到，记录实际发生的事件的表activeChannels中保存
        /// (3)这2个操作，合到一起，相当于epoll_wait函数的功能
        ///   因而，PollPoller::poll这个函数的功能，本质上，就是epoll_wait函数所做的事，
        ///   所以，PollPoller::poll这个函数，也就相当于epoll_wait函数
        /// ====================================================================================================
        /// 在，class EPollPoller IO复用的封装：封装了epoll中的功能
        /// ====================================================================================================
        /// 函数参数含义：
        /// int timeoutMs：epoll函数的超时时间
        /// ChannelList* activeChannels：记录实际发生的事件的表
        /// 函数功能：
        /// (1) numEvents：存放，有事件发生的文件描述符的总数
        ///   监听epoll的内核事件监听表epollfd_中，存放的文件描述符上，是否有相应的事件发生
        /// (2)遍历epoll的内核事件监听表epollfd_，从中找出有事件发生的fd，并将该fd所对应的表项的内容，
        ///   填入到，记录实际发生的事件的表activeChannels和记录实际发生的事件的表events_中保存
        /// (3)本质上，EPollPoller::poll这个函数，就是epoll_wait函数所做的事
        pollReturnTime_ = poller_->poll(kPollTimeMs, &activeChannels_);
        ++iteration_;
        if (Logger::logLevel() <= Logger::TRACE)
        {
            printActiveChannels();
        }
        // TODO sort channel by priority
        eventHandling_ = true;
        /// 遍历，记录实际发生的事件的表activeChannels_，并调用相应的事件处理函数，对发生的事件进行处理
        for (ChannelList::iterator it = activeChannels_.begin();
                it != activeChannels_.end(); ++it)
        {
            /// class Channel类的作用：
            /// （1）channel：通道，大管子，文件描述符（文件描述符：就是插在文件上的大管子）
            /// （2）记录：实际发生的事件的表的一个表项的类型（内容）
            /// （3）用一个class Channel类，来管理一个文件描述符
            /// currentActiveChannel_：管理(存放)，从记录实际发生的事件的表activeChannels_中，取出来的一个表项
            currentActiveChannel_ = *it;
            /// revents_：里面存放着已经就绪(实际发生)的事件（由内核填充）
            /// 分发：调用某个socket文件描述符上所发生的事件，所对应的事件处理函数，处理发生的事件的这个过程，就是分发
            /// 实现事件分发机制：根据class Channel所管理的文件描述符上，实际发生（已经就绪）的事件revents_，调用相应的事件处理函数
            currentActiveChannel_->handleEvent(pollReturnTime_);
        }
        currentActiveChannel_ = NULL;
        eventHandling_ = false;
        /// 在IO线程中，执行延期执行的回调函数
        doPendingFunctors();
    }

    LOG_TRACE << "EventLoop " << this << " stop looping";
    looping_ = false;
}

void EventLoop::quit()
{
    quit_ = true;
    // There is a chance that loop() just executes while(!quit_) and exits,
    // then EventLoop destructs, then we are accessing an invalid object.
    // Can be fixed using mutex_ in both places.
    /// threadId_记录：IO线程的ID
    /// CurrentThread::tid()：获取当前线程的ID
    /// 判断当前是否在IO线程中执行
    if (!isInLoopThread())
    {
        /// 非IO线程执行此函数，向IO线程发送一个数据1，实现唤醒IO线程
        wakeup();
    }
}

/// 在IO线程中，执行用户任务回调函数cb
/// (1)当前在IO线程中，所以，可以立即执行用户任务回调函数cb
/// (2)当前不在IO线程中，所以，就不能立即执行用户任务回回调函数
/// 将需要在IO线程中执行的用户回调函数cb，放入到队列中保存，并在必要时唤醒IO线程，执行这个用户任务回调函数
void EventLoop::runInLoop(const Functor &cb)
{
    /// threadId_记录：IO线程的ID
    /// CurrentThread::tid()：获取当前线程的ID
    /// 判断当前是否在IO线程中执行
    if (isInLoopThread())/// 当前在IO线程中，所以，可以立即执行用户任务回调函数cb
    {
        // 执行用户任务回调函数
        cb();
    }
    else/// 当前不在IO线程中，所以就不能立即执行用户任务回回调函数
    {
        /// 将需要在IO线程中执行的用户回调函数cb，放入到队列中保存，并在必要时唤醒IO线程，执行这个用户任务回调函数
        queueInLoop(cb);
    }
}

/// 将需要在IO线程中执行的用户回调函数cb，放入到队列中保存，并在必要时唤醒IO线程，执行这个用户任务回调函数
void EventLoop::queueInLoop(const Functor &cb)
{
    {
        MutexLockGuard lock(mutex_);
        /// pendingFunctors_用于存放：需要延期执行的用户任务回调：这些回调函数，都是需要在IO线程中执行的用户任务回调
        pendingFunctors_.push_back(cb);
    }

    if (!isInLoopThread() || callingPendingFunctors_)
    {
        /// 非IO线程执行此函数，向IO线程发送一个数据1，实现唤醒IO线程
        wakeup();
    }
}

size_t EventLoop::queueSize() const
{
    MutexLockGuard lock(mutex_);
    return pendingFunctors_.size();
}

/// 函数参数含义：
/// const Timestamp &time：定时器的超时时间
/// TimerCallback &&cb：定时器的回调函数
/// 函数功能：
/// 在时间点time处，定时器超时，并调用定时器回调函数
TimerId EventLoop::runAt(const Timestamp &time, const TimerCallback &cb)
{
    /// 在定时器容器中，添加一个新的定时器：
    ///（1）定时器的回调函数为cb
    ///（2）定时器的超时时间为：以time时间点为起点，隔0.0这么长的时间后，定时器超时
    return timerQueue_->addTimer(cb, time, 0.0);
}

/// 函数参数含义：
/// double delay：定时器的超时时间
/// TimerCallback &&cb：定时器的回调函数
/// 函数功能：
/// 以当前时间Timestamp::now()为起点，经过delay这么长的时间后，定时器超时，并调用定时器回调函数
TimerId EventLoop::runAfter(double delay, const TimerCallback &cb)
{
    Timestamp time(addTime(Timestamp::now(), delay));
    /// 函数参数含义：
    /// const Timestamp &time：定时器的超时时间
    /// TimerCallback &&cb：定时器的回调函数
    /// 函数功能：
    /// 在时间点time处，定时器超时，并调用定时器回调函数
    return runAt(time, cb);
}

/// 函数参数含义：
/// double interval：定时器的超时时间
/// TimerCallback &&cb：定时器的回调函数
/// 函数功能：
/// 以当前时间Timestamp::now()为起点，经过interval + interval这么长的时间后，定时器超时，并调用定时器回调函数
TimerId EventLoop::runEvery(double interval, const TimerCallback &cb)
{
    Timestamp time(addTime(Timestamp::now(), interval));
    /// 在定时器容器中，添加一个新的定时器：
    ///（1）定时器的回调函数为cb
    ///（2）定时器的超时时间为：以time时间点为起点，隔interval这么长的时间后，定时器超时
    /// 也就是在，时间点Timestamp::now() + interval + interval处，定时器超时
    return timerQueue_->addTimer(cb, time, interval);
}

#ifdef __GXX_EXPERIMENTAL_CXX0X__
// FIXME: remove duplication
void EventLoop::runInLoop(Functor &&cb)
{
    if (isInLoopThread())
    {
        cb();
    }
    else
    {
        queueInLoop(std::move(cb));
    }
}

/// 将需要在IO线程中执行的用户回调函数cb，放入到队列中保存，并在必要时唤醒IO线程，执行这个用户任务回调函数
void EventLoop::queueInLoop(Functor &&cb)
{
    {
        MutexLockGuard lock(mutex_);
        /// pendingFunctors_用于存放：需要延期执行的用户任务回调：这些回调函数，都是需要在IO线程中执行的用户任务回调
        pendingFunctors_.push_back(std::move(cb));  // emplace_back
    }

    if (!isInLoopThread() || callingPendingFunctors_)
    {
        /// 非IO线程执行此函数，向IO线程发送一个数据1，实现唤醒IO线程
        wakeup();
    }
}

/// 函数参数含义：
/// const Timestamp &time：定时器的超时时间
/// TimerCallback &&cb：定时器的回调函数
/// 函数功能：
/// 在时间点time处，定时器超时，并调用定时器回调函数
TimerId EventLoop::runAt(const Timestamp &time, TimerCallback &&cb)
{
    /// 在定时器容器中，添加一个新的定时器：
    ///（1）定时器的回调函数为cb
    ///（2）定时器的超时时间为：以time时间点为起点，隔0.0这么长的时间后，定时器超时
    return timerQueue_->addTimer(std::move(cb), time, 0.0);
}

/// 函数参数含义：
/// double delay：定时器的超时时间
/// TimerCallback &&cb：定时器的回调函数
/// 函数功能：
/// 以当前时间Timestamp::now()为起点，经过delay这么长的时间后，定时器超时，并调用定时器回调函数
TimerId EventLoop::runAfter(double delay, TimerCallback &&cb)
{
    /// 以当前时间Timestamp::now()为起点，经过delay这么长的时间后，定时器超时
    Timestamp time(addTime(Timestamp::now(), delay));
    /// 函数参数含义：
    /// const Timestamp &time：定时器的超时时间
    /// TimerCallback &&cb：定时器的回调函数
    /// 函数功能：
    /// 在时间点time处，定时器超时，并调用定时器回调函数
    return runAt(time, std::move(cb));
}

/// 函数参数含义：
/// double interval：定时器的超时时间
/// TimerCallback &&cb：定时器的回调函数
/// 函数功能：
/// 以当前时间Timestamp::now()为起点，经过interval + interval这么长的时间后，定时器超时，并调用定时器回调函数
TimerId EventLoop::runEvery(double interval, TimerCallback &&cb)
{
    Timestamp time(addTime(Timestamp::now(), interval));
    /// 在定时器容器中，添加一个新的定时器：
    ///（1）定时器的回调函数为cb
    ///（2）定时器的超时时间为：以time时间点为起点，隔interval这么长的时间后，定时器超时
    /// 也就是在，时间点Timestamp::now() + interval + interval处，定时器超时
    return timerQueue_->addTimer(std::move(cb), time, interval);
}
#endif

void EventLoop::cancel(TimerId timerId)
{
    return timerQueue_->cancel(timerId);
}

/// ====================================================================================================
/// 在，class PollPoller IO复用的封装：封装了poll，中的功能
/// ====================================================================================================
/// 函数参数含义：
/// Channel* channel：
///   （1）文件描述符fd管理类
///   （2）将要存放到，pollfds_表（相当于epoll的内核事件监听表）的，一个表项内容
///    (3) 将要修改的，pollfds_表（相当于epoll的内核事件监听表）的，一个表项内容
/// 函数功能：
/// 这个操作，相当于epoll_ctl函数参数选项中的EPOLL_CTL_ADD和EPOLL_CTL_MOD
/// （1）在pollfds_表（相当于epoll的内核事件监听表）中，新增一个表项
/// （2）修改pollfds_表（相当于epoll的内核事件监听表）中的，某个表项
/// ====================================================================================================
/// 在，class EPollPoller IO复用的封装：封装了epoll中的功能
/// ====================================================================================================
/// 函数参数含义：
/// Channel* channel：
///   （1）文件描述符fd管理类
///   （2）将要存放到，epoll的内核事件监听表epollfd_中的，一个表项内容
///    (3) 将要修改的，epoll的内核事件监听表epollfd_中的，一个表项内容
/// 函数功能：
/// 这个操作，相当于epoll_ctl函数参数选项中的EPOLL_CTL_ADD和EPOLL_CTL_MOD
/// （1）在epoll的内核事件监听表epollfd_中，新增一个表项
/// （2）修改epoll的内核事件监听表epollfd_中的，某个表项
void EventLoop::updateChannel(Channel *channel)
{
    assert(channel->ownerLoop() == this);
    /// 确保：执行EventLoop::updateChannel函数的线程，是IO线程
    /// IO线程：创建了EventLoop对象的线程，就是IO线程
    assertInLoopThread();
    poller_->updateChannel(channel);
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
void EventLoop::removeChannel(Channel *channel)
{
    assert(channel->ownerLoop() == this);
    /// 确保：执行EventLoop::removeChannel函数的线程，是IO线程
    /// IO线程：创建了EventLoop对象的线程，就是IO线程
    assertInLoopThread();
    if (eventHandling_)
    {
        assert(currentActiveChannel_ == channel ||
               std::find(activeChannels_.begin(), activeChannels_.end(), channel) == activeChannels_.end());
    }
    poller_->removeChannel(channel);
}

/// 函数参数含义：
/// class Channel类的作用：
/// （1）channel：通道，大管子，文件描述符（文件描述符：就是插在文件上的大管子）
/// （2）记录：实际发生的事件的表的一个表项的类型（内容）
/// （3）用一个class Channel类，来管理一个文件描述符
/// 函数功能：
/// 从fd（文件描述符）到Channel*（class Channel文件描述符fd管理类）的映射
/// 网络库中，所有的文件描述符fd，其上注册的事件events，以及其上实际发生的事件revents，都保存在了，ChannelMap表中
/// 判断，channel，在ChannelMap表 channels_中是否存在
bool EventLoop::hasChannel(Channel *channel)
{
    assert(channel->ownerLoop() == this);
    /// 确保：执行EventLoop::hasChannel函数的线程，是IO线程
    /// IO线程：创建了EventLoop对象的线程，就是IO线程
    assertInLoopThread();

    return poller_->hasChannel(channel);
}

void EventLoop::abortNotInLoopThread()
{
    LOG_FATAL << "EventLoop::abortNotInLoopThread - EventLoop " << this
              << " was created in threadId_ = " << threadId_
              << ", current thread id = " <<  CurrentThread::tid();
}

/// 非IO线程执行此函数，向IO线程发送一个数据1，实现唤醒IO线程
void EventLoop::wakeup()
{
    uint64_t one = 1;
    /// 非IO线程，向IO线程发送一个数据1，
    /// 当IO线程收到数据1后，就会使得IO线程，从IO复用函数poll的阻塞调用中返回
    /// 进而实现了，唤醒IO线程的效果
    ssize_t n = sockets::write(wakeupFd_, &one, sizeof one);
    if (n != sizeof one)
    {
        LOG_ERROR << "EventLoop::wakeup() writes " << n << " bytes instead of 8";
    }
}

/// 由于poller_监测wakeupFd_上的读事件，因此，会触发EventLoop::handleRead函数执行
void EventLoop::handleRead()
{
    uint64_t one = 1;
    /// IO线程被非IO线程唤醒后，读取非IO线程发来的数据
    ssize_t n = sockets::read(wakeupFd_, &one, sizeof one);
    if (n != sizeof one)
    {
        LOG_ERROR << "EventLoop::handleRead() reads " << n << " bytes instead of 8";
    }
}

/// 在IO线程中，执行某个用户任务回调函数
void EventLoop::doPendingFunctors()
{
    /// 空向量
    std::vector<Functor> functors;
    callingPendingFunctors_ = true;

    {
        MutexLockGuard lock(mutex_);
        /// pendingFunctors_用于存放：需要延期执行的用户任务回调：这些回调函数，都是需要在IO线程中执行的用户任务回调
        /// 把pendingFunctors_变成空向量，并把pendingFunctors_中原来存放的内容，全部替换到functors向量中
        functors.swap(pendingFunctors_);
    }

    for (size_t i = 0; i < functors.size(); ++i)
    {
        /// 执行用户任务回调函数
        functors[i]();
    }
    callingPendingFunctors_ = false;
}

void EventLoop::printActiveChannels() const
{
    for (ChannelList::const_iterator it = activeChannels_.begin();
            it != activeChannels_.end(); ++it)
    {
        const Channel *ch = *it;
        LOG_TRACE << "{" << ch->reventsToString() << "} ";
    }
}

