cmake_minimum_required(VERSION 3.9 FATAL_ERROR)

project(crdt_version4 CXX C)

add_library(crdt_version4 INTERFACE)

target_compile_features(crdt_version4 INTERFACE cxx_std_17)

target_include_directories(crdt_version4 INTERFACE "${PROJECT_SOURCE_DIR}")

########################################

add_executable(v4_test1
	./test1.cpp
)

target_link_libraries(v4_test1 PUBLIC crdt_version4)

########################################

#add_executable(v4_test2
	#./test2.cpp
#)

#target_link_libraries(v4_test2 PUBLIC crdt_version4)

