#include "../include/mutex.h"
#include <stdexcept>


namespace flz {

Semaphore::Semaphore(uint32_t count){
	if(sem_init(&m_semaphore,0,count)){
		throw std::logic_error("sem_init fail!");
	}
}

Semaphore::~Semaphore(){
	sem_destroy(&m_semaphore);
}

void Semaphore::wait(){
	if(sem_wait(&m_semaphore)){
		throw std::logic_error("sem_wait fail!");
	}
}

void Semaphore::notify(){
	if(sem_post(&m_semaphore)){
		throw std::logic_error("sem_post fail!");
	}
}

bool Semaphore::timewait(std::chrono::milliseconds ms){
	struct timespec ts;
	if(clock_gettime(CLOCK_REALTIME,&ts)==-1){
		return false;
	}
	int64_t sec = ms.count() / 1000;
	int64_t nsec = (ms.count() % 1000) * 1000000;
	ts.tv_sec += sec;
	ts.tv_nsec += nsec;

	if(ts.tv_nsec >= 1000000000){
		ts.tv_sec += 1;
		ts.tv_nsec -= 1000000000;
	}
	while(true){
		int rt = sem_timedwait(&m_semaphore,&ts);
		if(rt == 0)return true;
		if(rt == -1 && errno == EINTR)continue;
		if(rt == 01 && errno == ETIMEDOUT)return false;
		return false;
	}
}


}
