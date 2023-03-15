cmake_minimum_required(VERSION 3.1)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CONFIGURATION_TYPES "Debug;Release;RelWithDebInfo;Asan" CACHE STRING "" FORCE)

set(SDL_STATIC ON)

if(WIN32)
    if ("${CMAKE_CXX_COMPILER_ID}" STREQUAL "GNU")
        if(CMAKE_SIZEOF_VOID_P EQUAL 8)
            set(SDL2_INCLUDE_DIRS libs/SDL2/x86_64-w64-mingw32/include)
            set(SDL2_LIBRARIES "libs/SDL2/x86_64-w64-mingw32/lib")
            set(JPEG_LIBRARIES "libs/jpeg/win/x64")
        else(CMAKE_SIZEOF_VOID_P EQUAL 8)
            set(SDL2_INCLUDE_DIRS libs/SDL2/i686-w64-mingw32/include)
            set(SDL2_LIBRARIES "libs/SDL2/i686-w64-mingw32/lib")
        endif()
    elseif ("${CMAKE_CXX_COMPILER_ID}" STREQUAL "MSVC" OR "${CMAKE_CXX_COMPILER_ID}" STREQUAL "Clang")
        if("${CMAKE_GENERATOR_PLATFORM}" STREQUAL "ARM64")
            set(SDL2_LIBRARIES "libs/SDL2/lib/arm64")
            set(JPEG_LIBRARIES "libs/jpeg/win/arm64")
        elseif(CMAKE_SIZEOF_VOID_P EQUAL 8)
            set(SDL2_LIBRARIES "libs/SDL2/lib/x64")
            set(ITT_LIBRARIES ${CMAKE_CURRENT_SOURCE_DIR}/libs/vtune/win/x64)
            set(JPEG_LIBRARIES "libs/jpeg/win/x64")
        else(CMAKE_SIZEOF_VOID_P EQUAL 8)
            set(SDL2_LIBRARIES "libs/SDL2/lib/x86")
            set(ITT_LIBRARIES ${CMAKE_CURRENT_SOURCE_DIR}/libs/vtune/win/x86)
        endif()
        set(SDL2_INCLUDE_DIRS libs/SDL2/include)
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /EHs")
        set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} /fp:except")
        set(CMAKE_CXX_FLAGS_ASAN "/MTd /Zi /Od /RTC1 /fsanitize=address")
        set(CMAKE_EXE_LINKER_FLAGS_ASAN "${CMAKE_EXE_LINKER_FLAGS_DEBUG} /INCREMENTAL:NO")
    endif()

    if("${CMAKE_GENERATOR_PLATFORM}" STREQUAL "ARM64" OR "${CMAKE_CXX_COMPILER_ID}" STREQUAL "Clang")
        add_definitions(-DINTEL_NO_ITTNOTIFY_API)
    endif()
else(WIN32)
    set(CMAKE_CXX_FLAGS_ASAN "-fsanitize=address")
    set(CMAKE_EXE_LINKER_FLAGS_ASAN "-fsanitize=address")

    include(FindPkgConfig)
    pkg_search_module (SDL2 REQUIRED sdl2)

    if(APPLE)
        set(ITT_LIBRARIES ${CMAKE_CURRENT_SOURCE_DIR}/libs/vtune/osx/x64)
        if(CMAKE_APPLE_SILICON_PROCESSOR MATCHES "arm64")
            set(SDL2_INCLUDE_DIRS /opt/homebrew/include)
            set(SDL2_LIBRARIES /opt/homebrew/lib)
            set(JPEG_LIBRARIES "libs/jpeg/osx/arm64")
        else()
            set(SDL2_INCLUDE_DIRS /usr/local/include)
            set(SDL2_LIBRARIES /usr/local/lib)
            set(JPEG_LIBRARIES "libs/jpeg/osx/x64")
        endif()
    else(APPLE)
        set(ITT_LIBRARIES ${CMAKE_CURRENT_SOURCE_DIR}/libs/vtune/linux/x64)
        set(JPEG_LIBRARIES "libs/jpeg/linux/x64")
    endif(APPLE)

    if(CMAKE_HOST_SYSTEM_PROCESSOR MATCHES "arm64")
        add_definitions(-DINTEL_NO_ITTNOTIFY_API)
    endif()
endif(WIN32)

set(JPEG_INCLUDE_DIRS libs/jpeg/include)

include_directories(libs)
include_directories(${SDL2_INCLUDE_DIRS})
include_directories(${JPEG_INCLUDE_DIRS})
include_directories(.)

link_directories(${SDL2_LIBRARIES})
link_directories(${JPEG_LIBRARIES})
link_directories(${ITT_LIBRARIES})

enable_testing()

add_subdirectory(Ray)
add_subdirectory(Sys)
add_subdirectory(SW)
add_subdirectory(DemoLib)
add_subdirectory(DemoApp)
add_subdirectory(libs/SOIL2)

set_property(GLOBAL PROPERTY USE_FOLDERS ON)

set_target_properties(DemoApp
                      DemoLib PROPERTIES FOLDER App)

set_target_properties(Ray
                      Sys
                      SW
                      SOIL2 PROPERTIES FOLDER Libs)

set_target_properties(test_Ray
                      test_SW
                      test_Sys PROPERTIES FOLDER Tests)

add_test(RayTest        Ray/tests/test_Ray)
add_test(SWTest         Ren/SW/tests/test_SW)
add_test(SysTest        Sys/tests/test_Sys)

add_custom_target(Check COMMAND ${CMAKE_CTEST_COMMAND} DEPENDS test_Ray
                                                               test_SW
                                                               test_Sys)
set_target_properties(Check PROPERTIES FOLDER Tests)
