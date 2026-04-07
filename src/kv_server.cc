#include "../include/kv_server.h"
#include <string.h>
#include "command.cc"


namespace flz {

	KVServer::KVServer(uint32_t me,std::vector<std::string>& peers){
		m_kv_state_machine = std::unique_ptr<KVStateMachine>(new KVStateMachine());
		
		auto apply_cb = [this](const raft::LogEntry& entry){
			this->ApplierCallback(entry);
		};

		m_raft_node = std::unique_ptr<RaftNode>(new RaftNode(me,peers,apply_cb));
		
	}

	KVServer::~KVServer(){

	}


	void KVServer::ApplierCallback(const raft::LogEntry& entry){


		raft::CommandMsg cmd = ParseCommand(entry.command());
		
		std::string result_value = "";
		if(cmd.type() == raft::OpType::GET){
			m_kv_state_machine->getValue(cmd.key(),result_value);
		}else{
			m_kv_state_machine->apply(entry);
			result_value = "SUCCESS_RESULT_FROM_KV";
		}

	
		{
			flz::Mutex::Lock lock(m_wait_map_mutex);
			auto it = m_wait_map.find(entry.index());
			if(it != m_wait_map.end()){
				if(it->second.term == entry.term()){
					it->second.prom.set_value(result_value);
				}else {
					it->second.prom.set_value("ERROR_WRONG_LEADER");
				}
				m_wait_map.erase(it);
			}
		}

	}

	grpc::Status KVServer::Put(grpc::ServerContext* context,const kv::PutRequest* request,kv::PutReply* reply){
		StartResult sr = m_raft_node->start(request->command());

		if(!sr.is_leader){
			return grpc::Status(grpc::StatusCode::UNAVAILABLE,"Not Leader");
		}

		flz::Promise<std::string> prom;
		flz::Future<std::string> fut = prom.get_future();
		
		{
			flz::Mutex::Lock lock(m_wait_map_mutex);
			m_wait_map[sr.index] = {sr.term,std::move(prom)};
		}
		
		std::string result;
		if(fut.wait_for(std::chrono::milliseconds(500),result)){
			if (result == "ERROR_WRONG_LEADER") {
				return grpc::Status(grpc::StatusCode::ABORTED, "Lost Leadership before commit");
			}
			return grpc::Status::OK;
		}else{
			return grpc::Status(grpc::StatusCode::DEADLINE_EXCEEDED,"TIMEOUT");
		}

	}
	
	grpc::Status KVServer::Get(grpc::ServerContext* context,const kv::GetRequest* request,kv::GetReply* reply){
		StartResult sr = m_raft_node->start(request->command());
		if(!sr.is_leader){
			return grpc::Status(grpc::StatusCode::UNAVAILABLE,"Not Leader");
		}
		flz::Promise<std::string> prom;
		flz::Future<std::string> fut = prom.get_future();
		
		{
			flz::Mutex::Lock lock(m_wait_map_mutex);
			m_wait_map[sr.index] = {sr.term,std::move(prom)};
		}
		
		std::string result;
		if(fut.wait_for(std::chrono::milliseconds(500),result)){
			if (result == "ERROR_WRONG_LEADER") {
				return grpc::Status(grpc::StatusCode::ABORTED, "Lost Leadership before commit");
			}
			return grpc::Status::OK;
		}else{
			return grpc::Status(grpc::StatusCode::DEADLINE_EXCEEDED,"TIMEOUT");
		}


	}

}
