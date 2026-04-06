#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <vector>
#include <chrono>
#include <memory>
#include <grpcpp/grpcpp.h>
#include "../include/raft.h"

using grpc::Server;
using grpc::ServerBuilder;

// 封装一个 Raft 节点的运行容器，方便控制启停
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
        
        m_thread = std::thread([this]() {
            m_server->Wait();
        });
        std::cout << "[TestEnv] Node " << m_id << " started." << std::endl;
    }

    void Stop() {
        if (m_server) {
            // 模拟宕机/网络断开：立刻关闭 gRPC 服务
            m_server->Shutdown();
            m_thread.join();
            m_server.reset();
            m_raft_node.reset();
            std::cout << "[TestEnv] Node " << m_id << " CRASHED/STOPPED." << std::endl;
        }
    }

    void GetState(int& term, bool& is_leader) {
        if (m_raft_node) {
            m_raft_node->getState(term, is_leader);
        } else {
            term = -1;
            is_leader = false;
        }
    }

    ~RaftNodeRunner() { Stop(); }

private:
    uint32_t m_id;
    std::string m_addr;
    std::vector<std::string> m_peers;
    std::unique_ptr<flz::RaftNode> m_raft_node;
    std::unique_ptr<Server> m_server;
    std::thread m_thread;
};

// 辅助函数：检查当前集群谁是 Leader
int CheckOneLeader(std::vector<std::unique_ptr<RaftNodeRunner>>& nodes) {
    int leader_id = -1;
    int max_term = -1;
    
    // 给集群一点时间进行选举
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));

    for (size_t i = 0; i < nodes.size(); ++i) {
        int term;
        bool is_leader;
        nodes[i]->GetState(term, is_leader);
        
        if (is_leader) {
            if (leader_id != -1) {
                std::cerr << "FAIL: Multiple leaders found! Node " << leader_id << " and Node " << i << std::endl;
                exit(1);
            }
            leader_id = i;
            max_term = term;
        }
    }

    if (leader_id != -1) {
        std::cout << "=> Found Leader: Node " << leader_id << " at Term " << max_term << std::endl;
    } else {
        std::cout << "=> No leader found." << std::endl;
    }
    return leader_id;
}


int main() {
    std::vector<std::string> peers = {
        "127.0.0.1:50051",
        "127.0.0.1:50052",
        "127.0.0.1:50053"
    };

    std::vector<std::unique_ptr<RaftNodeRunner>> nodes;
    for (uint32_t i = 0; i < peers.size(); ++i) {
        nodes.push_back(std::unique_ptr<RaftNodeRunner>(new RaftNodeRunner(i, peers[i], peers)));
    }

    std::cout << "\n=== Test 1: Initial Election ===" << std::endl;
    for (auto& node : nodes) node->Start();
    int leader1 = CheckOneLeader(nodes);
    if (leader1 == -1) {
        std::cerr << "Test 1 Failed: Expected 1 leader, got 0." << std::endl;
        return 1;
    }

    std::cout << "\n=== Test 2: Re-Election after Leader Failure ===" << std::endl;
    // 模拟原 Leader 宕机
    nodes[leader1]->Stop();
    std::cout << "Waiting for new election..." << std::endl;
    
    int leader2 = CheckOneLeader(nodes);
    if (leader2 == -1) {
        std::cerr << "Test 2 Failed: Expected a new leader to be elected." << std::endl;
        return 1;
    }
    if (leader1 == leader2) {
        std::cerr << "Test 2 Failed: Dead node is still leader?!" << std::endl;
        return 1;
    }

    std::cout << "\n=== Test 3: Old Leader Rejoins ===" << std::endl;
    // 旧 Leader 重启恢复
    nodes[leader1]->Start();
    int leader3 = CheckOneLeader(nodes);
    // 旧 leader 重启后，可能会引发一轮新的选举（如果它的 timer 跑得快），
    // 或者直接变成 follower。无论怎样，最终只能有 1 个 Leader。
    if (leader3 == -1) {
         std::cerr << "Test 3 Failed: Cluster failed to stabilize after node rejoin." << std::endl;
         return 1;
    }

    std::cout << "\n=== Test 4: No Quorum (Minority Partition) ===" << std::endl;
    // 关掉两个节点，只留一个
    int dead_leader = leader3;
    int dead_follower = (leader3 + 1) % 3;
    nodes[dead_leader]->Stop();
    nodes[dead_follower]->Stop();
    
    std::cout << "Waiting to ensure remaining node does not become leader..." << std::endl;
    std::this_thread::sleep_for(std::chrono::milliseconds(1500)); // 等待足够长的时间让它发起选举
    int leader4 = CheckOneLeader(nodes);
    if (leader4 != -1) {
        std::cerr << "Test 4 Failed: Node " << leader4 << " elected itself without a majority!" << std::endl;
        return 1;
    }

    std::cout << "\n========================================" << std::endl;
    std::cout << "  ALL LAB 2A TESTS PASSED SUCCESSFULLY! " << std::endl;
    std::cout << "========================================" << std::endl;

    // 清理残余节点
    for (auto& node : nodes) node->Stop();

    return 0;
}
