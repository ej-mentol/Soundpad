function(soundpad_use_sdk target)
    target_compile_features(${target} PRIVATE cxx_std_20)
    target_compile_definitions(${target} PRIVATE
        WIN32
        _WINDOWS
        _USRDLL
        _MBCS
        _CRT_SECURE_NO_WARNINGS
        NO_MALLOC_OVERRIDE
        NO_TIER0_MINIDUMP
        NO_VCR
        STEAM_API_NODLL
        VGUI_USE_SURFACE2
        VGUI_USE_SCHEME2
        VGUI_USE_INPUT2
    )

    set(_soundpad_vgui2_include "")
    if(VGUI2EXTENSION_SOURCE_PATH)
        list(APPEND _soundpad_vgui2_include
            "${VGUI2EXTENSION_SOURCE_PATH}/include/Interface"
            "${VGUI2EXTENSION_SOURCE_PATH}/include/Interface/VGUI")
    endif()
    set(_soundpad_voice_include "")
    if(VOICEEXTENSION_SOURCE_PATH)
        # IVoiceExtension.h is included as "Interface/IVoiceExtension.h", so the
        # parent include directory must come before the vendored copy.
        list(APPEND _soundpad_voice_include
            "${VOICEEXTENSION_SOURCE_PATH}/include"
            "${VOICEEXTENSION_SOURCE_PATH}/include/Interface")
    endif()

    target_include_directories(${target} PRIVATE
        ${_soundpad_vgui2_include}
        ${_soundpad_voice_include}
        "${PROJECT_SOURCE_DIR}/src"
        "${PROJECT_SOURCE_DIR}/include"
        "${PROJECT_SOURCE_DIR}/include/Interface"
        "${PROJECT_SOURCE_DIR}/include/Interface/VGUI"
        "${METAHOOK_SOURCE_PATH}/include"
        "${METAHOOK_SOURCE_PATH}/include/Interface"
        "${METAHOOK_SOURCE_PATH}/include/Interface/VGUI"
        "${METAHOOK_SOURCE_PATH}/include/HLSDK/cl_dll"
        "${METAHOOK_SOURCE_PATH}/include/HLSDK/common"
        "${METAHOOK_SOURCE_PATH}/include/HLSDK/engine"
        "${METAHOOK_SOURCE_PATH}/include/HLSDK/pm_shared"
        "${METAHOOK_SOURCE_PATH}/include/HLSDK/public"
        "${METAHOOK_SOURCE_PATH}/include/VGUI"
        "${METAHOOK_SOURCE_PATH}/include/vgui_controls"
        "${METAHOOK_SOURCE_PATH}/include/SourceSDK"
        "${METAHOOK_SOURCE_PATH}/include/SourceSDK/tier0"
        "${METAHOOK_SOURCE_PATH}/include/SourceSDK/tier1"
        "${METAHOOK_SOURCE_PATH}/include/SourceSDK/vstdlib"
        "${METAHOOK_SOURCE_PATH}/include/SteamSDK"
    )
endfunction()
