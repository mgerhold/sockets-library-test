add_executable(server main.cpp)

target_link_libraries(server PRIVATE project_options c2k_sockets)
