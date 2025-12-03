cmake_minimum_required(VERSION 3.15)
project(Network
    VERSION 1.0.0
    LANGUAGES CXX
)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGES} -Wall -Wextra")
set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -g -O0 -DDEBUG")
set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -O3 -DNDEBUG")

find_package(Threads REQUIRED)


#set(HEADERS 

#)

#set(SOURCES
    
#)

#add_library(network_lib ${HEADERS} ${SOURCES})

add_executable(app main.cpp)

add_subdirectory(socket_example)

set(RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)      # 可执行文件输出目录
set(LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)      # 共享库输出目录
set(ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)      # 静态库输出目录




message(STATUS "AsyncLogSystem 构建配置完成")
message(STATUS "项目版本: ${PROJECT_VERSION}")
message(STATUS "C++标准: ${CMAKE_CXX_STANDARD}")
message(STATUS "构建类型: ${CMAKE_BUILD_TYPE}")
message(STATUS "输出目录: ${CMAKE_BINARY_DIR}")
message(STATUS "安装前缀: ${CMAKE_INSTALL_PREFIX}")
