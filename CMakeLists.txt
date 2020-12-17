# Building with cmake:
# Create and enter a build directory
# If compiling without GNU readline run 'cmake .. && make'
# If compiling with GNU readline run 'cmake -DREADLINE=1 .. && make'

project(arc++)
cmake_minimum_required(VERSION 2.8)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=gnu++17")

# Source files
set(SOURCES main.cpp arc.cpp)

# The target executable
add_executable(arc++ ${SOURCES})

# Always link stdmath
target_link_libraries(arc++ m)

# Only link GNU readline if we're compiling using it
if (READLINE)
	target_link_libraries(arc++ m readline)
endif()
