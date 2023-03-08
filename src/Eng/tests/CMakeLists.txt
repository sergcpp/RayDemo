cmake_minimum_required(VERSION 3.2)
project(test_Eng)

add_executable(test_Eng main.cpp
                        test_common.h)

target_link_libraries(test_Eng Eng)