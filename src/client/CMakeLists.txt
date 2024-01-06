add_executable(client main.cpp)

target_link_system_libraries(client PRIVATE raylib)
target_link_libraries(client PRIVATE project_options c2k_sockets)
