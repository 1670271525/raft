#include "../include/raft.h"
#include <random>


namespace flz{

	RaftNode::RaftNode(uint32_t my_id,std::vector<std::string> peer_channels,ApplyCallback apply_cb):
		m_id(my_id),
		m_peers(peer_channels),
		m_current_term(0),
		m_interval(std::chrono::milliseconds(100)),
		m_voted_for(-1),
		m_stop_flag(false),
		m_votes_received(0),
		m_role(FOLLOWER),
		m_last_log_index(0),
		m_last_log_term(0),
		m_commit_index(0),
		m_last_applied(0),
		m_apply_cb(apply_cb){
		
		raft::LogEntry dummy;
		dummy.set_index(0);
		dummy.set_term(0);
		m_logs.push_back(dummy);

		m_threadpool = std::unique_ptr<flz::ThreadPool>(new ThreadPool(peer_channels.size()));
		m_heartbeat_thread = std::make_shared<Thread>(std::bind(&RaftNode::heartbeatLoop,this),"thread_heartbeat",m_heartbeat_sem);
		m_election_thread = std::make_shared<Thread>(std::bind(&RaftNode::electionTimerLoop,this),"thread_election",m_election_sem);
		m_apply_log_thread = std::make_shared<Thread>(std::bind(&RaftNode::applyLogLoop,this),"thread_applylog",m_apply_sem);

		//m_kvsm = std::unique_ptr<flz::KVStateMachine>(new KVStateMachine());
		

		resetElectionTimer();
		m_next_index.resize(peer_channels.size(),1);
		m_match_index.resize(peer_channels.size(),0);

		restore();


	}

	RaftNode::~RaftNode(){
		m_stop_flag = true;
		m_election_sem->notify();
		m_heartbeat_sem->notify();

	}

	int getRandomTimeout(){
		static thread_local std::mt19937 generator(std::random_device{}());
		std::uniform_int_distribution<int> distribution(150,300);
		return distribution(generator);
	}

	void RaftNode::electionTimerLoop(){
		std::cout<<m_id<<"-election thread start\n";
		auto timeout = std::chrono::milliseconds(getRandomTimeout());

		while(true){
			bool should_wait = false;
			std::chrono::milliseconds wait_time = timeout;
			bool is_leader = false;
			{
				Mutex::Lock lock(m_mutex);
				if(m_stop_flag)return;
				
				if(m_role == LEADER){
					is_leader = true;
				}else{				
					auto now = std::chrono::steady_clock::now();
					auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now-m_last_heartbeat_time);
					if(elapsed >= timeout){
						startElection();
						resetElectionTimer();
					}else{
						should_wait = true;
						wait_time = timeout - elapsed;
					}
				}
			}
			if(is_leader){
				m_election_sem->wait();
			}else if(should_wait){
				m_election_sem->timewait(wait_time);
			}
		}
	}

	void RaftNode::heartbeatLoop(){
		std::cout<<m_id<<"-heartbeat thread start\n";
		while(true){
			int saved_term;
			int saved_leader_id;
			int last_included = m_logs.size()-1;
			bool is_leader = false;
			{
				Mutex::Lock lock(m_mutex);
				if(m_stop_flag)return;
				if(m_role == LEADER){
					is_leader = true;
					saved_leader_id = m_id;
					saved_term = m_current_term;
				}
			}
			if(!is_leader){
				m_heartbeat_sem->wait();
				continue;
			}
			for(size_t i = 0;i<m_peers.size();i++){
				if(i==m_id)continue;
				m_threadpool->enqueue([this,i,saved_leader_id,saved_term,last_included]{
						raft::AppendEntriesArgs args;
						args.set_term(saved_term);
						args.set_leaderid(saved_leader_id);
						auto req_prelogindex = m_next_index[i]-1;
						args.set_prevlogindex(req_prelogindex);
						args.set_prevlogterm(m_logs[req_prelogindex].term());
						args.set_leadercommit(m_commit_index);


						for(auto j = m_next_index[i];j<m_logs.size();j++){
							auto* entry = args.add_entries();
							entry->CopyFrom(m_logs[j]);
						}

						sendAppendEntriesToPeer(this->m_peers[i],args,saved_term,last_included);

						});
			}
			m_heartbeat_sem->timewait(m_interval);
			
		}
	}
	
	void RaftNode::applyLogLoop(){
		while (!m_stop_flag) {
			bool stop_flag = false;
			bool need_apply = false;
			{
				flz::Mutex::Lock lock(m_mutex);
				stop_flag = m_stop_flag;
				need_apply = m_commit_index > m_last_applied;
			}
			if (m_stop_flag) return;
			if (!need_apply) m_apply_sem->wait();
			// 批量取出需要应用的日志，减少锁竞争
			std::vector<raft::LogEntry> entries_to_apply;
			{
				Mutex::Lock raft_lock(m_mutex);
				while (m_commit_index > m_last_applied) {
					m_last_applied++;
					entries_to_apply.push_back(m_logs[m_last_applied]);
				}
			}

			// 在锁外调用应用接口（避免死锁）
			for (auto& entry : entries_to_apply) {
				//m_kvsm->apply(entry);
				m_apply_cb(entry);
			}
			
			
		}
	}


	grpc::Status RaftNode::RequestVote(grpc::ServerContext* context,const raft::RequestVoteArgs* request,raft::RequestVoteReply* reply){
		std::cout<<"RequestVote\n";
		flz::Mutex::Lock lock(m_mutex);

		reply->set_term(m_current_term);
		reply->set_votegranted(false);

		if(m_current_term > request->term()){
			return grpc::Status::OK;
		}
		if(m_current_term < request->term()){
			becomeFollower(request->term());
			reply->set_term(request->term());
		}
		if(m_voted_for == -1 || m_voted_for == request->candidateid()){


			bool is_candidate_log_up_to_date = false;
			if(m_last_log_term < request->lastlogterm()){
				is_candidate_log_up_to_date = true;
			}else if(m_last_log_term == request->lastlogterm() && m_last_log_index <= request->lastlogindex()){
				is_candidate_log_up_to_date = true;
			}

			if(is_candidate_log_up_to_date){
				reply->set_votegranted(true);
				m_voted_for = request->candidateid();

				resetElectionTimer();
			}

		}

		persist();

		return grpc::Status::OK;
	}

	grpc::Status RaftNode::AppendEntries(grpc::ServerContext* context,const raft::AppendEntriesArgs* request,raft::AppendEntriesReply* reply){

		
		flz::Mutex::Lock lock(m_mutex);

		reply->set_term(m_current_term);
		reply->set_success(false);
		reply->set_id(m_id);

		if(request->term() < m_current_term){
			return grpc::Status::OK;
		}

		if(request->term() > m_current_term){
			becomeFollower(request->term());
			reply->set_term(request->term());
		}
		
		if(m_role == CANDIDATE){
			becomeFollower(request->term());
		}
		
		if(m_logs.size()<request->prevlogindex()){
			return grpc::Status::OK;
		}

		if(m_logs[request->prevlogindex()].term() != request->prevlogterm()){
			return grpc::Status::OK;
		}
	
		resetElectionTimer();
	
		// 截断冲突日志并追加新日志
		int insert_index = request->prevlogindex() + 1;
		int entries_idx = 0;
		while (insert_index < m_logs.size() && entries_idx < request->entries_size()) {
			if (m_logs[insert_index].term() != request->entries(entries_idx).term()) {
				// 发现冲突，截断本地日志
				m_logs.erase(m_logs.begin() + insert_index, m_logs.end());
				break;
			}
			insert_index++;
			entries_idx++;
		}
		// 追加剩余的新日志
		while (entries_idx < request->entries_size()) {
			m_logs.push_back(request->entries(entries_idx));
			entries_idx++;
		}

		// 更新 Follower 的 commit_index
		if (request->leadercommit() > m_commit_index) {
			m_commit_index = std::min((int)request->leadercommit(), (int)(m_logs.size() - 1));
			m_apply_sem->notify(); // 唤醒 ApplyThread
		}



		reply->set_success(true);
	
		persist();

		return grpc::Status::OK;
	}


	void RaftNode::startElection(){
		m_role = CANDIDATE;
		m_current_term++;
		m_voted_for = m_id;
		m_votes_received = 1;
		int current_votes = 1;

		int saved_term = m_current_term;
		int saved_last_log_index = m_last_log_index;
		int saved_last_log_term = m_last_log_term;
		for(size_t i = 0;i<m_peers.size();i++){
			if(i == m_id)continue;

			m_threadpool->enqueue([this,saved_term,i,saved_last_log_index,saved_last_log_term]{
					raft::RequestVoteArgs args;
					args.set_term(m_current_term);
					args.set_candidateid(m_id);
					args.set_lastlogterm(saved_last_log_term);
					args.set_lastlogindex(saved_last_log_index);
					sendRequestToPeer(m_peers[i],args,saved_term);
			});
		}
		
		persist();
	}

	void RaftNode::becomeFollower(int term,int voted_for){
		
		m_voted_for = voted_for;
		m_role = FOLLOWER;
		m_current_term = term;
	}

	void RaftNode::becomeLeader(){
		m_role = LEADER;

		for(size_t i = 0;i<m_peers.size();i++){
			m_next_index[i] = m_logs.size();
			m_match_index[i] = 0;
		}

		m_heartbeat_sem->notify();
		std::cout << "[Election] Node " << m_id << " became LEADER at term " << m_current_term << "!\n";
	}

	void RaftNode::sendRequestToPeer(const std::string& peer,const raft::RequestVoteArgs& args,int saved_term){
		auto stub = raft::RaftService::NewStub(grpc::CreateChannel(peer,grpc::InsecureChannelCredentials()));
		grpc::ClientContext context;
		context.set_deadline(std::chrono::system_clock::now()+std::chrono::milliseconds(100));
		raft::RequestVoteReply reply;
		grpc::Status status = stub->RequestVote(&context,args,&reply);
		if(!status.ok()){
			std::cout<<peer<<"-RequestVote fail: "<<status.error_message()<<"\n";
			return;
		}
		Mutex::Lock lock(m_mutex);
		if(m_current_term != saved_term)return;
		if(m_current_term != reply.term())return;
		if(m_role != CANDIDATE)return;
		if(reply.term() > m_current_term){
			becomeFollower(reply.term());
			return;
		}
		if(reply.votegranted()){
			m_votes_received++;
			if(m_votes_received>m_peers.size()/2 && m_role != LEADER){
				becomeLeader();
				return;
			}
		}

	}

	void RaftNode::sendAppendEntriesToPeer(const std::string& peer,const raft::AppendEntriesArgs& args,int saved_term,int last_included){
		//std::cout<<m_id<<"send heartbeat\n";
		auto stub = raft::RaftService::NewStub(grpc::CreateChannel(peer,grpc::InsecureChannelCredentials()));
		grpc::ClientContext context;
		raft::AppendEntriesReply reply;
		grpc::Status status = stub->AppendEntries(&context,args,&reply);
		if(!status.ok()){
			std::cout<<"AppendEntries fail: "<<status.error_message()<<"\n";
			return;
		}
		int peer_id = reply.id();
		Mutex::Lock lock(m_mutex);
		if(m_current_term != saved_term)return;
		if(m_role != LEADER)return;
		if(!reply.success()){
			if(m_current_term < reply.term()){
				becomeFollower(reply.term());
				return;
			}
				
			
			m_next_index[peer_id] = std::max(1,m_next_index[peer_id]-1);
			m_heartbeat_sem->notify();
		}
		if(reply.success()){
			m_match_index[peer_id] = std::max(m_match_index[peer_id],last_included);
			m_next_index[peer_id] = m_match_index[peer_id]+1;
			updateCommitIndex();
		}
		
	}
	
	void RaftNode::resetElectionTimer(){
		m_last_heartbeat_time = std::chrono::steady_clock::now();
	}


	StartResult RaftNode::start(const std::string& command){
		
		
		StartResult sr;

		Mutex::Lock lock(m_mutex);
		if(m_role != LEADER){
			sr.index = -1;
			sr.term = -1;
			sr.is_leader = false;
		}

		sr.term = m_current_term;
		sr.index = m_logs.size();
		
		raft::LogEntry entry;
		entry.set_term(m_current_term);
		entry.set_command(command);
		entry.set_index(sr.index);

		m_logs.push_back(entry);

		m_heartbeat_sem->notify();

		std::cout<<"[Leader] Start: index="<<sr.index<<",term="<<sr.term<<" command size="<<command.size()<<"\n";

		persist();

		return sr;

	}

	void RaftNode::updateCommitIndex() {
		//在持有 m_mutex 的情况下调用
		
		//收集所有节点的 match_index
		std::vector<int> sorted_match;
		for (size_t i = 0; i < m_peers.size(); i++) {
			if (i == m_id) {
				sorted_match.push_back(m_logs.size() - 1); // Leader 自己的进度
			} else {
				sorted_match.push_back(m_match_index[i]);
			}
		}

		//排序找到中位数 (代表过半数节点都达到的索引)
		std::sort(sorted_match.begin(), sorted_match.end());
		
		// 对于 3 节点集群，下标 1 是中位数；对于 5 节点，下标 2 是中位数
		int n = m_peers.size();
		int majority_index = sorted_match[(n - 1) / 2]; 

		
		if (majority_index > m_commit_index && 
			m_logs[majority_index].term() == m_current_term) {
			
			m_commit_index = majority_index;
			
			// 提交增加后，唤醒 ApplyThread 将日志应用到状态机
			// 你可以使用条件变量或信号量
			m_apply_sem->notify(); 
			
			std::cout << "[Leader] CommitIndex advanced to " << m_commit_index << std::endl;
		}
	}

	
	void RaftNode::persist(){
		raft::PersistState state;
		state.set_current_term(m_current_term);
		state.set_voted_for(m_voted_for);
		
		for(const auto& log:m_logs){
			auto* entry = state.add_logs();
			entry->CopyFrom(log);
		}

		std::string serialized_data;
		state.SerializeToString(&serialized_data);
		const std::string filename = "raft_state_"+std::to_string(m_id)+".bin";
		std::ofstream out;
		bool rt = File::OpenForWrite(out,filename,std::ios::out|std::ios::binary|std::ios::trunc);
		if(rt){
			out.write(serialized_data.data(),serialized_data.size());
			out.flush();
			out.close();
		}else{
			std::perror("Fail to open file for persistence");
			assert(false);
		}


	}

	void RaftNode::restore(){
		std::string filename = "raft_state_"+std::to_string(m_id)+".bin";
		std::ifstream in(filename,std::ios::in | std::ios::binary);
		if(in.is_open()){
			std::string serialized_data((std::istreambuf_iterator<char>(in)),std::istreambuf_iterator<char>());
			in.close();
			raft::PersistState state;
			if(state.ParseFromString(serialized_data)){
				m_current_term = state.current_term();
				m_voted_for = state.voted_for();
				m_logs.clear();
				for(auto& log:state.logs()){
					m_logs.push_back(log);
				}
				std::cout << "[Node " << m_id << "] Successfully restored from disk. Term: "
                      << m_current_term << ", Logs length: " << m_logs.size() << std::endl;
			}
		}else{
			std::cout << "[Node " << m_id << "] No persistent state found. Starting fresh." << std::endl;
		}

	}


}	
