#!/bin/bash
echo "停止海昏侯简牍监测系统..."

if [ -f .server.pid ]; then
    SERVER_PID=$(cat .server.pid)
    if kill -0 $SERVER_PID 2>/dev/null; then
        kill $SERVER_PID
        echo "已停止后端服务 (PID: $SERVER_PID)"
    else
        echo "后端服务未运行"
    fi
    rm -f .server.pid
fi

if [ -f .simulator.pid ]; then
    SIM_PID=$(cat .simulator.pid)
    if kill -0 $SIM_PID 2>/dev/null; then
        kill $SIM_PID
        echo "已停止模拟器 (PID: $SIM_PID)"
    else
        echo "模拟器未运行"
    fi
    rm -f .simulator.pid
fi

echo "停止完成"
