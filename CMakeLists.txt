cmake_minimum_required(VERSION 3.21)

project(zecs VERSION 0.1.0 LANGUAGES CXX)

# PROJECT_IS_TOP_LEVEL is false when another project adds ZECS with
# add_subdirectory(). Then the tests and the install rules are off by default.
option(ZECS_BUILD_TESTS "Build the tests of ZECS" ${PROJECT_IS_TOP_LEVEL})
option(ZECS_INSTALL "Make the install rules of ZECS" ${PROJECT_IS_TOP_LEVEL})
option(ZECS_EXPERIMENTAL_EVENTS "Turn on the events API. It is a draft: see roadmap.md" OFF)

set(SRC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/src")
set(EXT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/ext")
set(TST_DIR "${CMAKE_CURRENT_SOURCE_DIR}/tests")

file(GLOB_RECURSE LIB_SOURCES CONFIGURE_DEPENDS "${SRC_DIR}/*.cpp" "${SRC_DIR}/*.h")
file(GLOB_RECURSE SIGNAL_SOURCES CONFIGURE_DEPENDS "${EXT_DIR}/signal/*.h")
file(GLOB_RECURSE TST_SOURCES CONFIGURE_DEPENDS "${TST_DIR}/*.cpp" "${TST_DIR}/*.h")

source_group("src" FILES ${LIB_SOURCES})
source_group("ext" FILES ${SIGNAL_SOURCES})
source_group("tests" FILES ${TST_SOURCES})

# ------------------------------------------
# The library
# ------------------------------------------
# A static library by default. Set BUILD_SHARED_LIBS to ON for a shared one.
add_library(zecs ${LIB_SOURCES} ${SIGNAL_SOURCES})

target_compile_features(zecs PUBLIC cxx_std_20)
set_target_properties(zecs PROPERTIES
    CXX_EXTENSIONS OFF
    VERSION ${PROJECT_VERSION}
    SOVERSION ${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}
    WINDOWS_EXPORT_ALL_SYMBOLS ON)

target_include_directories(zecs PUBLIC
    $<BUILD_INTERFACE:${SRC_DIR}>
    $<INSTALL_INTERFACE:include>)

# The headers of ZECS include <signal/Signal.h>, so the folder with it is
# public too. SYSTEM: the compiler shows no warnings for this code.
target_include_directories(zecs SYSTEM PUBLIC
    $<BUILD_INTERFACE:${EXT_DIR}>
    $<INSTALL_INTERFACE:include/zecs/ext>)

if(ZECS_EXPERIMENTAL_EVENTS)
    target_compile_definitions(zecs PUBLIC ZECS_EXPERIMENTAL_EVENTS)
endif()

# Warnings are on only when ZECS is built by itself: a project that
# uses ZECS doesn't need to see them.
if(PROJECT_IS_TOP_LEVEL)
    if(MSVC)
        set(ZECS_WARNING_FLAGS /W4)
    else()
        set(ZECS_WARNING_FLAGS -Wall -Wextra)
    endif()
    target_compile_options(zecs PRIVATE ${ZECS_WARNING_FLAGS})
endif()

# ------------------------------------------
# Tests
# ------------------------------------------
if(ZECS_BUILD_TESTS)
    enable_testing()

    add_executable(zecs_tests ${TST_SOURCES})
    target_link_libraries(zecs_tests PRIVATE zecs)
    target_include_directories(zecs_tests PRIVATE ${TST_DIR} ${EXT_DIR})
    # The tests always check the events API.
    target_compile_definitions(zecs_tests PRIVATE ZECS_EXPERIMENTAL_EVENTS)

    if(MSVC)
        # The tests have many templates: one object file needs more sections.
        target_compile_options(zecs_tests PRIVATE /bigobj)
    endif()

    if(WIN32 AND BUILD_SHARED_LIBS)
        # Windows looks for the DLL next to the program.
        add_custom_command(TARGET zecs_tests POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:zecs> $<TARGET_FILE_DIR:zecs_tests>)
    endif()

    add_test(NAME zecs_tests COMMAND zecs_tests)
endif()

# ------------------------------------------
# Install: after it another project can use find_package(zecs)
# and link to zecs.
# ------------------------------------------
if(ZECS_INSTALL)
    include(GNUInstallDirs)
    include(CMakePackageConfigHelpers)

    install(TARGETS zecs EXPORT zecsTargets
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

    install(DIRECTORY "${SRC_DIR}/zecs"
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
        FILES_MATCHING PATTERN "*.h")

    install(DIRECTORY "${EXT_DIR}/signal"
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/zecs/ext
        FILES_MATCHING PATTERN "*.h" PATTERN "LICENSE*")

    install(EXPORT zecsTargets
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/zecs)

    configure_package_config_file(
        "${CMAKE_CURRENT_SOURCE_DIR}/cmake/zecsConfig.cmake.in"
        "${CMAKE_CURRENT_BINARY_DIR}/zecsConfig.cmake"
        INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/zecs)

    # Before version 1.0 a new minor version can change the API.
    write_basic_package_version_file(
        "${CMAKE_CURRENT_BINARY_DIR}/zecsConfigVersion.cmake"
        VERSION ${PROJECT_VERSION}
        COMPATIBILITY SameMinorVersion)

    install(FILES
        "${CMAKE_CURRENT_BINARY_DIR}/zecsConfig.cmake"
        "${CMAKE_CURRENT_BINARY_DIR}/zecsConfigVersion.cmake"
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/zecs)
endif()
