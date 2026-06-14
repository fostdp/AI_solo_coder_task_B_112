cmake_minimum_required(VERSION 3.15)
project(haihunhou_testsuite)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

enable_testing()

include_directories(${CMAKE_SOURCE_DIR}/include)
include_directories(${CMAKE_SOURCE_DIR}/tests)

add_executable(test_fading_predictor
    tests/test_fading_predictor.cpp
    backend/src/modules/fading_predictor.cpp
    tests/stub_clickhouse.cpp
)
target_compile_definitions(test_fading_predictor PRIVATE TEST_STUB)
add_test(NAME test_fading_predictor COMMAND test_fading_predictor)

add_executable(test_mold_engine
    tests/test_mold_engine.cpp
    backend/src/modules/mold_engine.cpp
    tests/stub_clickhouse.cpp
)
target_compile_definitions(test_mold_engine PRIVATE TEST_STUB)
add_test(NAME test_mold_engine COMMAND test_mold_engine)

add_executable(test_config_loader
    tests/test_config_loader.cpp
    backend/src/modules/config_loader.cpp
)
add_test(NAME test_config_loader COMMAND test_config_loader)

add_executable(test_message_bus
    tests/test_message_bus.cpp
)
add_test(NAME test_message_bus COMMAND test_message_bus)
