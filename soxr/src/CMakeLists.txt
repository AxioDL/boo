# SoX Resampler Library       Copyright (c) 2007-13 robs@users.sourceforge.net
# Licence for this file: LGPL v2.1                  See LICENCE for details.



# Can generate vr-coefs.h but it complicates cross-compiling & non-cmake builds

if (NOT EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/vr-coefs.h)
  include_directories(${CMAKE_CURRENT_BINARY_DIR})
  set_property(SOURCE vr32.c APPEND PROPERTY OBJECT_DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/vr-coefs.h)
  add_executable (vr-coefs vr-coefs.c)
  ADD_CUSTOM_COMMAND(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/vr-coefs.h
    COMMAND vr-coefs > ${CMAKE_CURRENT_BINARY_DIR}/vr-coefs.h
    DEPENDS vr-coefs)
endif ()


# Minimalist boo configuration:
add_definitions (${PROJECT_C_FLAGS} -DSOXR_LIB -DSOXR_SILENT=1)
include (CheckFunctionExists)
include (CheckIncludeFiles)

set(WITH_LSR_BINDINGS OFF)
set(WITH_SINGLE_PRECISION ON)
set(WITH_DOUBLE_PRECISION OFF)
set(WITH_SIMD ON)
set(HAVE_SINGLE_PRECISION "1")
set(HAVE_DOUBLE_PRECISION "0")
set(HAVE_AVFFT "0")
set(HAVE_SIMD "1")
check_function_exists (lrint HAVE_LRINT)
if(NOT HAVE_LRINT)
  set(HAVE_LRINT "0")
endif()
check_include_files (fenv.h HAVE_FENV_H)
if(NOT HAVE_FENV_H)
  set(HAVE_FENV_H "0")
endif()
set(WORDS_BIGENDIAN "0")

configure_file (
        ${CMAKE_CURRENT_SOURCE_DIR}/../soxr-config.h.in
        ${CMAKE_CURRENT_BINARY_DIR}/soxr-config.h)
include_directories (${CMAKE_CURRENT_BINARY_DIR})

# Libsoxr configuration:

set (RDFT32 fft4g32)
if (WITH_AVFFT AND AVCODEC_FOUND)
  set (RDFT32 avfft32)
  set (RDFT32S avfft32s)
elseif (WITH_PFFFT)
  #set (RDFT32 pffft32)
  set (RDFT32S pffft32s)
elseif (WITH_SIMD)
  set (RDFT32S fft4g32s)
endif ()

if (WITH_DOUBLE_PRECISION)
  set (DP_SOURCES rate64)
endif ()

if (WITH_SINGLE_PRECISION)
  set (SP_SOURCES rate32 ${RDFT32})
endif ()

if (HAVE_SIMD)
  set (SIMD_SOURCES rate32s vr32s ${RDFT32S} simd)
  foreach (source ${SIMD_SOURCES})
    set_property (SOURCE ${source} PROPERTY COMPILE_FLAGS ${SIMD_C_FLAGS})
  endforeach ()
else ()
  set (SIMD_SOURCES vr32)
endif ()



# Libsoxr:

add_library (soxr ${LIB_TYPE} soxr.c data-io dbesi0 filter fft4g64
  ${SP_SOURCES} ${DP_SOURCES} ${SIMD_SOURCES})
set_target_properties (soxr PROPERTIES
  VERSION "${SO_VERSION}"
  SOVERSION ${SO_VERSION_MAJOR}
  INSTALL_NAME_DIR ${LIB_INSTALL_DIR}
  LINK_INTERFACE_LIBRARIES ""
  PUBLIC_HEADER "soxr.h")
if (BUILD_FRAMEWORK)
  set_target_properties (soxr PROPERTIES FRAMEWORK TRUE)
elseif (NOT WIN32)
#  set (TARGET_PCS ${CMAKE_CURRENT_BINARY_DIR}/soxr.pc)
#  configure_file (${CMAKE_CURRENT_SOURCE_DIR}/soxr.pc.in ${TARGET_PCS})
#  install (FILES ${CMAKE_CURRENT_BINARY_DIR}/soxr.pc DESTINATION ${LIB_INSTALL_DIR}/pkgconfig)
endif ()



# LSR bindings:

if (WITH_LSR_BINDINGS)
  set (LSR soxr-lsr)
  set (LSR_SO_VERSION 0.1.9)
  set (LSR_SO_VERSION_MAJOR 0)
  add_library (${LSR} ${LIB_TYPE} lsr)
  target_link_libraries (${LSR} soxr)
  set_target_properties (${LSR} PROPERTIES
    VERSION "${LSR_SO_VERSION}"
    SOVERSION ${LSR_SO_VERSION_MAJOR}
    INSTALL_NAME_DIR ${LIB_INSTALL_DIR}
    LINK_INTERFACE_LIBRARIES ""
    PUBLIC_HEADER "${LSR}.h")
  if (BUILD_FRAMEWORK)
    set_target_properties (${LSR} PROPERTIES FRAMEWORK TRUE)
  elseif (NOT WIN32)
#    set (TARGET_PCS "${TARGET_PCS} ${CMAKE_CURRENT_BINARY_DIR}/${LSR}.pc")
#    configure_file (${CMAKE_CURRENT_SOURCE_DIR}/${LSR}.pc.in ${CMAKE_CURRENT_BINARY_DIR}/${LSR}.pc)
#    install (FILES ${CMAKE_CURRENT_BINARY_DIR}/${LSR}.pc DESTINATION ${LIB_INSTALL_DIR}/pkgconfig)
  endif ()
endif ()



# Installation (from build from source):

#install (TARGETS soxr ${LSR}
#  FRAMEWORK DESTINATION ${FRAMEWORK_INSTALL_DIR}
#  LIBRARY DESTINATION ${LIB_INSTALL_DIR}
#  RUNTIME DESTINATION ${BIN_INSTALL_DIR}
#  ARCHIVE DESTINATION ${LIB_INSTALL_DIR}
#  PUBLIC_HEADER DESTINATION ${INCLUDE_INSTALL_DIR})



# Packaging (for unix-like distributions):

#get_property (LIB1 TARGET soxr PROPERTY LOCATION)
#if (BUILD_SHARED_LIBS)
#  set (LIB1 ${LIB1}.${SO_VERSION_MAJOR} ${LIB1}.${SO_VERSION})
#endif ()
#list (APPEND TARGET_HEADERS "${CMAKE_CURRENT_SOURCE_DIR}/soxr.h")
#if (WITH_LSR_BINDINGS)
#  get_property (LIB2 TARGET ${LSR} PROPERTY LOCATION)
#  if (BUILD_SHARED_LIBS)
#    set (LIB2 ${LIB2}.${LSR_SO_VERSION_MAJOR} ${LIB2}.${LSR_SO_VERSION})
#  endif ()
#  list (APPEND TARGET_HEADERS "${CMAKE_CURRENT_SOURCE_DIR}/${LSR}.h")
#endif ()
#set (TARGET_LIBS ${LIB1} ${LIB2})
#configure_file (${CMAKE_CURRENT_SOURCE_DIR}/libsoxr.src.in ${CMAKE_CURRENT_BINARY_DIR}/libsoxr.src)
#configure_file (${CMAKE_CURRENT_SOURCE_DIR}/libsoxr-dev.src.in ${CMAKE_CURRENT_BINARY_DIR}/libsoxr-dev.src)
