cmake_minimum_required(VERSION 3.16)
project(openveh_someip_sd_cpp LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(GENERATED_PARAMS_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../tools/someip_idl/generated)

add_library(someip_sd_cpp
    src/protocol.cpp
    src/sd_api.cpp
    src/sd_daemon.cpp
)

target_include_directories(someip_sd_cpp PUBLIC include)
target_compile_options(someip_sd_cpp PRIVATE -Wall -Wextra -pedantic)

if(UNIX)
    target_link_libraries(someip_sd_cpp PUBLIC pthread)
endif()

add_executable(run_sd_daemon examples/run_sd_daemon.cpp)
target_link_libraries(run_sd_daemon PRIVATE someip_sd_cpp)

add_executable(provider_demo examples/provider_demo.cpp)
target_link_libraries(provider_demo PRIVATE someip_sd_cpp)
target_include_directories(provider_demo PRIVATE ${GENERATED_PARAMS_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/examples)

add_executable(consumer_demo examples/consumer_demo.cpp)
target_link_libraries(consumer_demo PRIVATE someip_sd_cpp)
target_include_directories(consumer_demo PRIVATE ${GENERATED_PARAMS_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/examples)

add_executable(test_sd_service tests/test_sd_service.cpp)
target_link_libraries(test_sd_service PRIVATE someip_sd_cpp)
