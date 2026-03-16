cmake_minimum_required(VERSION 3.20)
project(concurrency-playground CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_compile_options(-Wall -Wextra -Wpedantic)

find_package(Threads REQUIRED)

add_subdirectory(src)
