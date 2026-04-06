#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <vector>
#include <chrono>

#include <grpcpp/grpcpp.h>
#include "../include/raft.h"

using grpc::Server;
using grpc::ServerBuilder;

class RaftNodeRunner {
public:
    RaftNodeRunner(uint32_t id, const std::string& addr, const std::vector<std::string>& peers)
        : m_id(id), m_addr(addr), m_peers(peers) {}

    void Start() {
        m_raft_node = std::unique_ptr<flz::RaftNode>(new flz::RaftNode(m_id, m_peers));
        ServerBuilder builder;
        builder.AddListeningPort(m_addr, grpc::InsecureServerCredentials());
        builder.RegisterService(m_raft_node.get());
        m_server = builder.BuildAndStart();
        
        m_thread = std::thread([this]() { m_server->Wait(); });
        std::cout << "[TestEnv] Node " << m_id << " started." << std::endl;
    }

    void Stop() {
        if (m_server) {
            m_server->Shutdown();
            m_thread.join();
            m_server.reset();
            m_raft_node.reset();
            std::cout << "[TestEnv] Node " << m_id << " CRASHED." << std::endl;
        }
    }

    flz::RaftNode* GetRaft() { return m_raft_node.get(); }
    ~RaftNodeRunner() { Stop(); }

private:
    uint32_t m_id;
    std::string m_addr;
    std::vector<std::string> m_peers;
    std::unique_ptr<flz::RaftNode> m_raft_node;
    std::unique_ptr<Server> m_server;
    std::thread m_thread;
};

// 寻找 Leader
int FindLeader(std::vector<std::unique_ptr<RaftNodeRunner>>& nodes) {
    for (int i = 0; i < 10; i++) { // 最多等 1 秒
        for (size_t j = 0; j < nodes.size(); ++j) {
            if (!nodes[j]->GetRaft()) continue;
            int term; bool is_leader;
            nodes[j]->GetRaft()->getState(term, is_leader); // 假设你 Lab2A 时写了 GetState
            if (is_leader) return j;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    return -1;
}

// 检查某个索引处，多数派是否达成了一致的指令
bool CheckConsensus(std::vector<std::unique_ptr<RaftNodeRunner>>& nodes, int target_index, const std::string& expected_cmd) {
    int match_count = 0;
    for (size_t i = 0; i < nodes.size(); ++i) {
        if (!nodes[i]->GetRaft()) continue;
        std::string cmd = nodes[i]->GetRaft()->GetLogCommand(target_index);
        if (cmd == expected_cmd) match_count++;
    }
    return match_count > nodes.size() / 2;
}

int main() {
    std::vector<std::string> peers = { "127.0.0.1:50051", "127.0.0.1:50052", "127.0.0.1:50053" };
    std::vector<std::unique_ptr<RaftNodeRunner>> nodes;
    for (uint32_t i = 0; i < peers.size(); ++i) {
        nodes.push_back(std::unique_ptr<RaftNodeRunner>(new RaftNodeRunner(i, peers[i], peers)));
        nodes[i]->Start();
    }

    std::cout << "\n=== Test 1: Basic Agreement ===" << std::endl;
    int leader = FindLeader(nodes);
    if (leader == -1) { std::cerr << "FAIL: No leader elected!\n"; return 1; }
    
    std::cout << "Leader is Node " << leader << ". Submitting command 'CMD_1'..." << std::endl;
    auto res = nodes[leader]->GetRaft()->start("CMD_1");
    
    // 给系统 500ms 进行网络分发和提交
    std::this_thread::sleep_for(std::chrono::milliseconds(500)); 
    
    if (!CheckConsensus(nodes, res.index, "CMD_1")) {
        std::cerr << "FAIL: Cluster failed to reach consensus on 'CMD_1' at index " << res.index << "!\n";
        return 1;
    }
    std::cout << "PASS: Basic Agreement reached!\n";

    std::cout << "\n=== Test 2: Agreement after Follower Failure ===" << std::endl;
    int dead_follower = (leader + 1) % 3;
    nodes[dead_follower]->Stop(); // 杀掉一个 Follower
    
    auto res2 = nodes[leader]->GetRaft()->start("CMD_2");
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    
    if (!CheckConsensus(nodes, res2.index, "CMD_2")) {
        std::cerr << "FAIL: Failed to reach consensus with a minority dead!\n";
        return 1;
    }
    std::cout << "PASS: Agreement reached despite follower failure!\n";

    std::cout << "\n=== Test 3: Agreement after Leader Failure ===" << std::endl;
    nodes[leader]->Stop(); // 杀掉当前 Leader
    nodes[dead_follower]->Start(); // 复活之前的 Follower
    
    std::cout << "Waiting for new leader..." << std::endl;
    int new_leader = FindLeader(nodes);
    if (new_leader == -1 || new_leader == leader) { std::cerr << "FAIL: New leader not elected!\n"; return 1; }
    
    auto res3 = nodes[new_leader]->GetRaft()->start("CMD_3");
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    
    if (!CheckConsensus(nodes, res3.index, "CMD_3")) {
        std::cerr << "FAIL: Failed to reach consensus after leader change!\n";
        return 1;
    }
    std::cout << "PASS: Agreement reached after leader crash and re-election!\n";

    std::cout << "\n========================================" << std::endl;
    std::cout << "  ALL LAB 2B TESTS PASSED SUCCESSFULLY! " << std::endl;
    std::cout << "========================================" << std::endl;

    for (auto& node : nodes) node->Stop();
    return 0;
}
