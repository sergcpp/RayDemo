cmake_minimum_required(VERSION 2.8.12)
project(DemoLib)

IF(WIN32)
    set(LIBS turbojpeg-static opengl32)
ELSE()
    set(LIBS turbojpeg-static)
ENDIF()

set(SOURCE_FILES    Viewer.h
                    Viewer.cpp)

set(ENG_SOURCES eng/Config.h
                eng/FrameInfo.h
                eng/GameBase.h
                eng/GameBase.cpp
                eng/GameState.h
                eng/GameStateManager.h
                eng/GameStateManager.cpp
                eng/Halton.h
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
                    states/GSFibTest.h
                    states/GSFibTest.cpp
                    states/GSFilterTest.h
                    states/GSFilterTest.cpp
                    states/GSHDRTest.h
                    states/GSHDRTest.cpp
                    states/GSHybTest.h
                    states/GSHybTest.cpp
                    states/GSNoiseTest.h
                    states/GSNoiseTest.cpp
                    states/GSRayTest.h
                    states/GSRayTest.cpp
                    states/GSLightmapTest.h
                    states/GSLightmapTest.cpp
                    states/GSSamplingTest.h
                    states/GSSamplingTest.cpp
                    states/GSVNDFTest.h
                    states/GSVNDFTest.cpp)

set(GUI_SOURCES gui/BaseElement.h
                gui/BaseElement.cpp
                gui/BitmapFont.h
                gui/BitmapFont.cpp
                gui/FontStorage.h
                gui/Renderer.h
                gui/RendererSW.cpp
                gui/Utils.h
                gui/Utils.cpp)

set(REN_SOURCES ren/Buffer.h
                ren/Buffer.cpp
                ren/Camera.h
                ren/Camera.cpp
                ren/Context.h
                ren/Context.cpp
                ren/ContextSW.cpp
                ren/MMat.h
                ren/MQuat.h
                ren/MVec.h
                ren/Program.h
                ren/ProgramSW.h
                ren/ProgramSW.cpp
                ren/SmallVector.h
                ren/SparseArray.h
                ren/Storage.h
                ren/Texture.h
                ren/TextureSW.h
                ren/TextureSW.cpp
                ren/Utils.h
                ren/Utils.cpp)

list(APPEND ALL_SOURCE_FILES ${SOURCE_FILES})
source_group("src" FILES ${SOURCE_FILES})

list(APPEND ALL_SOURCE_FILES ${ENG_SOURCES})
source_group("src\\eng" FILES ${ENG_SOURCES})

list(APPEND ALL_SOURCE_FILES ${LOAD_SOURCES})
source_group("src\\load" FILES ${LOAD_SOURCES})

list(APPEND ALL_SOURCE_FILES ${STATES_SOURCES})
source_group("src\\states" FILES ${STATES_SOURCES})

list(APPEND ALL_SOURCE_FILES ${GUI_SOURCES})
source_group("src\\ui" FILES ${GUI_SOURCES})

list(APPEND ALL_SOURCE_FILES ${REN_SOURCES})
source_group("src\\ren" FILES ${REN_SOURCES})

add_library(DemoLib STATIC ${ALL_SOURCE_FILES})
target_link_libraries(DemoLib Ray Sys SW ${LIBS})

