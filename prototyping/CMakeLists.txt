cmake_minimum_required(VERSION 3.9 FATAL_ERROR)

project(crdt_tests CXX C)

########################################

add_executable(prot1
	./test1.cpp
)

target_compile_features(prot1 PUBLIC cxx_std_17)

target_link_libraries(prot1 PUBLIC
	nlohmann_json::nlohmann_json
)

########################################

add_executable(prot2
	./test2.cpp
)

target_compile_features(prot2 PUBLIC cxx_std_17)

