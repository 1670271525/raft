#include "../grpc/raft.grpc.pb.h"


namespace flz {


	inline raft::CommandMsg ParseCommand(const std::string& raw_cmd_bytes) {
		raft::CommandMsg cmd;
		// 反序列化：将 string 解析为 Protobuf 对象
		if (!cmd.ParseFromString(raw_cmd_bytes)) {
			// 解析失败处理：通常是因为数据损坏，在 Raft 中属于严重错误
			std::cerr << "[KV SM] Failed to deserialize CommandMsg!" << std::endl;
			// 可以抛出异常或返回一个携带错误标识的 default object
		}
		return cmd;
	}


}
