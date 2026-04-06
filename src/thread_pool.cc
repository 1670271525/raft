#include "../include/thread_pool.h"




namespace flz{

	ThreadPool::ThreadPool(uint32_t threads):m_thread_size(threads),m_stop(false){
		for(size_t i = 0;i<m_thread_size;i++){
			flz::Mutex::Lock lock(m_mutex);
			auto thread_ = std::make_shared<Thread>(std::bind(&ThreadPool::cb,this),"thread_"+std::to_string(i),m_semaphore);

			m_threads.push_back(thread_);
			lock.unlock();
		}	
	}

	ThreadPool::~ThreadPool(){
		std::cout<<"threadpool exit\n";
		flz::Mutex::Lock lock(m_mutex);
		m_stop = true;
		lock.unlock();
		for(size_t i = 0;i<m_thread_size;i++){
			m_semaphore->notify();
		}
		for(size_t i = 0;i<m_thread_size;i++){
			m_threads[i]->join();
		}
		std::cout<<"threadpool real exit\n";
	}

	void ThreadPool::cb(){
		while(true){
			//std::cout<<Thread::GetName()<<"睡眠\n";
			flz::Thread::GetThis()->wait();
			//std::cout<<Thread::GetName()<<"唤醒\n";
			std::function<void()> task;
			{
				flz::Mutex::Lock lock(m_mutex);
				if(m_stop&&m_tasks.empty())return;
				if(!m_tasks.empty()){
					task = std::move(m_tasks.front());
					m_tasks.pop();
				}
			}
			if(task){
				//std::cout<<Thread::GetName()<<"执行任务\n";
				task();
			}
		}
	}
}
