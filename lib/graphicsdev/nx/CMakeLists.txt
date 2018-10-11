option(ENABLE_NX "Build mesa for offline NX shader compilation and runtime" ON)
find_program(MESON_PROG meson)
find_program(NINJA_PROG ninja)
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/mesa/meson.build AND
   EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/libdrm_nouveau/Makefile AND
   MESON_PROG AND NINJA_PROG AND ENABLE_NX)
message(STATUS "Enabling NX support")

set(LIBDRM_DIR ${CMAKE_CURRENT_SOURCE_DIR}/libdrm_nouveau)
set(MESA_DIR ${CMAKE_CURRENT_SOURCE_DIR}/mesa)
set(MESA_BUILD_DIR ${CMAKE_CURRENT_BINARY_DIR}/mesa)
file(MAKE_DIRECTORY ${MESA_BUILD_DIR})

if(NOT NX)
  set(PLAT_MESA_TARGETS src/mesa/libmesa_sse41.a)
else()
  set(PLAT_MESA_TARGETS src/gallium/winsys/nouveau/switch/libnouveauwinsys.a)
endif()

set(MESA_TARGETS
    src/compiler/libcompiler.a
    src/compiler/glsl/libglsl.a
    src/compiler/glsl/glcpp/libglcpp.a
    src/compiler/nir/libnir.a
    src/libglsl_util.a
    src/util/libmesa_util.a
    src/mesa/libmesa_gallium.a
    ${PLAT_MESA_TARGETS}
    src/gallium/auxiliary/libgallium.a
    src/gallium/auxiliary/libgalliumvl.a
    src/gallium/drivers/nouveau/libnouveau.a)

include_directories(
    ${MESA_DIR}/include
    ${MESA_DIR}/src
    ${MESA_DIR}/src/mesa
    ${MESA_DIR}/src/mapi
    ${MESA_DIR}/src/compiler/glsl
    ${MESA_BUILD_DIR}/src/compiler
    ${MESA_DIR}/src/mesa/state_tracker
    ${MESA_DIR}/src/gallium/include
    ${MESA_DIR}/src/gallium/auxiliary
    ${MESA_DIR}/src/gallium/drivers/nouveau
    ${LIBDRM_DIR}/include)

if(${CMAKE_BUILD_TYPE} STREQUAL Release OR ${CMAKE_BUILD_TYPE} STREQUAL RelWithDebInfo)
  set(MESON_BUILD_TYPE release)
  set(MESON_SANITIZE_ARGS "")
else()
  set(MESON_BUILD_TYPE debug)
  set(MESON_SANITIZE_ARGS "-fsanitize=address")
endif()

if(NX)
  configure_file(switch_cross_file.txt.in switch_cross_file.txt)
  set(MESON_CROSS --cross-file ${CMAKE_CURRENT_BINARY_DIR}/switch_cross_file.txt -D c_std=gnu11 -D cpp_std=gnu++17)
  set(MESA_PLATFORMS switch)
endif()

if(NOT EXISTS ${MESA_BUILD_DIR}/build.ninja)
  message(STATUS "Preparing mesa build system")
  set(ENV{CC} "")
  set(ENV{CXX} "")
  execute_process(COMMAND ${MESON_PROG} setup -D buildtype=${MESON_BUILD_TYPE} ${MESON_CROSS}
        -D gallium-drivers=nouveau -D dri-drivers= -D vulkan-drivers= -D llvm=false
        -D shared-glapi=true -D gles1=false -D gles2=false -D gbm=false
        -D shader-cache=false -D boo-offline-mode=true -D "platforms=${MESA_PLATFORMS}" -D glx=disabled
        -D "c_args=${MESON_SANITIZE_ARGS} -I${LIBDRM_DIR}/include -DDEBUG=1 -DHAVE_LIBDRM"
        -D "cpp_args=${MESON_SANITIZE_ARGS} -I${LIBDRM_DIR}/include -DDEBUG=1 -DHAVE_LIBDRM"
        -D "c_link_args=${MESON_SANITIZE_ARGS}"
        -D "cpp_link_args=${MESON_SANITIZE_ARGS}"
        ${MESA_DIR} ${MESA_BUILD_DIR}
        RESULT_VARIABLE MESON_RESULT)
  if(NOT MESON_RESULT EQUAL 0)
    message(FATAL_ERROR "meson failed with error code ${MESON_RESULT}")
  endif()
endif()
message(STATUS "Invoking mesa build system")
execute_process(COMMAND ${NINJA_PROG} -C ${MESA_BUILD_DIR} ${MESA_TARGETS} RESULT_VARIABLE NINJA_RESULT)
if(NOT NINJA_RESULT EQUAL 0)
  message(FATAL_ERROR "ninja failed with error code ${NINJA_RESULT}")
endif()

if(NOT WIN32)
  add_definitions("-DHAVE_PTHREAD -DHAVE_TIMESPEC_GET")
  if(${CMAKE_SYSTEM_NAME} STREQUAL Linux)
    add_definitions("-DHAVE_LINUX_FUTEX_H")
  endif()
endif()
add_definitions("-DHAVE_ZLIB -DDEBUG=1 -DHAVE_LIBDRM")

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-error=return-type")

add_subdirectory(fake_libdrm_nouveau)

add_library(nx_compiler nx_compiler.cpp
            ${MESA_DIR}/src/compiler/glsl/ir_builder_print_visitor.cpp)
target_link_libraries(nx_compiler
        ${MESA_BUILD_DIR}/src/mesa/libmesa_gallium.a
        ${MESA_BUILD_DIR}/src/mesa/libmesa_sse41.a
        ${MESA_BUILD_DIR}/src/compiler/nir/libnir.a
        ${MESA_BUILD_DIR}/src/compiler/glsl/libglsl.a
        ${MESA_BUILD_DIR}/src/libglsl_util.a
        ${MESA_BUILD_DIR}/src/compiler/glsl/glcpp/libglcpp.a
        ${MESA_BUILD_DIR}/src/compiler/libcompiler.a
        ${MESA_BUILD_DIR}/src/gallium/drivers/nouveau/libnouveau.a
        ${MESA_BUILD_DIR}/src/gallium/auxiliary/libgallium.a
        ${MESA_BUILD_DIR}/src/gallium/auxiliary/libgalliumvl.a
        ${MESA_BUILD_DIR}/src/util/libmesa_util.a
        fake_libdrm_nouveau
        unwind dl pthread z)
add_executable(nx_compiler_driver nx_compiler_driver.cpp)
target_link_libraries(nx_compiler_driver nx_compiler)

if(COMMAND add_sanitizers)
  add_sanitizers(nx_compiler nx_compiler_driver)
endif()

if(NX)
  include_directories(${DEVKITPRO}/libnx/include)
  add_library(libdrm_nouveau
          libdrm_nouveau/source/bomap.c
          libdrm_nouveau/source/bufctx.c
          libdrm_nouveau/source/nouveau.c
          libdrm_nouveau/source/pushbuf.c)
  add_definitions(-DBOO_HAS_NX=1)

  add_library(nx_runtime NX.cpp nx_compiler.cpp
              ${MESA_DIR}/src/compiler/glsl/ir_builder_print_visitor.cpp)
  target_link_libraries(nx_runtime xxhash
          ${MESA_BUILD_DIR}/src/mesa/libmesa_gallium.a
          ${MESA_BUILD_DIR}/src/compiler/nir/libnir.a
          ${MESA_BUILD_DIR}/src/compiler/glsl/libglsl.a
          ${MESA_BUILD_DIR}/src/libglsl_util.a
          ${MESA_BUILD_DIR}/src/compiler/glsl/glcpp/libglcpp.a
          ${MESA_BUILD_DIR}/src/compiler/libcompiler.a
          ${MESA_BUILD_DIR}/src/gallium/drivers/nouveau/libnouveau.a
          ${MESA_BUILD_DIR}/src/gallium/winsys/nouveau/switch/libnouveauwinsys.a
          ${MESA_BUILD_DIR}/src/gallium/auxiliary/libgallium.a
          ${MESA_BUILD_DIR}/src/gallium/auxiliary/libgalliumvl.a
          ${MESA_BUILD_DIR}/src/util/libmesa_util.a
          libdrm_nouveau nx)
  if(COMMAND add_sanitizers)
    add_sanitizers(nx_runtime libdrm_nouveau)
  endif()
endif()

else()
  if(NX)
    message(FATAL_ERROR "Unable to find meson or ninja or mesa submodules; this is required for NX.")
  elseif(ENABLE_NX)
    message(STATUS "Unable to find meson or ninja or mesa submodules; skipping NX support.")
  endif()
endif()
