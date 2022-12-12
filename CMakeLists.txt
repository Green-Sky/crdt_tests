cmake_minimum_required(VERSION 3.9 FATAL_ERROR)

project(crdt_tests CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE)
	set(CMAKE_BUILD_TYPE "Debug")
endif()

set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

#option(BUILD_TESTING "Enable testing with ctest." OFF)

add_subdirectory(./external/json)

# Bump up warning levels appropriately for clang, gcc & msvc
if (${CMAKE_CXX_COMPILER_ID} STREQUAL "GNU" OR ${CMAKE_CXX_COMPILER_ID} STREQUAL "Clang")
	set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -pedantic")

	#link_libraries(-fsanitize=address)
elseif (${CMAKE_CXX_COMPILER_ID} STREQUAL "MSVC")
	if (CMAKE_CXX_FLAGS MATCHES "/W[0-4]")
		string(REGEX REPLACE "/W[0-4]" "/W4" CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")
	else()
		set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /W4")
	endif()
endif()

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
