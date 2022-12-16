cmake_minimum_required(VERSION 3.9 FATAL_ERROR)

project(crdt_version0 CXX C)

add_library(crdt_version0 INTERFACE)

target_compile_features(crdt_version0 INTERFACE cxx_std_17)

target_include_directories(crdt_version0 INTERFACE "${PROJECT_SOURCE_DIR}")

########################################

add_executable(test1
	./test1.cpp
)

target_link_libraries(test1 PUBLIC crdt_version0)

########################################

add_executable(test2
	./test2.cpp
)

target_link_libraries(test2 PUBLIC crdt_version0)

