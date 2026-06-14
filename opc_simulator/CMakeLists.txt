cmake_minimum_required(VERSION 3.15)
project(opcua_simulator CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

add_compile_options(
    $<$<CXX_COMPILER_ID:MSVC>:/W4>
    $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wall -Wextra -Wpedantic>
)

find_package(Boost REQUIRED COMPONENTS system thread)

include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/../include
    ${Boost_INCLUDE_DIRS}
)

add_executable(opcua_simulator src/simulator.cpp)

target_link_libraries(opcua_simulator
    PRIVATE
    ${Boost_LIBRARIES}
    $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:pthread>
)

if(WIN32)
    target_link_libraries(opcua_simulator PRIVATE ws2_32 mswsock)
endif()

install(TARGETS opcua_simulator RUNTIME DESTINATION bin)
