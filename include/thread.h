#pragma once
#include <pthread.h>
#include <memory>
#include "mutex.h"
#include <functional>
#include "nocopyable.h"


namespace flz {

	class Thread:public Nocopyable{
	public:
		using ptr = std::shared_ptr<Thread>;
		Thread(std::function<void()> c,const std::string& name,flz::Semaphore::ptr semaphore);
		~Thread();
		pid_t getId()const{return m_id;}
		const std::string& getName()const{return m_name;}
		void join();
		void wait();
	public:
		static const std::string& GetName();
		static void SetName(const std::string& name);
		static Thread* GetThis();
	private:
		static void* Run(void* args);
	private:
		std::string m_name;
		pid_t m_id;
		std::function<void()> m_cb;
		pthread_t m_thread;
		flz::Semaphore::ptr m_semaphore;
	};



}
