add_executable(client_tcp client_tcp.cpp)
target_link_libraries(client_tcp Threads::Threads)


add_executable(server_tcp server_tcp.cpp)
target_link_libraries(server_tcp Threads::Threads)


add_executable(server_udp server_udp.cpp)
target_link_libraries(server_udp Threads::Threads)

add_executable(client_udp client_udp.cpp)
target_link_libraries(client_udp Threads::Threads)