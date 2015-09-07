# translation - Implementing-a-Thread-Safe-Queue-using-Condition-Variables
# 实现一个线程安全队列
原链接： https://www.justsoftwaresolutions.co.uk/threading/implementing-a-thread-safe-queue-using-condition-variables.html

翻译： Scott Gu 


源代码：https://github.com/ScottKaiGu/Implementing-a-Thread-Safe-Queue-using-Condition-Variables/blob/master/concurrent_queue.hpp

多线程代码需要一次又一次面对的一个问题是，如何把数据从一个线程传到另一个县城。 举例来说，一个常见的把串行算法并行化的方法是，把他们分成块并且做成一个管道。管道中任意一块都可以单独在一个线程里运行。每个阶段完成后把数据给到下个阶段的输入队列。
 
 
##### Basic Thread Safety 使用mutex实现简单的线程安全
 
最简单的办法是封装一个非线程安全的队列，使用mutex保护它（实例使用boost中的方法和类型，需要1.35以上版）
 

    template<typename Data>
    class concurrent_queue
    {
    private:
        std::queue<Data> the_queue;
        mutable boost::mutex the_mutex;
    public:
        void push(const Data& data)
        {
            boost::mutex::scoped_lock lock(the_mutex);
            the_queue.push(data);
        }
    
        bool empty() const
        {
            boost::mutex::scoped_lock lock(the_mutex);
            return the_queue.empty();
        }
    
        Data& front()
        {
            boost::mutex::scoped_lock lock(the_mutex);
            return the_queue.front();
        }
        
        Data const& front() const
        {
            boost::mutex::scoped_lock lock(the_mutex);
            return the_queue.front();
        }
    
        void pop()
        {
            boost::mutex::scoped_lock lock(the_mutex);
            the_queue.pop();
        }
    };
     
     
     

如果一个以上县城从队列中取数据，当前的设计会受制于竞态条件，empty, front 和pop会互相竞争。
但是对于一个消费者的系统就没事。 但是假如队列是空的话多个线程有可能无事可做，进入loop 等待->check->等待...:
 
    while(some_queue.empty())
    {
        boost::this_thread::sleep(boost::posix_time::milliseconds(50));
    }
    
 尽管sleep()相较于忙等待避免了大量cpu资源的浪费，这个设计还是有些不足。首先线程必须每隔50ms（或者其他间隔）唤醒一次用来锁定mutex、检查队列、解锁mutex、强制上下文切换。   其次，睡眠的间隔时间相当于强加了一个限制给响应时间：数据被加到队列后到线程响应的响应时间。——— 0ms到50ms都有可能，平均是25ms。

##### 使用Condition Variable等待
不停轮询方案的一个替代方案是使用Condition Variable等待。  当数据被加到空队列之后Condition Variable会被通知，  然后等待的线程被唤醒。这需要mutex来保护队列。
我们在 concurrent_queue里实现了个成员方法:
 
    
    template<typename Data>
    class concurrent_queue
    {
    private:
        boost::condition_variable the_condition_variable;
    public:
        void wait_for_data()
        {
            boost::mutex::scoped_lock lock(the_mutex);
            while(the_queue.empty())
            {
                the_condition_variable.wait(lock);
            }
        }
        void push(Data const& data)
        {
            boost::mutex::scoped_lock lock(the_mutex);
            bool const was_empty=the_queue.empty();
            the_queue.push(data);
            if(was_empty)
            {
                the_condition_variable.notify_one();
            }
        }
        // rest as before
    };
这有三件事需要注意
>首先，lock变量被当作参数传到了条件变量的wait方法。 这使得条件变量的实现是可以自动的解锁mutex并把消费者线程加到等待队列。当第一个县城等待时另一个线程可以更新被保护的数据。

>其次， condition variable 等待在一个循环里面，可能遭遇假冒唤醒。所以在wait返回时检查实际状态非常重要。
当你执行唤醒操作的时候要小心

>第三，调用notify_one发生在数据被加入队列后。假如push操作抛出异常的话，这能避免等待的线程被唤醒却发现没数据。从代码来看，notify_one还是在被保护的区域内。
 
这还不是最佳方案：等待的线程可能在接到通知后立刻唤醒，并且是在mutex被解锁前，在这种条件下当退出wait重新获取mutex时，它将不得不阻塞。
通过修改这个方法，新的通知将在mutex解锁后发出，等待的线程可以立刻获得mutex不需等待：
    
    template<typename Data>
    class concurrent_queue
    {
    public:
        void push(Data const& data)
        {
            boost::mutex::scoped_lock lock(the_mutex);
            bool const was_empty=the_queue.empty();
            the_queue.push(data);
    
            lock.unlock(); // unlock the mutex
    
            if(was_empty)
            {
                the_condition_variable.notify_one();
            }
        }
        // rest as before
    };
##### 减少锁的开销
 
尽管条件变量改善了生产者消费者的性能，但是对于消费者来说执行锁的操作还是过多。wait_for_data, front 以及pop 全都要锁mutex，消费者还是会快速交替调用锁操作。 吧wait和pop整合为一个操作可以减少加锁解锁操作：
    template<typename Data>
    class concurrent_queue
    {
    public:
        void wait_and_pop(Data& popped_value)
        {
            boost::mutex::scoped_lock lock(the_mutex);
            while(the_queue.empty())
            {
                the_condition_variable.wait(lock);
            }
            
            popped_value=the_queue.front();
            the_queue.pop();
        }
    
        // rest as before
    };
     
使用引用参数而不是函数返回值来传递结果是为了避免抛出异常引发的安全问题，使用返回值的话如果拷贝构造函数有异常抛出，那么数据被移除了但是也丢失了。然而使用这种方式就不会（参见 Herb Sutter's Guru Of The Week #8 的讨论）有时需要使用 boost::optional来避免一些问题。
 
##### 处理多个消费者
 
wait_and_pop 不仅移掉了锁的间接开销还带来了额外的好处。　 —— 现在自动允许多个消费者了。
然而没有外部锁，把方法分成多个天生有细粒度的本质，这使他们会遭受竞态条件，现在吧方法结合起来就能安全的处理并发请求了。　 (one reason why the authors of the SGI STL advocate against making things like std::vector thread-safe — 你需要外部锁去做许多共同的工作，让内部锁变得浪费资源)。
 
 
如果多个线程并发的从一个满队列里面取数据，他们会在wait_and_pop里面线性化。 如果队列是空的，然后所有队列就会阻塞等待条件变量。 当有新的数据加入队列，一个等待的线程会被唤醒并取数据，同时其余数据继续等待。 如果多于一个线程被唤醒（比如假唤醒），又或者又一个新线程同时调用了wait_and_pop， while循环确保了只有一个线程可以pop，其余的会继续wait。
 
- 更新: 正如像David 在下面评论里面说的, 使用多个消费者确实会有一个问题： 当添加数据的时候如果有多个线程在等待，只有一个能被唤醒。如果只有一个数据加入队列这正是你想要的，但是如果有多个数据加入队列，那你会希望多个线程被唤醒。 
 
有两个解决方案：使用notify_all() 而不是 notify_one()来唤醒线程。
或者不管多少数据添加到队列都调用notify_one()而即使之前队列里面不是空的。
 If all threads are notified then the extra threads will see it as a spurious wake and resume waiting if there isn't enough data for them.如果所有的线程都被唤醒了，其余无数据可以分配的线程就把这次唤醒试做假唤醒并恢复等待。
 
如果我们每次调用push都唤醒一个县城，那么就只有正确数量的线程会被唤醒。 这正是推荐做法：条件变量的notify方法在没有线程等待的时候是很轻量的。
修改过的代码如下：
     
    template<typename Data>
    class concurrent_queue
    {
    public:
        void push(Data const& data)
        {
            boost::mutex::scoped_lock lock(the_mutex);
            the_queue.push(data);
            lock.unlock();
            the_condition_variable.notify_one();//每次都唤醒一个
        }
        // rest as before
    };
     
 但是把函数分成多个相对于合起来还是有一点好处的：有检查队列是否为空的能力，并且能在队列为空时做点别的。
empty这个方法在面对多个消费者时还是有用的，但是它的返回值是瞬时的而且是临时的，在一个线程调用wait_and_pop时可能已经变了，没法保证它的返回值一直正确。
出于以上原因我们有必要再加一个方法，try_pop，它返回true就是能拿到数据（并以传入参数形式拿到数据），反之就是队列当时是空的。
     
    template<typename Data>
    class concurrent_queue
    {
    public:
        bool try_pop(Data& popped_value)
        {
            boost::mutex::scoped_lock lock(the_mutex);
            if(the_queue.empty())
            {
                return false;
            }
            
            popped_value=the_queue.front();
            the_queue.pop();
            return true;
        }
    
        // rest as before
    };
 
通过移除front and pop 方法，我们这个简单而又单纯的实现，现在已经变成了一个可用的多生产者多消费者队列。
 
最终方案
多生产者多消费者队列的最终方案：
    
     
    /*'''
    Created on Nov 10, 2014
    performance tested: 
    10 million data (100B each), 3 producers and 3 consumers:
    *need about 10 seconds to process
    environment: 64bit win7, i7-4800MQ, 8GB 
    '''*/
    //
    // there's a spurious wake. issue, if there're many consumers (consumer number is depends on 
    // environment and performance requirement, usually hundreds consumers), 
    // cpu may waste many time here in this loop, causing performance issue. --scott
    /
    #pragma once

    #include "stdafx.h"
    #include <boost/thread/mutex.hpp>
    #include <boost/thread/thread.hpp>
    #include <boost/thread/locks.hpp>
    #include <boost/thread/condition_variable.hpp>
    #include "log4cxx/xml/domconfigurator.h"
    #include <string>
    #include <vector>
    #include <queue>
    
    template<typename Data>
    class Concurrent_Queue
    {
    private:
        std::queue<Data> the_queue;
        mutable boost::mutex the_mutex;
        boost::condition_variable the_condition_variable;
    
    public:
        
        void push(Data const& data)
        {
            /*if (data == NULL)
                return;
    */
            boost::mutex::scoped_lock lock(the_mutex);
            the_queue.push(data);
            lock.unlock();
            the_condition_variable.notify_one();
        }
    
        bool empty() const
        {
            boost::mutex::scoped_lock lock(the_mutex);
            return the_queue.empty();
        }
    
            
        void wait_and_pop(Data& popped_value)
        {
            boost::mutex::scoped_lock lock(the_mutex);
            
            while (the_queue.empty())
            {
                //log4cxx::Logger::getRootLogger()->debug("conQueue waiting...");
                the_condition_variable.wait(lock);
            }
    
            //log4cxx::Logger::getRootLogger()->debug("conQueue poping...");
            popped_value = the_queue.front();
            the_queue.pop();
        }
    
        bool try_pop(Data& popped_value)
        {
            boost::mutex::scoped_lock lock(the_mutex);
            if (the_queue.empty())
            {
                return false;
            }
    
            popped_value = the_queue.front();
            the_queue.pop();
            return true;
        }
        
        //
        // return value: pop-ed return true; otherwise false;
        //
        template<typename Duration>
        bool timed_wait_and_pop(Data& popped_value,
            Duration const& wait_duration)
        {
            boost::mutex::scoped_lock lock(the_mutex);
            if (!the_condition_variable.timed_wait(lock, wait_duration,
                queue_not_empty(the_queue)))
                return false;
            popped_value = the_queue.front();
            the_queue.pop();
            return true;
        }
    
        struct queue_not_empty
        {
            std::queue<Data>& queue;
    
            queue_not_empty(std::queue<Data>& queue_) :
                queue(queue_)
            {}
            bool operator()() const
            {
                return !queue.empty();
            }
        };
    };
     

 

需要注意的问题or可以改进的方向：

　　1. 内存占用太大耗尽系统内存：当队列大小超过设定的值时阻塞生产者。

　　2. 消费者太多会显著降低性能，原因是假唤醒问题。（当生产者消费者数量都是内核数量4倍时，每毫秒仅能处理60条消息，使用的是i7处理器8个逻辑内核）

 

 
