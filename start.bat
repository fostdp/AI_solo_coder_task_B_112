@echo off
echo ========================================
echo  海昏侯简牍监测系统 - 启动脚本 (Windows)
echo ========================================
echo.

set SERVER_EXE=build\backend\Release\haihunhou_server.exe
set SIMULATOR_EXE=build\opc_simulator\Release\opcua_simulator.exe
set CONFIG_FILE=config\config.yaml

echo [检查] ClickHouse 数据库...
echo 请确保 ClickHouse 服务已启动并执行了初始化脚本
echo clickhouse-client --queries-file=clickhouse\init.sql
echo.

if not exist %SERVER_EXE% (
    echo [ERROR] 后端服务未编译，请先运行 build.bat
    pause
    exit /b 1
)

echo [1/2] 启动后端服务...
start "海昏侯后端服务" %SERVER_EXE% --config %CONFIG_FILE% --frontend frontend

timeout /t 5 /nobreak >nul

echo [2/2] 启动 OPC UA 模拟器（可选）...
set /p START_SIM=是否启动OPC UA模拟器? (y/n): 
if /i "%START_SIM%"=="y" (
    if exist %SIMULATOR_EXE% (
        start "OPC UA模拟器" %SIMULATOR_EXE% --server http://localhost:8080 --interval 21600
    ) else (
        echo [WARNING] 模拟器未编译，跳过
    )
)

echo.
echo ========================================
echo  系统已启动
echo  前端访问: http://localhost:8080
echo  API文档:   http://localhost:8080/api/
echo ========================================
echo.
pause
