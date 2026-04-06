#pragma once

#include "../grpc/raft.grpc.pb.h"
#include <iostream>
#include <string>
#include <vector>
#include "mutex.h"
#include <grpcpp/grpcpp.h>
#include "thread_pool.h"
#include "kv_state_machine.h"
#include "util.h"
#include "future_promise.h"
#include <memory>


namespace flz {

	using ApplyCallback = std::function<void(const raft::LogEntry&)>;


	enum Role{
		FOLLOWER,
		CANDIDATE,
		LEADER
	};

	struct StartResult{
		int index;
		int term;
		bool is_leader;
	};


	class RaftNode final : public raft::RaftService::Service{
	public:
		using ptr = std::unique_ptr<RaftNode>;
		RaftNode(uint32_t my_id,std::vector<std::string> peer_channels,ApplyCallback apply_cb);
		~RaftNode();
		
		grpc::Status RequestVote(grpc::ServerContext* context,const raft::RequestVoteArgs* request,raft::RequestVoteReply* reply)override;
		grpc::Status AppendEntries(grpc::ServerContext* context,const raft::AppendEntriesArgs* request,raft::AppendEntriesReply* reply)override;
		
		// 获取当前节点的状态 (用于测试)
		void getState(int& term, bool& is_leader) {
			flz::Mutex::Lock lock(m_mutex);
			term = m_current_term;
			is_leader = (m_role == LEADER);
		}
		
		std::string GetLogCommand(int index) {
   			flz::Mutex::Lock lock(m_mutex);
    		if (index < m_logs.size()) return m_logs[index].command();
    		return "";
		}

		StartResult start(const std::string& command);
	private:
		void electionTimerLoop();
		void heartbeatLoop();
		void applyLogLoop();

		void startElection();
		void becomeFollower(int term,int voted_for = -1);
		void becomeLeader();

		void sendRequestToPeer(const std::string& peer,const raft::RequestVoteArgs& args,int saved_term);
		void sendAppendEntriesToPeer(const std::string& peer,const raft::AppendEntriesArgs& args,int saved_term,int last_included);
		void resetElectionTimer();
		void updateCommitIndex();
		void persist();
		void restore();

	private:
		uint32_t m_id;
		std::vector<std::string> m_peers;
		std::vector<raft::LogEntry> m_logs;
		flz::Mutex m_mutex;
		flz::Semaphore::ptr m_heartbeat_sem  = std::make_shared<Semaphore>(0);
		flz::Semaphore::ptr m_election_sem = std::make_shared<Semaphore>(0);
		flz::Semaphore::ptr m_apply_sem = std::make_shared<Semaphore>(0);

		//flz::KVStateMachine::ptr m_kvsm;

		std::atomic<bool> m_stop_flag;
		flz::ThreadPool::ptr m_threadpool;
		int m_current_term;
		int m_voted_for;
		int m_votes_received = 0;
		Role m_role;

		std::chrono::steady_clock::time_point m_last_heartbeat_time;
		
		ApplyCallback m_apply_cb;


		Thread::ptr m_election_thread;
		Thread::ptr m_heartbeat_thread;
		Thread::ptr m_apply_log_thread;


		std::chrono::milliseconds m_interval;
		
		int m_last_log_index;
		int m_last_log_term;

		int m_commit_index = 0;
		int m_last_applied = 0;

		std::vector<int> m_next_index;
		std::vector<int> m_match_index;

	};


	
}


