cmake_minimum_required(VERSION 3.9 FATAL_ERROR)

project(crdt_version2 CXX C)

add_library(crdt_version2 INTERFACE)

target_compile_features(crdt_version2 INTERFACE cxx_std_17)

target_include_directories(crdt_version2 INTERFACE "${PROJECT_SOURCE_DIR}")

########################################

add_executable(v2_test1
	./test1.cpp
)

target_link_libraries(v2_test1 PUBLIC crdt_version2)

########################################

#add_executable(v2_test2
	#./test2.cpp
#)

#target_link_libraries(v2_test2 PUBLIC crdt_version2)

