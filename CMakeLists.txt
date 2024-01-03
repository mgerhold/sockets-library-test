cmake_minimum_required(VERSION 3.27)
project(network_test)

set(CMAKE_CXX_STANDARD 23)

include(dependencies.cmake)
setup_dependencies()

include(project_options.cmake)

add_subdirectory(src)
