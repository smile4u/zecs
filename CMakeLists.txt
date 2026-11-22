cmake_minimum_required(VERSION 3.25)

project("zecs" VERSION 1.0 LANGUAGES CXX)

option(BUILD_SHARED_LIBS "Build the shared library" OFF)
option(BUILD_TESTS "Build test programs" OFF)
option(ZECS_EXPERIMENTAL_EVENTS "Turn on the events API. It is a draft: see roadmap.md" OFF)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(SRC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/src")
set(EXT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/ext")
set(TST_DIR "${CMAKE_CURRENT_SOURCE_DIR}/tests")

# Рекурсивно собираем все исходники
file(GLOB_RECURSE LIB_SOURCES CONFIGURE_DEPENDS "${SRC_DIR}/*.cpp" "${SRC_DIR}/*.h")
file(GLOB_RECURSE EXT_SOURCES CONFIGURE_DEPENDS "${EXT_DIR}/*.cpp" "${EXT_DIR}/*.h")
file(GLOB_RECURSE TST_SOURCES CONFIGURE_DEPENDS "${TST_DIR}/*.cpp" "${TST_DIR}/*.h")

source_group("src" FILES ${LIB_SOURCES})
source_group("ext" FILES ${EXT_SOURCES})
source_group("tests" FILES ${TST_SOURCES})

add_library(${PROJECT_NAME} OBJECT ${LIB_SOURCES} ${EXT_SOURCES})
target_include_directories(${PROJECT_NAME} PUBLIC ${SRC_DIR} ${EXT_DIR})

if(ZECS_EXPERIMENTAL_EVENTS)
    target_compile_definitions(${PROJECT_NAME} PUBLIC ZECS_EXPERIMENTAL_EVENTS)
endif()

if(BUILD_SHARED_LIBS)
    add_library(${PROJECT_NAME}_shared SHARED $<TARGET_OBJECTS:${PROJECT_NAME}>)
    target_include_directories(${PROJECT_NAME}_shared PUBLIC ${SRC_DIR} ${EXT_DIR})
    set_target_properties(${PROJECT_NAME}_shared PROPERTIES WINDOWS_EXPORT_ALL_SYMBOLS ON)
    if(ZECS_EXPERIMENTAL_EVENTS)
        target_compile_definitions(${PROJECT_NAME}_shared PUBLIC ZECS_EXPERIMENTAL_EVENTS)
    endif()
endif()

if(BUILD_TESTS)
    if(BUILD_SHARED_LIBS)
        set(ZECS_LIB ${PROJECT_NAME}_shared)
    else()
        set(ZECS_LIB ${PROJECT_NAME})
    endif()

    add_executable(${PROJECT_NAME}_tests ${TST_SOURCES})
    target_link_libraries(${PROJECT_NAME}_tests PRIVATE ${ZECS_LIB})
    target_include_directories(${PROJECT_NAME}_tests PRIVATE ${TST_DIR} ${EXT_DIR})
    # The tests always check the events API.
    target_compile_definitions(${PROJECT_NAME}_tests PRIVATE ZECS_EXPERIMENTAL_EVENTS)
endif()
