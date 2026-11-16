set(METAHOOK_SOURCE_PATH "$ENV{METAHOOK_SOURCE_PATH}" CACHE PATH "MetaHook source tree; empty fetches SDK main")
set(VGUI2EXTENSION_SOURCE_PATH "$ENV{VGUI2EXTENSION_SOURCE_PATH}" CACHE PATH "VGUI2Extension source tree providing its public interface headers; empty fetches the pinned commit")
set(VOICEEXTENSION_SOURCE_PATH "$ENV{VOICEEXTENSION_SOURCE_PATH}" CACHE PATH "VoiceExtension source tree providing IVoiceExtension.h; empty uses the vendored interface copy")

function(soundpad_fetch_source name url tag out_var)
    include(FetchContent)
    FetchContent_Populate(${name}
        GIT_REPOSITORY "${url}"
        GIT_TAG "${tag}"
        GIT_SUBMODULES ""
        GIT_SUBMODULES_RECURSE FALSE
        SOURCE_DIR "${CMAKE_BINARY_DIR}/_deps/${name}-src")
    string(TOLOWER "${name}" name_lower)
    set(${out_var} "${${name_lower}_SOURCE_DIR}" PARENT_SCOPE)
endfunction()

function(soundpad_validate_metahook_source source)
    foreach(required include/metahook.h include/HLSDK/common/interface.cpp
        include/Interface/IPlugins.h include/SourceSDK/tier1/KeyValues.cpp
        include/vgui_controls/Panel.cpp)
        if(NOT EXISTS "${source}/${required}" OR IS_DIRECTORY "${source}/${required}")
            message(FATAL_ERROR "METAHOOK_SOURCE_PATH is missing ${required}: ${source}")
        endif()
    endforeach()
endfunction()

function(soundpad_validate_vgui2extension_source source)
    foreach(required include/Interface/IVGUI2Extension.h include/Interface/IDpiManager.h
        include/Interface/VGUI/IInput2.h include/Interface/VGUI/IScheme2.h include/Interface/VGUI/ISurface2.h)
        if(NOT EXISTS "${source}/${required}" OR IS_DIRECTORY "${source}/${required}")
            message(FATAL_ERROR "VGUI2EXTENSION_SOURCE_PATH is missing ${required}: ${source}")
        endif()
    endforeach()
endfunction()

function(soundpad_validate_voiceextension_source source)
    if(NOT EXISTS "${source}/include/Interface/IVoiceExtension.h"
        OR IS_DIRECTORY "${source}/include/Interface/IVoiceExtension.h")
        message(FATAL_ERROR "VOICEEXTENSION_SOURCE_PATH is missing include/Interface/IVoiceExtension.h: ${source}")
    endif()
endfunction()

function(soundpad_prepare_dependencies)
    # Validate explicit paths before downloading anything; external trees are
    # read-only inputs and must never be modified by this build.
    if(METAHOOK_SOURCE_PATH)
        get_filename_component(metahook_source "${METAHOOK_SOURCE_PATH}" ABSOLUTE BASE_DIR "${PROJECT_SOURCE_DIR}")
        soundpad_validate_metahook_source("${metahook_source}")
    endif()
    if(VGUI2EXTENSION_SOURCE_PATH)
        get_filename_component(vgui2extension_source "${VGUI2EXTENSION_SOURCE_PATH}" ABSOLUTE BASE_DIR "${PROJECT_SOURCE_DIR}")
        soundpad_validate_vgui2extension_source("${vgui2extension_source}")
    endif()
    if(VOICEEXTENSION_SOURCE_PATH)
        get_filename_component(voiceextension_source "${VOICEEXTENSION_SOURCE_PATH}" ABSOLUTE BASE_DIR "${PROJECT_SOURCE_DIR}")
        soundpad_validate_voiceextension_source("${voiceextension_source}")
    endif()

    if(METAHOOK_SOURCE_PATH)
        get_filename_component(metahook_source "${METAHOOK_SOURCE_PATH}" ABSOLUTE BASE_DIR "${PROJECT_SOURCE_DIR}")
    else()
        include(FetchContent)
        FetchContent_Declare(soundpad_metahook
            GIT_REPOSITORY https://github.com/MetaHookSv/MetaHook
            # MetaHook is tracked as a branch: always fetch the latest main.
            GIT_TAG origin/main
            GIT_SUBMODULES ""
            GIT_SUBMODULES_RECURSE FALSE
            # This SDK directory has no CMakeLists.txt: populate without building the launcher.
            SOURCE_SUBDIR include
        )
        FetchContent_MakeAvailable(soundpad_metahook)
        set(metahook_source "${soundpad_metahook_SOURCE_DIR}")
    endif()
    soundpad_validate_metahook_source("${metahook_source}")
    set(METAHOOK_SOURCE_PATH "${metahook_source}" PARENT_SCOPE)
    message(STATUS "METAHOOK_SOURCE_PATH: ${metahook_source}")

    if(NOT VGUI2EXTENSION_SOURCE_PATH)
        soundpad_fetch_source(soundpad_vgui2extension
            "https://github.com/MetaHookSv/VGUI2Extension"
            "b60ec0f03c60cb7c8c6d3e59c3c38f4753cf1d0d" vgui2extension_source)
        soundpad_validate_vgui2extension_source("${vgui2extension_source}")
    endif()
    set(VGUI2EXTENSION_SOURCE_PATH "${vgui2extension_source}" PARENT_SCOPE)
    message(STATUS "VGUI2EXTENSION_SOURCE_PATH: ${vgui2extension_source}")

    if(VOICEEXTENSION_SOURCE_PATH)
        set(VOICEEXTENSION_SOURCE_PATH "${voiceextension_source}" PARENT_SCOPE)
        message(STATUS "VOICEEXTENSION_SOURCE_PATH: ${voiceextension_source}")
    else()
        message(STATUS "VOICEEXTENSION_SOURCE_PATH: (not set; using Plugins/Soundpad/include/Interface/IVoiceExtension.h)")
    endif()
endfunction()
