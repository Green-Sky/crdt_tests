cmake_minimum_required(VERSION 3.9 FATAL_ERROR)

project(crdt_bench CXX C)

########################################

add_executable(crdt_bench_jpaper_v0
	./v0_jpaper.cpp
)

target_link_libraries(crdt_bench_jpaper_v0 PUBLIC
	crdt_version0
	nlohmann_json::nlohmann_json
)

########################################

if (FALSE)
add_executable(crdt_bench_jpaper_v1
	./test1.cpp
)

target_link_libraries(crdt_bench_jpaper_v1 PUBLIC
	crdt_version1
	nlohmann_json::nlohmann_json
)
endif()

