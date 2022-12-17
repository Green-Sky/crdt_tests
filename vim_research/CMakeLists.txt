cmake_minimum_required(VERSION 3.9 FATAL_ERROR)

project(vim_research CXX C)

########################################

add_executable(vim_research_test1
	./test1.cpp
)

target_link_libraries(vim_research_test1 PUBLIC
	crdt_version0
	zed_net
	nlohmann_json::nlohmann_json
)

########################################

add_executable(vim_research_test2
	./test2.cpp
)

target_link_libraries(vim_research_test2 PUBLIC
	crdt_version0
	zed_net
	nlohmann_json::nlohmann_json
	toxcore
)

