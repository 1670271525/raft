#pragma once
#include <pthread.h>
#include <semaphore.h>
#include <iostream>
#include <memory>
#include <chrono>

namespace flz {


class Semaphore{
public:
	using ptr = std::shared_ptr<Semaphore>;
	Semaphore(uint32_t count = 0);
	~Semaphore();
	virtual void wait();
	virtual void notify();
	bool timewait(std::chrono::milliseconds ms);

private:
	sem_t m_semaphore;
};

template<class T>
struct ScopedLockImpl{
public:
	ScopedLockImpl(T& mutex):m_mutex(mutex),m_lock(true){
		m_mutex.lock();
	}
	~ScopedLockImpl(){
		m_lock = false;
		m_mutex.unlock();
	}
	void lock(){
		if(m_lock)return;
		m_lock = true;
		m_mutex.lock();
	}
	void unlock(){
		if(!m_lock)return;
		m_lock = false;
		m_mutex.unlock();
	}
private:
	T& m_mutex;
	bool m_lock:1;
};

class Mutex{
public:
	using Lock = ScopedLockImpl<Mutex>;
	Mutex(){
		pthread_mutex_init(&m_mutex,nullptr);
	}
	~Mutex(){
		pthread_mutex_destroy(&m_mutex);
	}
	void lock(){
		pthread_mutex_lock(&m_mutex);
	}
	void unlock(){
		pthread_mutex_unlock(&m_mutex);
	}
private:
	pthread_mutex_t m_mutex;
};

}
