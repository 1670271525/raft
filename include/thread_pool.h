#pragma once

#include "mutex.h"
#include <vector>
#include <queue>
#include "thread.h"
#include <memory>

namespace flz {


	class ThreadPool{
	public:
		using ptr = std::unique_ptr<ThreadPool>;
		ThreadPool(uint32_t threads);
		~ThreadPool();
		template<class F,class... Args>
		void enqueue(F&& f,Args&&... args);
		void cb();

	private:
		flz::Mutex m_mutex;
		flz::Semaphore::ptr m_semaphore = std::make_shared<Semaphore>(0);
		uint32_t m_thread_size;
		std::vector<flz::Thread::ptr> m_threads;
		std::queue<std::function<void()>> m_tasks;
		bool m_stop;

	};

	template<class F,class... Args>
	void ThreadPool::enqueue(F&& f,Args&&... args){
		flz::Mutex::Lock lock(m_mutex);
		m_tasks.emplace(std::bind(std::forward<F>(f),std::forward<Args>(args)...));
		lock.unlock();
		//std::cout<<"新增任务\n";
		m_semaphore->notify();
	}

}

