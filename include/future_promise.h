#pragma once
#include "mutex.h"
#include <memory>
#include <chrono>


namespace flz {

	template <typename T>
	struct SharedState {
		flz::Mutex mutex;
		flz::Semaphore semaphore{0}; // 初始信号量为 0，即阻塞状态
		T value;
		bool is_ready = false;
	};
	
	template <typename T>
	class Future {
	public:
		explicit Future(std::shared_ptr<SharedState<T>> state) : m_state(state) {}

		// 阻塞等待直到拿到结果
		T get() {
			m_state->semaphore.wait(); // 等待 Promise 发出 notify
			flz::Mutex::Lock lock(m_state->mutex);
			return m_state->value;
		}

		// 带有超时的等待
		bool wait_for(std::chrono::milliseconds ms, T& out_value) {
			if (m_state->semaphore.timewait(ms)) {
				flz::Mutex::Lock lock(m_state->mutex);
				out_value = m_state->value;
				return true;
			}
			return false; // 超时了还没拿到结果
		}

	private:
		std::shared_ptr<SharedState<T>> m_state;
	};

	template <typename T>
	class Promise {
	public:
		Promise() : m_state(std::make_shared<SharedState<T>>()) {}

		// 获取对应的 Future 发给别人
		Future<T> get_future() {
			return Future<T>(m_state);
		}

		// 填入结果，并唤醒等待者
		void set_value(const T& val) {
			flz::Mutex::Lock lock(m_state->mutex);
			if (m_state->is_ready) return; // 防止重复 set
			
			m_state->value = val;
			m_state->is_ready = true;
			
			lock.unlock(); // 先解锁再 notify 
			m_state->semaphore.notify(); // 唤醒沉睡在 get() 或 wait_for() 上的线程
		}

	private:
		std::shared_ptr<SharedState<T>> m_state;
	};
}

