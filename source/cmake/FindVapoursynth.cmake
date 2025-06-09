include(CheckIncludeFile)
set(PACKAGE_CONFIG_TEXT "Vapoursynth include directory")

if(WIN32)
    get_filename_component(VS_FOLDER "[HKEY_LOCAL_MACHINE\\SOFTWARE\\VapourSynth;Path]" ABSOLUTE)
    if (NOT VS_FOLDER MATCHES "/registry") # registry stuff in CMake is pretty obscure
        check_include_file("${VS_FOLDER}/sdk/include/vapoursynth/VapourSynth4.h" HAVE_API4_INSTALLATION)
        if (HAVE_API4_INSTALLATION)
            set(VPY_INCLUDE_DIR "${VS_FOLDER}/sdk/include" CACHE PATH "${PACKAGE_CONFIG_TEXT}")
        endif()
    endif()
else()
    find_path(VPY_INCLUDE_PREFIX NAMES vapoursynth 
        PATHS 
            /opt/homebrew              # Homebrew on Apple Silicon
            /usr/local                 # Homebrew on Intel Mac or manual install
            /usr                       # System install
            /opt/local                 # MacPorts
        PATH_SUFFIXES 
            include 
            include/vapoursynth
        DOC "VapourSynth include directory"
    )
    
    if(VPY_INCLUDE_PREFIX)
        if(EXISTS "${VPY_INCLUDE_PREFIX}/vapoursynth/VapourSynth4.h")
            set(CMAKE_REQUIRED_INCLUDES "${VPY_INCLUDE_PREFIX}")
            check_include_file("vapoursynth/VapourSynth4.h" HAVE_API4_INSTALLATION)
            if (HAVE_API4_INSTALLATION)
                set(VPY_INCLUDE_DIR "${VPY_INCLUDE_PREFIX}" CACHE PATH "${PACKAGE_CONFIG_TEXT}")
            endif()
        elseif(EXISTS "${VPY_INCLUDE_PREFIX}/VapourSynth4.h")
            get_filename_component(VPY_PARENT_DIR "${VPY_INCLUDE_PREFIX}" DIRECTORY)
            set(CMAKE_REQUIRED_INCLUDES "${VPY_PARENT_DIR}")
            check_include_file("vapoursynth/VapourSynth4.h" HAVE_API4_INSTALLATION)
            if (HAVE_API4_INSTALLATION)
                set(VPY_INCLUDE_DIR "${VPY_PARENT_DIR}" CACHE PATH "${PACKAGE_CONFIG_TEXT}")
            endif()
        endif()
    endif()
endif()

if(VPY_INCLUDE_DIR)
    set(Vapoursynth_FOUND 1)
    message(STATUS "${PACKAGE_CONFIG_TEXT}: ${VPY_INCLUDE_DIR}")
    
    get_filename_component(VPY_LIB_PREFIX "${VPY_INCLUDE_DIR}" DIRECTORY)
    find_library(VPY_SCRIPT_LIBRARY 
        NAMES vapoursynth-script libvapoursynth-script
        PATHS "${VPY_LIB_PREFIX}/lib" "${VPY_LIB_PREFIX}"
        NO_DEFAULT_PATH
    )
    
    if(VPY_SCRIPT_LIBRARY)
        message(STATUS "VapourSynth script library: ${VPY_SCRIPT_LIBRARY}")
        set(VPY_SCRIPT_LIB_PATH "${VPY_SCRIPT_LIBRARY}" CACHE PATH "VapourSynth script library path")
    else()
        message(WARNING "VapourSynth script library not found")
    endif()
else()
    set(Vapoursynth_FOUND 0)
    set(VPY_INCLUDE_DIR "VPY_INCLUDE_DIR-NOTFOUND" CACHE PATH "${PACKAGE_CONFIG_TEXT}")
    message(STATUS "${PACKAGE_CONFIG_TEXT} NOT found")
endif()
