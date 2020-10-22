cmake_minimum_required(VERSION 3.15)
if(${CMAKE_VERSION} VERSION_LESS 3.15)
    cmake_policy(VERSION ${CMAKE_MAJOR_VERSION}.${CMAKE_MINOR_VERSION})
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

project(lustre-io-test
		VERSION 1.0
        DESCRIPTION "Tools to test parallel I/O on Lustre"
        LANGUAGES CXX)

set(DEPENDENCIES "")
set(LIBRARIES "lustreapi")

set(BUILD_DIR $ENV{HOME}/group_pawsey/build/${CMAKE_PROJECT_NAME})

# Separate debug-release paths
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_DEBUG ${BUILD_DIR}/bin/debug)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_RELEASE ${BUILD_DIR}/bin/release)
include_directories(${CMAKE_SOURCE_DIR}/include/lustre_include_patched)

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