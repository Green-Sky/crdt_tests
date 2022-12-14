cmake_minimum_required(VERSION 3.9 FATAL_ERROR)

project(crdt_version0 CXX C)

########################################

add_executable(test1
	./list.hpp
	./text_document.hpp
	./test1.cpp
)

target_compile_features(test1 PUBLIC cxx_std_17)

########################################

add_executable(test2
	./list.hpp
	./text_document.hpp
	./test2.cpp
)

target_compile_features(test2 PUBLIC cxx_std_17)

