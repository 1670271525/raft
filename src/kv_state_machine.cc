#include "../include/kv_state_machine.h"

#include "command.cc"



namespace flz {

	// 执行应用逻辑（由 Raft 的 ApplyThread 在锁外调用）
   	void KVStateMachine::apply(const raft::LogEntry& entry) {
		flz::Mutex::Lock lock(m_mutex);


		
   		 //std::cout << "[KV SM] Node applied Log Index " << entry.index()
              //<< " | Term: " << entry.term()
             // << " | Command: " << entry.command() << std::endl;
		 //return;
        // 解析日志中的指令
		raft::CommandMsg cmd = ParseCommand(entry.command());
        
        // 幂等性检查：判断这条指令是否已经被执行过？
        if (cmd.type() != raft::OpType::GET && isDuplicate(cmd.client_id(), cmd.seq_id())) {
            std::cout << "[KV SM] Ignored duplicate command: client=" 
                      << cmd.client_id() << " seq=" << cmd.seq_id() << std::endl;
            // 已经是重复请求，直接跳过执行，但依然需要唤醒等待的 RPC
            return;
        }

        // 真正执行业务逻辑
        std::string result_value = "";
        if (cmd.type() == raft::OpType::PUT) {
            m_kv_data[cmd.key()] = cmd.value();
        } 
        else if (cmd.type() == raft::OpType::APPEND) {
            m_kv_data[cmd.key()] += cmd.value();
        } 
        else if (cmd.type() == raft::OpType::GET) {
            if (m_kv_data.find(cmd.key()) != m_kv_data.end()) {
                result_value = m_kv_data[cmd.key()];
            }
        }

        // 更新该客户端的最高序列号，防止后续重复执行
        if (cmd.type() != raft::OpType::GET) {
            m_client_last_seq[cmd.client_id()] = cmd.seq_id();
        }

		
        std::cout << "[KV SM] Applied Log Index " << entry.index() 
                  << " | Op: " << (int)cmd.type() << " Key: " << cmd.key() << std::endl;
    }


	bool KVStateMachine::isDuplicate(int64_t client_id, int64_t seq_id) {
		
		auto it = m_client_last_seq.find(client_id);
		if (it != m_client_last_seq.end()) {
			// 如果收到的序列号小于或等于记录的最后序列号，说明是重放的旧请求
			return seq_id <= it->second;
		}
		return false;
	}

	bool KVStateMachine::getValue(const std::string& key,std::string& result_value){
		auto it = m_kv_data.find(key);
		if(it != m_kv_data.end()){
			result_value = it->second;
			return true;
		}
		result_value = "ERROR_NO_KEY";
		return false;
	}
}
