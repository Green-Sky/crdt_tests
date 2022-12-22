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
add_subdirectory(./external/zed_net)
add_subdirectory(./external/toxcore)

# Bump up warning levels appropriately for clang, gcc & msvc
if (${CMAKE_CXX_COMPILER_ID} STREQUAL "GNU" OR ${CMAKE_CXX_COMPILER_ID} STREQUAL "Clang")
	set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -pedantic")

	link_libraries(-fsanitize=address,undefined)
elseif (${CMAKE_CXX_COMPILER_ID} STREQUAL "MSVC")
	if (CMAKE_CXX_FLAGS MATCHES "/W[0-4]")
		string(REGEX REPLACE "/W[0-4]" "/W4" CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")
	else()
		set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /W4")
	endif()
endif()

add_subdirectory(./prototyping EXCLUDE_FROM_ALL)

add_subdirectory(./version0)
add_subdirectory(./version1)
add_subdirectory(./version2)

add_subdirectory(./bench)

add_subdirectory(./vim_research)

