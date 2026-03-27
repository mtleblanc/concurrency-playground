add_library(concurrency_lib socket.cc coroutine.cc)
target_include_directories(concurrency_lib PUBLIC ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(concurrency_lib PUBLIC Threads::Threads)
