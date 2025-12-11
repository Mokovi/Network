# Linux网络编程知识点总结项目 - CMake构建配置
# ================================================================================
# 项目信息
# ================================================================================
cmake_minimum_required(VERSION 3.15)
project(network_programming_examples
    VERSION 1.0.0
    DESCRIPTION "Linux网络编程知识点总结与实践示例"
    LANGUAGES C CXX
)

# 设置C++标准
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# 编译选项
add_compile_options(
    -Wall
    -Wextra
    -Wpedantic
    -Wno-unused-parameter
)

# 根据构建类型设置编译选项
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    add_compile_options(-g -O0 -DDEBUG)
elseif(CMAKE_BUILD_TYPE STREQUAL "Release")
    add_compile_options(-O3 -DNDEBUG)
endif()

# 查找依赖库
find_package(Threads REQUIRED)

# 查找liburing (用于io_uring示例)
find_library(URING_LIBRARY uring)
if(URING_LIBRARY)
    message(STATUS "Found liburing: ${URING_LIBRARY}")
    set(HAVE_LIBURING TRUE)
else()
    message(WARNING "liburing not found - io_uring examples will not be built")
    set(HAVE_LIBURING FALSE)
endif()

# ================================================================================
# 构建目标配置
# ================================================================================

# 基础网络编程示例 (pratice目录)
# ================================================================================

# 1. 大小端序测试
add_executable(1_endianness pratice/1_endianness.cpp)

# 2. UDP服务器
add_executable(2_udpServer pratice/2_udpServer.cpp)

# 3. TCP服务器
add_executable(3_tcpServer pratice/3_tcpServer.cpp)

# 4. 非阻塞TCP服务器
add_executable(4_nonBlockTcpS pratice/4_nonBlockTcpS.cpp)

# 5. select多路复用
add_executable(5_select pratice/5_select.cpp)

# 6. poll多路复用
add_executable(6_poll pratice/6_poll.cpp)

# 7. epoll多路复用
add_executable(7_epoll pratice/7_epoll.cpp)

# 8. io_uring异步I/O (需要liburing)
if(HAVE_LIBURING)
    add_executable(8_iouring pratice/8_iouring.cpp)
    target_link_libraries(8_iouring uring)
    target_compile_definitions(8_iouring PRIVATE HAVE_LIBURING)
else()
    message(STATUS "Skipping 8_iouring - liburing not available")
endif()

# 服务器模型示例 (serverModel目录)
# ================================================================================

# 0. 线程池库 (作为静态库构建)
add_library(threadpool STATIC serverModel/0_threadpool.h)
set_target_properties(threadpool PROPERTIES LINKER_LANGUAGE C)
target_link_libraries(threadpool Threads::Threads)

# 1. 线程每连接服务器
add_executable(1_threadPerConn serverModel/1_threadPerConn.cpp)
target_link_libraries(1_threadPerConn Threads::Threads)

# 2. 线程池服务器 (C语言实现)
add_executable(2_threadpoolServer serverModel/2_threadpoolServer.c serverModel/0_threadpool.h)
target_link_libraries(2_threadpoolServer Threads::Threads)

# 3. Reactor模式epoll服务器
add_executable(3_reactor_epoll_server serverModel/3_reactor_epoll_server.c)
target_link_libraries(3_reactor_epoll_server Threads::Threads)

# 4. Reactor+线程池+epoll服务器
add_executable(4_reactor_threadpool_epoll serverModel/4_reactor_threadpool_epoll.c)
target_link_libraries(4_reactor_threadpool_epoll Threads::Threads)

# 5. Proactor模式服务器
if(HAVE_LIBURING)
    add_executable(5_proactor serverModel/5_proactor.c)
    target_link_libraries(5_proactor uring Threads::Threads)
    target_compile_definitions(5_proactor PRIVATE HAVE_LIBURING)
else()
    message(STATUS "Skipping 5_proactor - liburing not available")
endif()

# ================================================================================
# 构建目录配置
# ================================================================================

# 设置构建输出目录
set(EXECUTABLE_OUTPUT_PATH ${CMAKE_BINARY_DIR}/bin)
set(LIBRARY_OUTPUT_PATH ${CMAKE_BINARY_DIR}/lib)


# ================================================================================
# 自定义目标
# ================================================================================

# 清理目标
add_custom_target(clean-all
    COMMAND ${CMAKE_COMMAND} -E remove_directory ${CMAKE_BINARY_DIR}/bin
    COMMAND ${CMAKE_COMMAND} -E remove_directory ${CMAKE_BINARY_DIR}/lib
    COMMENT "Cleaning all build artifacts"
)

# 运行所有测试目标
add_custom_target(run-all
    COMMAND ${CMAKE_COMMAND} -E echo "Building all examples..."
    COMMAND ${CMAKE_MAKE_PROGRAM} -j$(nproc)
    COMMAND ${CMAKE_COMMAND} -E echo "All examples built successfully!"
    COMMENT "Building all examples"
)

# 帮助目标
add_custom_target(help-examples
    COMMAND ${CMAKE_COMMAND} -E echo ""
    COMMAND ${CMAKE_COMMAND} -E echo "=== Linux网络编程示例程序 ==="
    COMMAND ${CMAKE_COMMAND} -E echo ""
    COMMAND ${CMAKE_COMMAND} -E echo "基础网络编程示例:"
    COMMAND ${CMAKE_COMMAND} -E echo "  1_endianness     - 大小端序测试"
    COMMAND ${CMAKE_COMMAND} -E echo "  2_udpServer      - UDP服务器"
    COMMAND ${CMAKE_COMMAND} -E echo "  3_tcpServer      - TCP服务器"
    COMMAND ${CMAKE_COMMAND} -E echo "  4_nonBlockTcpS   - 非阻塞TCP服务器"
    COMMAND ${CMAKE_COMMAND} -E echo "  5_select         - select多路复用"
    COMMAND ${CMAKE_COMMAND} -E echo "  6_poll           - poll多路复用"
    COMMAND ${CMAKE_COMMAND} -E echo "  7_epoll          - epoll多路复用"
    COMMAND ${CMAKE_COMMAND} -E echo "  8_iouring        - io_uring异步I/O"
    COMMAND ${CMAKE_COMMAND} -E echo ""
    COMMAND ${CMAKE_COMMAND} -E echo "服务器架构模型示例:"
    COMMAND ${CMAKE_COMMAND} -E echo "  1_threadPerConn         - 线程每连接模型"
    COMMAND ${CMAKE_COMMAND} -E echo "  2_threadpoolServer      - 线程池服务器"
    COMMAND ${CMAKE_COMMAND} -E echo "  3_reactor_epoll_server - Reactor模式epoll服务器"
    COMMAND ${CMAKE_COMMAND} -E echo "  4_reactor_threadpool_epoll - Reactor+线程池+epoll"
    COMMAND ${CMAKE_COMMAND} -E echo "  5_proactor              - Proactor模式"
    COMMAND ${CMAKE_COMMAND} -E echo ""
    COMMAND ${CMAKE_COMMAND} -E echo "构建命令:"
    COMMAND ${CMAKE_COMMAND} -E echo "  mkdir build && cd build"
    COMMAND ${CMAKE_COMMAND} -E echo "  cmake .. && make -j4"
    COMMAND ${CMAKE_COMMAND} -E echo ""
    COMMAND ${CMAKE_COMMAND} -E echo "运行示例:"
    COMMAND ${CMAKE_COMMAND} -E echo "  ./bin/program_name"
    COMMENT "Display help information"
)

# 默认目标设置为help-examples
set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT help-examples)

# ================================================================================
# 项目总结信息
# ================================================================================

message(STATUS "")
message(STATUS "=== Linux网络编程示例项目配置完成 ===")
message(STATUS "")
message(STATUS "项目版本: ${PROJECT_VERSION}")
message(STATUS "C++标准: C++${CMAKE_CXX_STANDARD}")
message(STATUS "C标准: C${CMAKE_C_STANDARD}")
message(STATUS "构建类型: ${CMAKE_BUILD_TYPE}")
if(HAVE_LIBURING)
    message(STATUS "liburing支持: 是")
else()
    message(STATUS "liburing支持: 否 (io_uring示例将被跳过)")
endif()
message(STATUS "")
message(STATUS "构建命令:")
message(STATUS "  mkdir build && cd build")
message(STATUS "  cmake ..")
message(STATUS "  make -j$(nproc)")
message(STATUS "")
message(STATUS "运行 'make help-examples' 查看所有可用示例")
message(STATUS "")
