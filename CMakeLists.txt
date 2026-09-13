# minimal. add_subdirectory() or FetchContent this, then link `paula`.
cmake_minimum_required(VERSION 3.14)
project(paula CXX)

add_library(paula STATIC paula.cpp)
target_include_directories(paula PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(paula PUBLIC cxx_std_17)

# the example and the tests only build when this is the top-level project.
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    add_executable(paulatest example/example.cpp)
    target_link_libraries(paulatest PRIVATE paula)

    enable_testing()
    add_executable(test_audxlen_zero tests/test_audxlen_zero.cpp)
    target_link_libraries(test_audxlen_zero PRIVATE paula)
    add_test(NAME audxlen_zero COMMAND test_audxlen_zero)
    add_executable(test_audio_interrupt tests/test_audio_interrupt.cpp)
    target_link_libraries(test_audio_interrupt PRIVATE paula)
    add_test(NAME audio_interrupt COMMAND test_audio_interrupt)
endif()
