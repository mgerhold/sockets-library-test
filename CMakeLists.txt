cmake_minimum_required(VERSION 3.27)
project(network_test)

set(CMAKE_CXX_STANDARD 23)

include(${PROJECT_SOURCE_DIR}/project_options.cmake)
include(${PROJECT_SOURCE_DIR}/cmake/system_link.cmake)

include(${PROJECT_SOURCE_DIR}/dependencies.cmake)
setup_dependencies()

add_subdirectory(src)
