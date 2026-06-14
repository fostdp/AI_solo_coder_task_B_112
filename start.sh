#!/bin/bash
echo "========================================"
echo " 海昏侯简牍监测系统 - 启动脚本 (Linux)"
echo "========================================"
echo ""

SERVER_EXE="build/backend/haihunhou_server"
SIMULATOR_EXE="build/opc_simulator/opcua_simulator"
CONFIG_FILE="config/config.yaml"

echo "[检查] ClickHouse 数据库..."
echo "请确保 ClickHouse 服务已启动并执行了初始化脚本"
echo "clickhouse-client --queries-file=clickhouse/init.sql"
echo ""

if [ ! -f "$SERVER_EXE" ]; then
    echo "[ERROR] 后端服务未编译，请先运行 ./build.sh"
    exit 1
fi

echo "[1/2] 启动后端服务..."
nohup $SERVER_EXE --config $CONFIG_FILE --frontend frontend > logs/server.log 2>&1 &
SERVER_PID=$!
echo "后端服务 PID: $SERVER_PID"

sleep 3

echo "[2/2] 启动 OPC UA 模拟器（可选）..."
read -p "是否启动OPC UA模拟器? (y/n): " START_SIM
if [[ "$START_SIM" =~ ^[Yy]$ ]]; then
    if [ -f "$SIMULATOR_EXE" ]; then
        nohup $SIMULATOR_EXE --server http://localhost:8080 --interval 21600 > logs/simulator.log 2>&1 &
        SIM_PID=$!
        echo "模拟器 PID: $SIM_PID"
        echo $SIM_PID > .simulator.pid
    else
        echo "[WARNING] 模拟器未编译，跳过"
    fi
fi

echo $SERVER_PID > .server.pid

echo ""
echo "========================================"
echo " 系统已启动"
echo " 前端访问: http://localhost:8080"
echo " API文档:   http://localhost:8080/api/"
echo "========================================"
echo ""
echo "停止服务: ./stop.sh"
