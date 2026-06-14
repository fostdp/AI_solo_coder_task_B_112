cmake_minimum_required(VERSION 3.15)
project(haihunhou_testsuite)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

enable_testing()

include_directories(${CMAKE_SOURCE_DIR}/include)
include_directories(${CMAKE_SOURCE_DIR}/tests)

option(BUILD_SHARED_LIBS "Build shared libraries" ON)

add_compile_definitions(HAIHUNHOU_BUILD_SHARED)

# ============ Shared Libraries (4 Modules + core) ============

add_library(thread_pool SHARED
    backend/src/thread_pool.cpp
)
target_include_directories(thread_pool PUBLIC ${CMAKE_SOURCE_DIR}/include)

add_library(cnn_matcher SHARED
    backend/src/modules/cnn_matcher.cpp
)
target_include_directories(cnn_matcher PUBLIC ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(cnn_matcher PUBLIC thread_pool)

add_library(plsr_inversion SHARED
    backend/src/modules/plsr_inversion.cpp
)
target_include_directories(plsr_inversion PUBLIC ${CMAKE_SOURCE_DIR}/include)

add_library(rf_corrosion SHARED
    backend/src/modules/rf_corrosion.cpp
)
target_include_directories(rf_corrosion PUBLIC ${CMAKE_SOURCE_DIR}/include)

add_library(bayes_opt SHARED
    backend/src/modules/bayes_opt.cpp
    backend/src/modules/bayes_process.cpp
)
target_include_directories(bayes_opt PUBLIC ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(bayes_opt PUBLIC thread_pool)

add_library(haihunhou_core INTERFACE)
target_link_libraries(haihunhou_core INTERFACE
    cnn_matcher plsr_inversion rf_corrosion bayes_opt thread_pool
)

# ============ Existing Unit Tests ============

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
    tests/stub_clickhouse.cpp
)
target_compile_definitions(test_cnn_matcher PRIVATE TEST_STUB)
target_link_libraries(test_cnn_matcher PRIVATE cnn_matcher)
add_test(NAME test_cnn_matcher COMMAND test_cnn_matcher)

add_executable(test_plsr_inversion
    tests/test_plsr_inversion.cpp
    tests/stub_clickhouse.cpp
)
target_compile_definitions(test_plsr_inversion PRIVATE TEST_STUB)
target_link_libraries(test_plsr_inversion PRIVATE plsr_inversion)
add_test(NAME test_plsr_inversion COMMAND test_plsr_inversion)

add_executable(test_rf_corrosion
    tests/test_rf_corrosion.cpp
    tests/stub_clickhouse.cpp
)
target_compile_definitions(test_rf_corrosion PRIVATE TEST_STUB)
target_link_libraries(test_rf_corrosion PRIVATE rf_corrosion)
add_test(NAME test_rf_corrosion COMMAND test_rf_corrosion)

add_executable(test_bayes_opt
    tests/test_bayes_opt.cpp
    tests/stub_clickhouse.cpp
)
target_compile_definitions(test_bayes_opt PRIVATE TEST_STUB)
target_link_libraries(test_bayes_opt PRIVATE bayes_opt)
add_test(NAME test_bayes_opt COMMAND test_bayes_opt)

add_executable(test_enhanced_slip_matching
    tests/test_enhanced_slip_matching.cpp
    tests/stub_clickhouse.cpp
)
target_compile_definitions(test_enhanced_slip_matching PRIVATE TEST_STUB)
target_link_libraries(test_enhanced_slip_matching PRIVATE cnn_matcher)
add_test(NAME test_enhanced_slip_matching COMMAND test_enhanced_slip_matching)

add_executable(test_enhanced_plsr
    tests/test_enhanced_plsr.cpp
    tests/stub_clickhouse.cpp
)
target_compile_definitions(test_enhanced_plsr PRIVATE TEST_STUB)
target_link_libraries(test_enhanced_plsr PRIVATE plsr_inversion)
add_test(NAME test_enhanced_plsr COMMAND test_enhanced_plsr)

add_executable(test_enhanced_rf
    tests/test_enhanced_rf.cpp
    tests/stub_clickhouse.cpp
)
target_compile_definitions(test_enhanced_rf PRIVATE TEST_STUB)
target_link_libraries(test_enhanced_rf PRIVATE rf_corrosion)
add_test(NAME test_enhanced_rf COMMAND test_enhanced_rf)

add_executable(test_enhanced_bayes
    tests/test_enhanced_bayes.cpp
    tests/stub_clickhouse.cpp
)
target_compile_definitions(test_enhanced_bayes PRIVATE TEST_STUB)
target_link_libraries(test_enhanced_bayes PRIVATE bayes_opt)
add_test(NAME test_enhanced_bayes COMMAND test_enhanced_bayes)

add_executable(test_defect_fixes
    tests/test_defect_fixes.cpp
    tests/stub_clickhouse.cpp
)
target_compile_definitions(test_defect_fixes PRIVATE TEST_STUB)
target_include_directories(test_defect_fixes PRIVATE include tests)
target_link_libraries(test_defect_fixes PRIVATE cnn_matcher plsr_inversion bayes_opt)
add_test(NAME test_defect_fixes COMMAND test_defect_fixes)

# ============ New Refactor Tests ============

add_executable(test_session_pool
    tests/test_session_pool.cpp
    backend/src/opcua/session_pool.cpp
)
target_compile_definitions(test_session_pool PRIVATE TEST_STUB)
target_include_directories(test_session_pool PRIVATE include tests)
add_test(NAME test_session_pool COMMAND test_session_pool)

add_executable(test_bayes_penalty
    tests/test_bayes_penalty.cpp
    tests/stub_clickhouse.cpp
)
target_compile_definitions(test_bayes_penalty PRIVATE TEST_STUB)
target_include_directories(test_bayes_penalty PRIVATE include tests)
target_link_libraries(test_bayes_penalty PRIVATE bayes_opt)
add_test(NAME test_bayes_penalty COMMAND test_bayes_penalty)
