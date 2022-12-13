cmake_minimum_required(VERSION 3.9 FATAL_ERROR)

project(crdt_version0 CXX C)

########################################

add_executable(test1
	./test1.cpp
)

target_compile_features(test1 PUBLIC cxx_std_17)

