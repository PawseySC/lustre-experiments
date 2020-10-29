# BSD 3-Clause License
# 
# Copyright (c) 2020, Commonwealth Scientific and Industrial Research 
# Organisation (CSIRO) and The Pawsey Supercomputing Centre
# 
# Author: Ugo Varetto
# All rights reserved.
# 
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are met:
# 
# 1. Redistributions of source code must retain the above copyright notice,
#    this list of conditions and the following disclaimer.
# 
# 2. Redistributions in binary form must reproduce the above copyright notice,
#    this list of conditions and the following disclaimer in the documentation
#    and/or other materials provided with the distribution.
# 
# 3. Neither the name of the copyright holder nor the names of its
#    contributors may be used to endorse or promote products derived from
#    this software without specific prior written permission.
# 
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
# AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
# IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
# ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
# LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
# CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
# SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
# INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
# CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
# ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
# POSSIBILITY OF SUCH DAMAGE.
 
cmake_minimum_required(VERSION 3.15)
if(${CMAKE_VERSION} VERSION_LESS 3.15)
    cmake_policy(VERSION ${CMAKE_MAJOR_VERSION}.${CMAKE_MINOR_VERSION})
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

project(lustre-io-test
		VERSION 1.0
        DESCRIPTION "Tools to test parallel I/O on Lustre without MPI"
        LANGUAGES CXX)

#dependencies
set(Lyra ${CMAKE_SOURCE_DIR}/dep/Lyra) #command line parsing
    
set(LIBRARIES "lustreapi")

set(BUILD_DIR $ENV{HOME}/group_pawsey/build/${CMAKE_PROJECT_NAME})

# Separate debug-release paths
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_DEBUG ${BUILD_DIR}/bin/debug)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_RELEASE ${BUILD_DIR}/bin/release)
include_directories(${CMAKE_SOURCE_DIR}/include/lustre_include_patched)
include_directories(${Lyra}/include)

find_package(Threads)

add_executable(genseq src/genseq.cpp)

add_executable(print_version src/print_version.cpp)
add_executable(create_file src/create_file.cpp)
add_executable(print_layout_info src/print_layout.cpp)
add_executable(read_test src/read_test.cpp)
# add_executable(write_test src/write_test.cpp)
# add_executable(parallel_copy src/parallel_copy.cpp)

target_link_libraries(print_version ${LIBRARIES})
target_link_libraries(create_file ${LIBRARIES})
target_link_libraries(print_layout_info ${LIBRARIES})
target_link_libraries(read_test ${LIBRARIES} ${CMAKE_THREAD_LIBS_INIT})
# target_link_libraries(write_test ${LIBRARIES} ${CMAKE_THREAD_LIBS_INIT})
# target_link_libraries(parallel_copy ${LIBRARIES} ${CMAKE_THREAD_LIBS_INIT})

# no dependencies can be compiled separately on the command line:
# g++ -pthread simple_***_test.cpp -O2 -o simple_***_test
add_executable(simple_read_test src/simple_read_test.cpp)
add_executable(simple_write_test src/simple_write_test.cpp)
target_link_libraries(simple_read_test ${CMAKE_THREAD_LIBS_INIT})
target_link_libraries(simple_write_test ${CMAKE_THREAD_LIBS_INIT})