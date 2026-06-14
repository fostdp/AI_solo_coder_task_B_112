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

add_executable(test_cnn_matcher
    tests/test_cnn_matcher.cpp
    backend/src/modules/cnn_matcher.cpp
    tests/stub_clickhouse.cpp
)
target_compile_definitions(test_cnn_matcher PRIVATE TEST_STUB)
add_test(NAME test_cnn_matcher COMMAND test_cnn_matcher)

add_executable(test_plsr_inversion
    tests/test_plsr_inversion.cpp
    backend/src/modules/plsr_inversion.cpp
    tests/stub_clickhouse.cpp
)
target_compile_definitions(test_plsr_inversion PRIVATE TEST_STUB)
add_test(NAME test_plsr_inversion COMMAND test_plsr_inversion)

add_executable(test_rf_corrosion
    tests/test_rf_corrosion.cpp
    backend/src/modules/rf_corrosion.cpp
    tests/stub_clickhouse.cpp
)
target_compile_definitions(test_rf_corrosion PRIVATE TEST_STUB)
add_test(NAME test_rf_corrosion COMMAND test_rf_corrosion)

add_executable(test_bayes_opt
    tests/test_bayes_opt.cpp
    backend/src/modules/bayes_opt.cpp
    tests/stub_clickhouse.cpp
)
target_compile_definitions(test_bayes_opt PRIVATE TEST_STUB)
add_test(NAME test_bayes_opt COMMAND test_bayes_opt)
