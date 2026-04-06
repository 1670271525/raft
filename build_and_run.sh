#!/bin/bash
set -e

echo "================================"
echo "    RaftProject 编译构建脚本    "
echo "================================"

mkdir -p build
mkdir -p out/test

echo "[1/3] 配置 CMake 构建系统..."
cd build
cmake ..

echo "[2/3] 开始编译项目..."
make -j$(nproc)
cd ..

echo "[3/3] 编译成功！拉起 Raft 3节点测试集群..."
echo "--------------------------------"
./out/test/test_lab2a
