#include <iostream>
#include <thread>
#include <vector>
#include <string>
#include <chrono>
#include <atomic>
#include <memory>
#include <grpcpp/grpcpp.h>
#include "../include/kv_server.h"
#include "../grpc/raft.pb.h"
#include "../grpc/kv_server.grpc.pb.h"

// 模拟的端口
std::vector<std::string> g_peers = {
    "127.0.0.1:50000",
    "127.0.0.1:50001",
    "127.0.0.1:50002"
};

// ---------------------------------------------------------
// 1. 服务端启动逻辑
// ---------------------------------------------------------
void RunServer(int node_id) {
    flz::KVServer kv_server(node_id, g_peers);
    grpc::ServerBuilder builder;
    builder.AddListeningPort(g_peers[node_id], grpc::InsecureServerCredentials());
    builder.RegisterService(&kv_server);
    
    // 使用 getRaftNode
    builder.RegisterService(kv_server.getRaftNode());
    
    std::unique_ptr<grpc::Server> server(builder.BuildAndStart());
    server->Wait();
}

// ---------------------------------------------------------
// 2. 客户端测试桩封装
// ---------------------------------------------------------
class TestClient {
public:
    TestClient(const std::string& addr) {
        auto channel = grpc::CreateChannel(addr, grpc::InsecureChannelCredentials());
        stub_ = kv::KVService::NewStub(channel);
    }

    // 辅助生成 CommandMsg 序列化字符串
    std::string MakeCommandStr(raft::OpType type, const std::string& key, const std::string& val, int64_t cid, int64_t seq) {
        raft::CommandMsg cmd;
        cmd.set_type(type);
        cmd.set_key(key);
        cmd.set_value(val);
        cmd.set_client_id(cid);
        cmd.set_seq_id(seq);
        std::string out;
        cmd.SerializeToString(&out);
        return out;
    }

    grpc::Status Put(const std::string& key, const std::string& val, int64_t cid, int64_t seq) {
        grpc::ClientContext context;
        kv::PutRequest req;
        req.set_command(MakeCommandStr(raft::OpType::PUT, key, val, cid, seq));
        kv::PutReply rep;
        return stub_->Put(&context, req, &rep);
    }

    grpc::Status Get(const std::string& key, int64_t cid, int64_t seq, std::string& out_val) {
        grpc::ClientContext context;
        kv::GetRequest req;
        req.set_command(MakeCommandStr(raft::OpType::GET, key, "", cid, seq));
        kv::GetReply rep;
        grpc::Status status = stub_->Get(&context, req, &rep);
        if (status.ok()) {
            // 如果你的 proto 中 GetReply 定义了 value 字段，取消下面这行的注释
            // out_val = rep.value();
        }
        return status;
    }

private:
    std::unique_ptr<kv::KVService::Stub> stub_;
};

// ---------------------------------------------------------
// 3. 测试用例执行逻辑
// ---------------------------------------------------------
void RunTests() {
    std::cout << "\n[Test] 等待 3 秒让集群完成初始选举...\n";
    // C++11 标准写法
    std::this_thread::sleep_for(std::chrono::seconds(3));

    std::vector<std::unique_ptr<TestClient>> clients;
    for (const auto& peer : g_peers) {
        // C++11 使用 new，而不是 C++14 的 std::make_unique
        clients.push_back(std::unique_ptr<TestClient>(new TestClient(peer)));
    }

    // 寻找 Leader
    int leader_idx = -1;
    for (int i = 0; i < 3; ++i) {
        auto status = clients[i]->Put("test_leader", "v1", 1, 1);
        if (status.ok()) {
            leader_idx = i;
            break;
        }
    }

    if (leader_idx == -1) {
        std::cerr << "[Test Failed] 未找到 Leader，选举可能失败或网络异常！\n";
        return;
    }
    std::cout << "[Test] 成功找到 Leader: 节点 " << leader_idx << "\n";

    // --- 场景 1: 基础读写测试 ---
    std::cout << "\n>>> 场景 1: 基础 Put/Get 测试 <<<\n";
    {
        auto status_put = clients[leader_idx]->Put("key1", "value1", 1, 2);
        if (!status_put.ok()) std::cerr << "Put 失败!\n";

        std::string get_val;
        auto status_get = clients[leader_idx]->Get("key1", 1, 3, get_val);
        std::cout << "场景 1 结果: Put OK, Get 状态: " << (status_get.ok() ? "OK" : "Fail") << "\n";
    }

    // --- 场景 2: Follower 拒绝测试 ---
    std::cout << "\n>>> 场景 2: 非 Leader 节点路由测试 <<<\n";
    {
        int follower_idx = (leader_idx + 1) % 3;
        auto status = clients[follower_idx]->Put("key2", "value2", 2, 1);
        if (status.error_code() == grpc::StatusCode::UNAVAILABLE) {
            std::cout << "场景 2 结果: 成功被 Follower 拒绝 (UNAVAILABLE)!\n";
        } else {
            std::cerr << "场景 2 失败: Follower 返回了错误的状态码 " << status.error_code() << "\n";
        }
    }

    // --- 场景 3: 幂等性测试 ---
    std::cout << "\n>>> 场景 3: 幂等性与重复请求测试 <<<\n";
    {
        clients[leader_idx]->Put("key_idem", "val_first", 3, 100);
        auto status = clients[leader_idx]->Put("key_idem", "val_second", 3, 100);
        std::cout << "场景 3 结果: 重复请求返回状态: " << (status.ok() ? "OK" : "Fail") << "\n";
    }

    // --- 场景 4: 高并发写入测试 ---
    std::cout << "\n>>> 场景 4: 高并发写入测试 (10 个线程, 共 100 个请求) <<<\n";
    {
        std::vector<std::thread> workers;
        std::atomic<int> success_count{0};
        
        for (int t = 0; t < 10; ++t) {
            // C++11 Lambda 捕获
            workers.emplace_back([&clients, leader_idx, t, &success_count]() {
                for (int i = 0; i < 10; ++i) {
                    int seq = t * 100 + i;
                    std::string key = "concurrent_key_" + std::to_string(seq);
                    std::string val = "val_" + std::to_string(seq);
                    
                    auto status = clients[leader_idx]->Put(key, val, 100 + t, seq);
                    if (status.ok()) {
                        success_count++;
                    }
                }
            });
        }
        
        for (auto& w : workers) {
            w.join();
        }
        std::cout << "场景 4 结果: 成功并发写入 " << success_count.load() << " 个请求\n";
    }

    std::cout << "\n[Test] 所有测试执行完毕，你可以按 Ctrl+C 终止集群。\n";
}

int main() {
    // 1. 启动集群节点
    std::vector<std::thread> servers;
    for (int i = 0; i < 3; ++i) {
        servers.emplace_back(RunServer, i);
    }

    // 2. 启动客户端测试
    std::thread client_thread(RunTests);

    client_thread.join();
    
    // Server 节点死循环阻塞，测试结束后 detach 线程随主进程退出
    for(auto& t : servers) {
        t.detach(); 
    }

    return 0;
}
