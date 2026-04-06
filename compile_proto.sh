#!/bin/bash

# 发生错误时立即退出脚本
set -e

echo "=================================="
echo "  编译 Protobuf & gRPC C++ 文件   "
echo "=================================="

# 确保你在项目的根目录执行此脚本
PROTO_DIR="./grpc"

# 查找系统的 grpc_cpp_plugin 路径
GRPC_CPP_PLUGIN_EXECUTABLE=$(which grpc_cpp_plugin)

if [ -z "$GRPC_CPP_PLUGIN_EXECUTABLE" ]; then
    echo "错误: 找不到 grpc_cpp_plugin，请确认是否正确安装了 gRPC 编译工具！"
    exit 1
fi

echo "开始生成标准 Protobuf 消息类..."
protoc -I="$PROTO_DIR" --cpp_out="$PROTO_DIR" "$PROTO_DIR"/*.proto

echo "开始生成 gRPC 服务接口类..."
protoc -I="$PROTO_DIR" --grpc_out="$PROTO_DIR" --plugin=protoc-gen-grpc="$GRPC_CPP_PLUGIN_EXECUTABLE" "$PROTO_DIR"/*.proto

echo "编译完成！生成的文件位于 $PROTO_DIR 目录下。"
