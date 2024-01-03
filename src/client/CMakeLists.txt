add_executable(client main.cpp)

target_link_libraries(client PRIVATE project_options raylib c2k_sockets)
