# minimal. add_subdirectory() or FetchContent this, then link `paula`.
cmake_minimum_required(VERSION 3.14)
project(paula CXX)

add_library(paula STATIC paula.cpp)
target_include_directories(paula PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(paula PUBLIC cxx_std_17)

# the example only builds when this is the top-level project.
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    add_executable(paulatest example/example.cpp)
    target_link_libraries(paulatest PRIVATE paula)
endif()
