cmake_minimum_required(VERSION 3.10)
project(hdic C)


set(CMAKE_INSTALL_PREFIX "/home/huashi/code/")
# set(CMAKE_BUILD_TYPE Debug)
set(CMAKE_BUILD_TYPE Release)



include_directories(
    /home/huashi/code/include
    ${CMAKE_SOURCE_DIR}/include
    )
link_directories(/home/huashi/code/lib)

file(GLOB SRC_SRCS "src/*.c")

add_executable(hdic ${SRC_SRCS})
target_link_libraries(hdic PUBLIC zlog iniparser hbase)



install(TARGETS
    hdic
    RUNTIME DESTINATION bin
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
)



