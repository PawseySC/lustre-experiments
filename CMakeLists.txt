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
add_executable(read_test_api src/read_test_api.cpp)
# add_executable(read_test_posix src/read_test_posix.cpp)
# add_executable(read_test_api_mm src/read_test_api_mm.cpp)
# add_executable(read_test_posix_mm src/read_test_posix_mm.cpp)
# add_executable(write_test_api src/write_test_api.cpp)
# add_executable(write_test_posix src/write_test_posix.cpp)
# add_executable(write_test_api_mm src/write_test_api_mm.cpp)
# add_executable(write_test_posix_mm src/write_test_posix_mm.cpp)
# add_executable(parallel_copy_posix src/parallel_copy_posix.cpp)
# add_executable(parallel_copy_api src/parallel_copy_api.cpp
# add_executable(parallel_copy_posix_mm src/parallel_copy_posix.cpp)
# add_executable(parallel_copy_api_mm src/parallel_api_mm.cpp)

target_link_libraries(print_version ${LIBRARIES})
target_link_libraries(create_file ${LIBRARIES})
target_link_libraries(print_layout_info ${LIBRARIES})
target_link_libraries(read_test_api ${LIBRARIES})
target_link_libraries(read_test_api ${CMAKE_THREAD_LIBS_INIT})
# target_link_libraries(read_test_posix ${LIBRARIES})
# target_link_libraries(read_test_api_mm ${LIBRARIES})
# target_link_libraries(read_test_posix_mm ${LIBRARIES})
# target_link_libraries(write_test_api ${LIBRARIES})
# target_link_libraries(write_test_posix ${LIBRARIES})
# target_link_libraries(write_test_api_mm ${LIBRARIES})
# target_link_libraries(write_test_posix_mm ${LIBRARIES})
# target_link_libraries(parallel_copy_api ${LIBRARIES})
# target_link_libraries(parallel_copy_posix ${LIBRARIES})
# target_link_libraries(parallel_copy_api_mm ${LIBRARIES})
# target_link_libraries(parallel_copy_posix_mm ${LIBRARIES})