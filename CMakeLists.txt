cmake_minimum_required(VERSION 3.2...3.12 FATAL_ERROR)

project ("commands"
    LANGUAGES
        C
)

# If you install ids peak via the tar archive, you'll need to point to it here
set(CMAKE_PREFIX_PATH "/home/starcam/ids-peak-with-ueyetl_2.16.0.0-457_amd64/lib/")

# Create target executable
add_executable (${PROJECT_NAME}
    commands.c commands.h
    camera.c camera.h
    lens_adapter.c lens_adapter.h
    astrometry.c astrometry.h
    matrix.c matrix.h
    sc_send.c sc_send.h
    sc_listen.c sc_listen.h
    sc_data_structures.h
)

# Command compiler to use C99 standard (required by ids_peak_comfort_c)
set_target_properties(${PROJECT_NAME}
    PROPERTIES
        C_STANDARD 99
        C_STANDARD_REQUIRED ON
)

find_package(ids_peak_comfort_c REQUIRED)
find_package(ids_peak REQUIRED)
find_package(ids_peak_afl REQUIRED)
find_library(PEAK_IFL_PATH ids_peak_ifl ${CMAKE_PREFIX_PATH}) # not a cmake package but a bare .so
find_package(ids_peak_ipl REQUIRED)

# Setup include directories
target_include_directories (${PROJECT_NAME}
    PRIVATE
        ${CMAKE_SOURCE_DIR}/include
        /usr/local/astrometry/include
        /usr/local/sofa/include
)

# import required, prebuilt local libraries
# if you installed astrometry.net or SOFA in different places, you'll need to
# update them here
add_library( astrometry SHARED IMPORTED )
set_target_properties( astrometry PROPERTIES IMPORTED_LOCATION /usr/local/astrometry/lib/libastrometry.so )
add_library( sofa SHARED IMPORTED )
set_target_properties( sofa PROPERTIES IMPORTED_LOCATION /usr/local/sofa/lib/libsofa_c.a )

# Link libraries
target_link_libraries (${PROJECT_NAME}
    pthread
    sofa
    astrometry
    # ueye_api
    ids_peak_comfort_c::ids_peak_comfort_c
    ids_peak
    ids_peak_afl
    ${PEAK_IFL_PATH}
    ids_peak_ipl
    m
)

# required to conditionally compile starcam code until we remove dependency on
# ueye old api
add_compile_definitions(IDS_PEAK)

# help find our shared libraries at runtime, after make install
set(CMAKE_INSTALL_RPATH_USE_LINK_PATH TRUE)

# Add postbuild step to copy ids_peak_comfort_c dependencies
ids_peak_comfort_c_deploy(${PROJECT_NAME})

# For unix Build we need the environment variable GENICAM_GENTL32_PATH respectively GENICAM_GENTL64_PATH to find the GenTL producer libraries.
# To set these environment variables a shell script is used. This script can be automatically generated via ids_peak_comfort_c_generate_starter_script.
# The shell script will be saved at ${CMAKE_CURRENT_BINARY_DIR}/${targetName}.sh and automatically copied to the output directory during post-build.

# To run the samples run this script, not the binary.
if(UNIX)
    ids_peak_comfort_c_generate_starter_script(${PROJECT_NAME})
endif()