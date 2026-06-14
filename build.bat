@echo off
echo ========================================
echo  海昏侯简牍监测系统 - Windows 构建脚本
echo ========================================
echo.

set BUILD_DIR=build
set CMAKE_GENERATOR="Visual Studio 17 2022"

echo [1/4] 检查依赖...
where cmake >nul 2>&1
if errorlevel 1 (
    echo [ERROR] 未找到 CMake，请先安装 CMake
    exit /b 1
)

where cl >nul 2>&1
if errorlevel 1 (
    echo [WARNING] 未找到 MSVC 编译器，请确保已安装 Visual Studio
    echo           尝试从开始菜单打开 "x64 Native Tools Command Prompt for VS"
)

echo.
echo [2/4] 创建构建目录...
if not exist %BUILD_DIR% mkdir %BUILD_DIR%

echo.
echo [3/4] 配置 CMake 项目...
cd %BUILD_DIR%
cmake .. -G %CMAKE_GENERATOR% -DCMAKE_BUILD_TYPE=Release
if errorlevel 1 (
    echo [ERROR] CMake 配置失败
    cd ..
    exit /b 1
)

echo.
echo [4/4] 编译项目...
cmake --build . --config Release -j 8
if errorlevel 1 (
    echo [ERROR] 编译失败
    cd ..
    exit /b 1
)

cd ..
echo.
echo ========================================
echo  构建完成！
echo  可执行文件位置: %BUILD_DIR%\backend\Release\haihunhou_server.exe
echo                %BUILD_DIR%\opc_simulator\Release\opcua_simulator.exe
echo ========================================
pause
