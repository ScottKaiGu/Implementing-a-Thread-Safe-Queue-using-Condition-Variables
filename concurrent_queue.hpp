/*'''
Created on Nov 10, 2014

performance tested: 
	10 million data (100B each), 3 producers and 3 consumers:
	*need about 10 seconds to process

	environment: 64bit win7, i7-4800MQ, 8GB 
'''*/


#pragma once

#include <boost/thread/mutex.hpp>
#include <boost/thread/thread.hpp>
#include <boost/thread/locks.hpp>
#include <boost/thread/condition_variable.hpp>
#include <boost/format.hpp>

#include <string>
#include <vector>
#include <queue>

//
// WARNING: queue::pop() doesn't return anything
//
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
	
	void push_many(const std::vector<Data> manydata)
	{
		boost::mutex::scoped_lock lock(the_mutex);
		
		for (auto data = manydata.begin(); data != manydata.end(); data++)
		{
			the_queue.push(*data);
		}

		lock.unlock();
		the_condition_variable.notify_all();
	}

	bool empty() const
	{
		boost::mutex::scoped_lock lock(the_mutex);
		return the_queue.empty();
	}


	void wait_and_pop(Data& popped_value)
	{
		boost::mutex::scoped_lock lock(the_mutex);
		int cntr = 0; // this is a local var, so this won't increase to a very large number, don't worry about the Integer overflow 
		while (the_queue.empty())
		{
			//
			// there's a spurious wake. issue, if there're many consumers 
			// ( consumer number is depends on 
			// environment and performance requirement, usually when
			//		consumerNumber > 10*LogicalCoreNumber ), 
			// cpu may waste many time here in this loop, causing performance issue.
			// -- scott
			//
			the_condition_variable.wait(lock);
		}

		popped_value = the_queue.front();
		the_queue.pop();
	}


	//
	// return value: not empty return true; otherwise return false;
	//
	template<typename Duration>
	bool timed_wait(Duration const& wait_duration)
	{
		boost::mutex::scoped_lock lock(the_mutex);
		if (!the_condition_variable.timed_wait(lock, wait_duration,
			queue_not_empty(the_queue)))
			return false;
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

	void wait()
	{
		boost::mutex::scoped_lock lock(the_mutex);
		int cntr = 0; // this is a local var, so this won't increase to a very large number, don't worry about the Integer overflow 
		while (the_queue.empty())
		{
			//
			// there's a spurious wake. issue, if there're many consumers 
			// ( consumer number is depends on 
			// environment and performance requirement, usually when
			//		consumerNumber > 10*LogicalCoreNumber ), 
			// cpu may waste many time here in this loop, causing performance issue.
			// -- scott
			//
			the_condition_variable.wait(lock);
		}
	}


	// not thread safe
	int size() {
		return the_queue.size();
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
};


typedef boost::shared_ptr<Concurrent_Queue<std::pair<int,std::string>>> Concurrent_queue_ptr;

