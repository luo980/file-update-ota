cmake_minimum_required(VERSION 3.0.0)
project(daemon VERSION 0.1.0)

# 设置 C++ 标准
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)


set(PROJECT_NAME daemon)

find_package(CURL REQUIRED)

find_package(nlohmann_json REQUIRED)

find_package(yaml-cpp REQUIRED)


# 添加源代码文件
add_executable(${PROJECT_NAME} src/daemon.cpp)

target_link_libraries(${PROJECT_NAME}  CURL::libcurl nlohmann_json::nlohmann_json yaml-cpp::yaml-cpp ssl crypto)