# A small project that uses an installed ZECS. The CI builds and runs it
# to check the install rules.
cmake_minimum_required(VERSION 3.21)

project(zecs_package_test LANGUAGES CXX)

find_package(zecs 0.1 REQUIRED)

add_executable(zecs_package_test main.cpp)
target_link_libraries(zecs_package_test PRIVATE zecs)
