#!/bin/bash
echo "========================================"
echo " 海昏侯简牍监测系统 - Linux 构建脚本"
echo "========================================"
echo ""

BUILD_DIR="build"

echo "[1/4] 检查依赖..."
if ! command -v cmake &> /dev/null; then
    echo "[ERROR] 未找到 CMake，请先安装 CMake"
    exit 1
fi

if ! command -v g++ &> /dev/null; then
    echo "[ERROR] 未找到 g++ 编译器"
    exit 1
fi

echo ""
echo "[2/4] 创建构建目录..."
mkdir -p $BUILD_DIR

echo ""
echo "[3/4] 配置 CMake 项目..."
cd $BUILD_DIR
cmake .. -DCMAKE_BUILD_TYPE=Release
if [ $? -ne 0 ]; then
    echo "[ERROR] CMake 配置失败"
    cd ..
    exit 1
fi

echo ""
echo "[4/4] 编译项目..."
make -j$(nproc)
if [ $? -ne 0 ]; then
    echo "[ERROR] 编译失败"
    cd ..
    exit 1
fi

cd ..
echo ""
echo "========================================"
echo " 构建完成！"
echo " 可执行文件位置: $BUILD_DIR/backend/haihunhou_server"
echo "                $BUILD_DIR/opc_simulator/opcua_simulator"
echo "========================================"
