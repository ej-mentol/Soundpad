cmake_minimum_required(VERSION 3.21)
option(FORMAT_VALIDATION_ONLY "Configure only clang-format targets without native dependencies" OFF)
if(FORMAT_VALIDATION_ONLY)
    project(FormatValidation LANGUAGES NONE)
    include(cmake/FormatValidation.cmake)
    return()
endif()
cmake_policy(SET CMP0091 NEW)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
project(Soundpad LANGUAGES C CXX)

if(NOT WIN32 OR NOT MSVC OR NOT CMAKE_SIZEOF_VOID_P EQUAL 4)
    message(FATAL_ERROR "Soundpad requires Windows MSVC x86. Configure with -A Win32.")
endif()
get_property(soundpad_multi_config GLOBAL PROPERTY GENERATOR_IS_MULTI_CONFIG)
if(soundpad_multi_config)
    set(CMAKE_CONFIGURATION_TYPES "Debug;Release" CACHE STRING "Supported configurations" FORCE)
elseif(NOT CMAKE_BUILD_TYPE MATCHES "^(Debug|Release)$")
    message(FATAL_ERROR "Select Debug or Release.")
endif()

include(cmake/Dependencies.cmake)
soundpad_prepare_dependencies()
include(cmake/SDK.cmake)
include(cmake/Sources.cmake)
include(cmake/SourceGroups.cmake)

add_library(Soundpad SHARED ${SOUNDPAD_SOURCES})
soundpad_assign_source_groups(Soundpad)
soundpad_use_sdk(Soundpad)

target_compile_definitions(Soundpad PRIVATE
    $<$<CONFIG:Debug>:_DEBUG>
    $<$<CONFIG:Release>:NDEBUG>
)
target_compile_options(Soundpad PRIVATE
    /permissive
    /MP
    /Zi
    /W3
    /wd4291
    /wd4311
    /wd4312
    /wd4819
    /wd4996
    $<$<CONFIG:Release>:/wd4091>
)
target_link_libraries(Soundpad PRIVATE winmm ole32 oleaut32)
target_link_options(Soundpad PRIVATE
    /DEBUG
    /SUBSYSTEM:WINDOWS
    $<$<CONFIG:Debug>:/INCREMENTAL>
    $<$<CONFIG:Release>:/INCREMENTAL:NO>
    $<$<CONFIG:Release>:/OPT:REF>
    $<$<CONFIG:Release>:/OPT:ICF>
)
set_target_properties(Soundpad PROPERTIES INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE)

install(TARGETS Soundpad RUNTIME DESTINATION svencoop/metahook/plugins)
install(FILES "$<TARGET_PDB_FILE:Soundpad>" DESTINATION svencoop/metahook/plugins)

if(CMAKE_SOURCE_DIR STREQUAL PROJECT_SOURCE_DIR)
    option(METAHOOKSV_ENABLE_LAUNCH_GAME "Add the Visual Studio build/deploy/debug target" OFF)
    if(METAHOOKSV_ENABLE_LAUNCH_GAME)
        include(cmake/LaunchGame.cmake)
        metahooksv_add_launch_game()
    endif()
endif()

# Shared formatting targets are explicit; ordinary native builds do not run them.
include(cmake/FormatValidation.cmake)
