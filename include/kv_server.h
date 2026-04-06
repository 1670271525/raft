#pragma once
#include <memory>
#include <vector>
#include "../grpc/raft.grpc.pb.h"
#include "kv_state_machine.h"
#include "raft.h"
#include <unordered_map>
#include "future_promise.h"
#include "../grpc/kv_server.grpc.pb.h"



namespace flz {


	class KVServer final : public kv::KVService::Service{
	public:
		KVServer(uint32_t me,std::vector<std::string>& peers);
		~KVServer();
		grpc::Status Put(grpc::ServerContext* context,const kv::PutRequest* request,kv::PutReply* reply)override;
		grpc::Status Get(grpc::ServerContext* context,const kv::GetRequest* request,kv::GetReply* reply)override;
	private:
		void ApplierCallback(const raft::LogEntry& entry);

	private:
		KVStateMachine::ptr m_kv_state_machine;
		RaftNode::ptr m_raft_node;
 		std::unordered_map<int,flz::Promise<std::string>> m_wait_map;
		flz::Mutex m_wait_map_mutex;

	};

}
