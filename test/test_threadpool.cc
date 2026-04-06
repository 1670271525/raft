#include "../include/thread_pool.h"
#include <iostream>
#include <chrono>
#include <thread>

int main(){
	
	flz::ThreadPool thread_pool(10);
	flz::Mutex mutex_;
	for(size_t i = 0;i<20;i++){
		thread_pool.enqueue([i,&mutex_](){
				flz::Mutex::Lock lock1(mutex_);
				std::cout<<flz::Thread::GetName()<<"running\n";
				lock1.unlock();
				std::this_thread::sleep_for(std::chrono::seconds(1));
				flz::Mutex::Lock lock2(mutex_);
				std::cout<<flz::Thread::GetName()<<"done\n";
				lock2.unlock();
				});
	}
	//std::this_thread::sleep_for(std::chrono::seconds(5));
}
