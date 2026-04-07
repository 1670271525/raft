cmake_minimum_required(VERSION 3.15)
project(RaftProject CXX)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/out/test)

# 1. 寻找基础依赖
find_package(Threads REQUIRED)
find_package(Protobuf REQUIRED)

# 2. 引入 PkgConfig 并寻找 gRPC 
find_package(PkgConfig REQUIRED)
# IMPORTED_TARGET 标志会帮我们自动生成一个现代的 CMake 链接目标 PkgConfig::GRPC
pkg_check_modules(GRPC REQUIRED IMPORTED_TARGET grpc++ grpc)

include_directories(
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/grpc
)

set(GRPC_SRCS
    ${CMAKE_SOURCE_DIR}/grpc/raft.pb.cc
    ${CMAKE_SOURCE_DIR}/grpc/raft.grpc.pb.cc
	${CMAKE_SOURCE_DIR}/grpc/kv_server.pb.cc
	${CMAKE_SOURCE_DIR}/grpc/kv_server.grpc.pb.cc


)

set(CORE_SRCS
    ${CMAKE_SOURCE_DIR}/src/mutex.cc
    ${CMAKE_SOURCE_DIR}/src/thread.cc
    ${CMAKE_SOURCE_DIR}/src/thread_pool.cc
    ${CMAKE_SOURCE_DIR}/src/raft.cc
	${CMAKE_SOURCE_DIR}/src/kv_state_machine.cc
	${CMAKE_SOURCE_DIR}/src/kv_server.cc
	${CMAKE_SOURCE_DIR}/src/command.cc
	${CMAKE_SOURCE_DIR}/src/util.cc



)

# 编译核心库
add_library(raft_core STATIC ${CORE_SRCS} ${GRPC_SRCS})

# 3. 链接目标修改：将 gRPC::grpc++ 替换为 PkgConfig::GRPC
target_link_libraries(raft_core
    PUBLIC
    Threads::Threads
    protobuf::libprotobuf
    PkgConfig::GRPC
)

#add_executable(test_raft ${CMAKE_SOURCE_DIR}/test/test_raft.cc)
#target_link_libraries(test_raft PRIVATE raft_core)




# 添加专门的自动化测试可执行文件
add_executable(test_cluster ${CMAKE_SOURCE_DIR}/test/test_cluster.cc)
target_link_libraries(test_cluster PRIVATE raft_core)



