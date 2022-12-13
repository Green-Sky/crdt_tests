cmake_minimum_required(VERSION 3.9 FATAL_ERROR)

project(crdt_tests CXX C)

########################################

add_executable(test1
	./test1.cpp
)

target_compile_features(test1 PUBLIC cxx_std_17)

target_link_libraries(test1 PUBLIC
	nlohmann_json::nlohmann_json
)

########################################

add_executable(test2
	./test2.cpp
)

target_compile_features(test2 PUBLIC cxx_std_17)

target_link_libraries(test2 PUBLIC
	nlohmann_json::nlohmann_json
)

