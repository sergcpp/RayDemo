cmake_minimum_required(VERSION 2.8.12)
project(Eng)

add_definitions(-DSHINY_IS_COMPILED=FALSE)

IF(WIN32)
    IF(NOT "${CMAKE_GENERATOR_PLATFORM}" STREQUAL "ARM64")
        set(ITT_LIB "libittnotify")
    ENDIF()
ELSE(WIN32)
    if(NOT CMAKE_SYSTEM_NAME MATCHES "Android")
        set(ITT_LIB "ittnotify")
    endif()
    set(CMAKE_CXX_STANDARD  11)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fPIC")
ENDIF(WIN32)

set(SOURCE_FILES Config.h
                 FlowControl.h
                 FlowControl.cpp
                 FrameInfo.h
                 GameBase.h
                 GameBase.cpp
                 GameState.h
                 GameStateManager.h
                 GameStateManager.cpp
                 Log.h
                 Log.cpp
                 ObjectPool.h
                 Random.h
                 Random.cpp
                 TimedInput.h
                 TimedInput.cpp)

set(GO_SOURCE_FILES go/Go.h
                    go/Go.cpp
                    go/GoAlloc.h
                    go/GoComponent.h
                    go/GoID.h)

list(APPEND ALL_SOURCE_FILES ${SOURCE_FILES})
set_source_files_properties(${SOURCE_FILES} PROPERTIES HEADER_FILE_ONLY TRUE)
source_group("src" FILES ${SOURCE_FILES})

list(APPEND ALL_SOURCE_FILES ${GO_SOURCE_FILES})
set_source_files_properties(${GO_SOURCE_FILES} PROPERTIES HEADER_FILE_ONLY TRUE)
source_group("src\\go" FILES ${GO_SOURCE_FILES})

list(APPEND ALL_SOURCE_FILES _Eng.cpp)
source_group("src" FILES _Eng.cpp)

add_library(Eng STATIC ${ALL_SOURCE_FILES})
target_link_libraries(Eng Gui Ren Sys ${ITT_LIB})

add_subdirectory(tests)