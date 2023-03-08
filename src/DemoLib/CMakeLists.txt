cmake_minimum_required(VERSION 2.8.12)
project(DemoLib)

set(LIBS turbojpeg-static)

set(SOURCE_FILES    Viewer.h
                    Viewer.cpp)

set(ENG_SOURCES eng/Config.h
		        eng/FrameInfo.h
		        eng/GameBase.h
		        eng/GameBase.cpp
		        eng/GameState.h
		        eng/GameStateManager.h
		        eng/GameStateManager.cpp
		        eng/Log.h
		        eng/Log.cpp
		        eng/Random.h
		        eng/Random.cpp
		        eng/TimedInput.h
		        eng/TimedInput.cpp)

set(LOAD_SOURCES  load/Load.h
				  load/Load.cpp)

set(STATES_SOURCES  states/GSCreate.h
					states/GSCreate.cpp
					states/GSCPUTest.h
					states/GSCPUTest.cpp
					states/GSHDRTest.h
					states/GSHDRTest.cpp
					states/GSHybTest.h
					states/GSHybTest.cpp
					states/GSRayTest.h
					states/GSRayTest.cpp
					states/GSRayBucketTest.h
					states/GSRayBucketTest.cpp
					states/GSLightmapTest.h
					states/GSLightmapTest.cpp
					states/GSSamplingTest.h
					states/GSSamplingTest.cpp
                    states/GSVNDFTest.h
                    states/GSVNDFTest.cpp)

set(UI_SOURCES  ui/FontStorage.h)

if(${RENDERER} STREQUAL "GL")
    set(RENDERER_SOURCES ${RENDERER_SOURCES}
                         renderer/RendererGL.cpp)
elseif(${RENDERER} STREQUAL "SW")
    set(RENDERER_SOURCES ${RENDERER_SOURCES}
                         renderer/RendererSW.cpp)
endif()

list(APPEND ALL_SOURCE_FILES ${SOURCE_FILES})
source_group("src" FILES ${SOURCE_FILES})

list(APPEND ALL_SOURCE_FILES ${ENG_SOURCES})
source_group("src\\eng" FILES ${ENG_SOURCES})

list(APPEND ALL_SOURCE_FILES ${LOAD_SOURCES})
source_group("src\\load" FILES ${LOAD_SOURCES})

list(APPEND ALL_SOURCE_FILES ${STATES_SOURCES})
source_group("src\\states" FILES ${STATES_SOURCES})

list(APPEND ALL_SOURCE_FILES ${UI_SOURCES})
set_source_files_properties(${UI_SOURCES} PROPERTIES HEADER_FILE_ONLY TRUE)
source_group("src\\ui" FILES ${UI_SOURCES})

add_library(DemoLib STATIC ${ALL_SOURCE_FILES})
target_link_libraries(DemoLib Gui Ray Ren Sys ${LIBS})

