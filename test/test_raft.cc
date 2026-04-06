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

// 启动单个 Raft 节点的函数
void RunRaftNode(uint32_t my_id, const std::string& my_address, const std::vector<std::string>& all_peers) {
    // 1. 初始化 Raft 核心逻辑节点
    flz::RaftNode raft_node(my_id, all_peers);

    // 2. 配置并启动 gRPC 服务
    ServerBuilder builder;
    builder.AddListeningPort(my_address, grpc::InsecureServerCredentials());
    builder.RegisterService(&raft_node);
    
    std::unique_ptr<Server> server(builder.BuildAndStart());
    std::cout << "[Cluster] Node " << my_id << " started on " << my_address << std::endl;
    
    // 3. 阻塞等待（让节点持续运行）
    server->Wait();
}

int main() {
    std::cout << "========================================" << std::endl;
    std::cout << "  Starting Raft Cluster (3 Nodes)...  " << std::endl;
    std::cout << "========================================" << std::endl;

    // 定义集群中所有节点的地址
    std::vector<std::string> peers = {
        "127.0.0.1:50051",
        "127.0.0.1:50052",
        "127.0.0.1:50053"
    };

    std::vector<std::thread> cluster_threads;

    // 启动 3 个节点
    for (uint32_t i = 0; i < peers.size(); ++i) {
        // 每个节点在一个独立的线程中运行其 gRPC Server
        cluster_threads.emplace_back(RunRaftNode, i, peers[i], peers);
    }

    // 主线程持续运行，你可以通过 Ctrl+C 终止
    for (auto& t : cluster_threads) {
        t.join();
    }

    return 0;
}
