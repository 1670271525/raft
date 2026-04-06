#pragma once


#include <iostream>
#include <memory>
#include <unordered_map>
#include "mutex.h"
#include "../grpc/raft.grpc.pb.h"


namespace flz {

	class KVStateMachine{
	public:
		using ptr = std::unique_ptr<KVStateMachine>;
		void apply(const raft::LogEntry& entry);

		bool getValue(const std::string& key,std::string& result_value);
	private:
		bool isDuplicate(int64_t client_id,int64_t seq_id);
	private:
		flz::Mutex m_mutex;
		std::unordered_map<std::string,std::string> m_kv_data;
		std::unordered_map<int64_t,int64_t> m_client_last_seq;
	};



}
