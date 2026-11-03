cmake_minimum_required(VERSION 3.10)

# Compiler: clang >= 10 is required (BPF CO-RE needs __builtin_preserve_access_index).
# The system clang 3.4 on CentOS 7 has no BPF target. Candidates are tried in
# order; the first one whose version is >= 10 is used. Pass
# -DCMAKE_C_COMPILER=... to bypass the search.
if(NOT DEFINED CMAKE_C_COMPILER)
  set(BPF_CLANG_FOUND "")
  foreach(_cand_name IN ITEMS
          "/opt/llvm-10/bin/clang" clang-10 clang-11 clang-12 clang-13
          clang-14 clang-15 clang-16 clang-17 clang-18 clang)
    unset(_cand CACHE)
    find_program(_cand ${_cand_name})
    if(_cand)
      execute_process(COMMAND ${_cand} --version
                      OUTPUT_VARIABLE _clang_version_out
                      ERROR_QUIET
                      RESULT_VARIABLE _clang_rc)
      if(_clang_rc EQUAL 0 AND _clang_version_out MATCHES "version ([0-9]+)")
        if(CMAKE_MATCH_1 GREATER_EQUAL 10)
          set(BPF_CLANG_FOUND "${_cand}")
          break()
        endif()
      endif()
    endif()
  endforeach()
  if(NOT BPF_CLANG_FOUND)
    message(FATAL_ERROR
      "clang >= 10 not found. Install it (Ubuntu: sudo apt-get install clang-10; "
      "CentOS: unpack the official LLVM 10 build into /opt/llvm-10), "
      "or pass -DCMAKE_C_COMPILER=<path to clang>.")
  endif()
  set(CMAKE_C_COMPILER "${BPF_CLANG_FOUND}")
endif()

project(execsnoop C)

# BPF needs Linux. Other systems are rejected up front.
if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
  message(FATAL_ERROR "execsnoop uses BPF and only builds on Linux (got ${CMAKE_SYSTEM_NAME})")
endif()

# Writes build/compile_commands.json with the real flags of every target,
# including the BPF object.
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ------------------------------------------------- distribution detection
# Pick install directories and the package names for missing dependencies.
set(OS_ID "unknown")
set(OS_ID_LIKE "")
if(EXISTS "/etc/os-release")
  file(STRINGS "/etc/os-release" _os_release_lines)
  foreach(_line IN LISTS _os_release_lines)
    if(_line MATCHES "^ID=(.*)$")
      string(REPLACE "\"" "" OS_ID "${CMAKE_MATCH_1}")
    elseif(_line MATCHES "^ID_LIKE=(.*)$")
      string(REPLACE "\"" "" OS_ID_LIKE "${CMAKE_MATCH_1}")
    endif()
  endforeach()
endif()

set(OS_FAMILY "other")
set(DEP_INSTALL_CMD "")
set(DEP_PACKAGES "")
if(OS_ID MATCHES "^(centos|rhel|fedora|rocky|almalinux|ol)$" OR OS_ID_LIKE MATCHES "(rhel|fedora)")
  set(OS_FAMILY "rhel")
  set(DEP_INSTALL_CMD "yum install")
  set(DEP_PACKAGES "elfutils-libelf-devel zlib-devel")
elseif(OS_ID MATCHES "^(debian|ubuntu|linuxmint)$" OR OS_ID_LIKE MATCHES "(debian|ubuntu)")
  set(OS_FAMILY "debian")
  set(DEP_INSTALL_CMD "apt-get install")
  set(DEP_PACKAGES "libelf-dev zlib1g-dev")
endif()

# RHEL-family 64-bit systems keep libraries in lib64; Debian-family use lib.
if(NOT DEFINED CMAKE_INSTALL_LIBDIR)
  if(OS_FAMILY STREQUAL "rhel" AND CMAKE_SIZEOF_VOID_P EQUAL 8)
    set(CMAKE_INSTALL_LIBDIR "lib64")
  else()
    set(CMAKE_INSTALL_LIBDIR "lib")
  endif()
endif()
include(GNUInstallDirs)

message(STATUS "execsnoop: OS=${OS_ID} (family ${OS_FAMILY}), install libdir=${CMAKE_INSTALL_LIBDIR}")

# ------------------------------------------------------------ dependencies
find_library(ELF_LIB NAMES elf libelf)
find_library(Z_LIB NAMES z)
find_path(GELF_INC gelf.h)
if(NOT ELF_LIB OR NOT GELF_INC OR NOT Z_LIB)
  if(DEP_INSTALL_CMD)
    set(_hint "Install them with: sudo ${DEP_INSTALL_CMD} ${DEP_PACKAGES}")
  else()
    set(_hint "Install the libelf and zlib development packages for your system.")
  endif()
  message(FATAL_ERROR "libelf/zlib development files not found. ${_hint}")
endif()

set(SRC_DIR     ${CMAKE_SOURCE_DIR}/src)
set(INC_DIR     ${CMAKE_SOURCE_DIR}/include)   # vendored libbpf headers (bpf/*.h)
set(OBJ_DIR     ${CMAKE_BINARY_DIR}/obj)
set(LIB_DIR     ${CMAKE_BINARY_DIR}/lib)
set(BIN_DIR     ${CMAKE_BINARY_DIR}/bin)
set(BPFTOOL_DIR ${CMAKE_BINARY_DIR}/bpftool)

# libbpf and bpftool are git submodules (third_party/), built from source:
#   git submodule update --init --recursive
set(LIBBPF_SRC  ${CMAKE_SOURCE_DIR}/third_party/libbpf/src)
set(BPFTOOL_SRC ${CMAKE_SOURCE_DIR}/third_party/bpftool/src)
if(NOT EXISTS "${LIBBPF_SRC}/libbpf.h" OR NOT EXISTS "${BPFTOOL_SRC}/Makefile")
  message(FATAL_ERROR "third_party/libbpf or third_party/bpftool is missing. "
          "Run: git submodule update --init --recursive")
endif()

find_program(MAKE_PROG make REQUIRED)

# Architecture name used by BPF code (__TARGET_ARCH_x86 / arm64).
if(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64")
  set(BPF_ARCH x86)
elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64")
  set(BPF_ARCH arm64)
else()
  set(BPF_ARCH ${CMAKE_SYSTEM_PROCESSOR})
endif()

# ---------------------------------------------------------------- libbpf
# LIBSUBDIR=lib pins libbpf's install layout to usr/lib, so the copy below
# does not depend on the distribution's default.
add_custom_command(
  OUTPUT ${LIB_DIR}/libbpf.so.1
  COMMAND ${CMAKE_COMMAND} -E make_directory ${OBJ_DIR}/libbpf ${OBJ_DIR}/libbpf-install ${LIB_DIR}
  COMMAND ${MAKE_PROG} -C ${LIBBPF_SRC} OBJDIR=${OBJ_DIR}/libbpf
          DESTDIR=${OBJ_DIR}/libbpf-install LIBSUBDIR=lib install
  # cp -L: install a real file, not a symlink (cmake --install would copy the symlink
  # without its target).
  COMMAND sh -c "cp -L ${OBJ_DIR}/libbpf-install/usr/lib/libbpf.so.1 ${LIB_DIR}/libbpf.so.1"
  VERBATIM
  COMMENT "Building libbpf")
add_custom_target(libbpf_build DEPENDS ${LIB_DIR}/libbpf.so.1)

# Imported target for the user-space library built above.
add_library(bpf_so SHARED IMPORTED)
set_target_properties(bpf_so PROPERTIES
  IMPORTED_LOCATION ${LIB_DIR}/libbpf.so.1
  INTERFACE_INCLUDE_DIRECTORIES ${INC_DIR})

# --------------------------------------------------------------- bpftool
# bpftool's Makefile names its LLVM tools as clang$(LLVM_VERSION),
# llvm-strip$(LLVM_VERSION), ... . If the compiler is clang-NN (e.g. clang-18,
# as on Ubuntu), use the matching -NN suffix so those tools are found.
get_filename_component(_cc_name "${CMAKE_C_COMPILER}" NAME)
if(_cc_name MATCHES "^clang-([0-9]+)$")
  set(BPFTOOL_LLVM_VERSION "-${CMAKE_MATCH_1}")
else()
  set(BPFTOOL_LLVM_VERSION "")
endif()

# The CentOS 7 (3.10) kernel headers predate NFPROTO_NETDEV, which bpftool needs.
# Define it only when the headers lack it: on newer headers it is an enum value,
# and a -D macro for it breaks the enum.
include(CheckCSourceCompiles)
# Compile a use of the constant (not its address, which an enumerator lacks).
check_c_source_compiles("
#include <linux/netfilter.h>
int main(void) { int v = NFPROTO_NETDEV; return v; }"
  HAVE_NFPROTO_NETDEV)
if(HAVE_NFPROTO_NETDEV)
  set(BPFTOOL_EXTRA_CFLAGS "")
else()
  set(BPFTOOL_EXTRA_CFLAGS "-DNFPROTO_NETDEV=5")
endif()

add_custom_command(
  OUTPUT ${BPFTOOL_DIR}/bpftool
  COMMAND ${CMAKE_COMMAND} -E make_directory ${BPFTOOL_DIR}
  COMMAND ${MAKE_PROG} -C ${BPFTOOL_SRC} OUTPUT=${BPFTOOL_DIR}/
          LLVM_VERSION=${BPFTOOL_LLVM_VERSION}
          EXTRA_CFLAGS=${BPFTOOL_EXTRA_CFLAGS}
  COMMENT "Building bpftool")
add_custom_target(bpftool_build DEPENDS ${BPFTOOL_DIR}/bpftool)

# -------------------------------------------------------------- vmlinux.h
# The bundled 6.18 header. Generating it from /sys/kernel/btf/vmlinux would
# need BTF on the build machine, which this project does not assume.
add_custom_command(
  OUTPUT ${OBJ_DIR}/vmlinux.h
  COMMAND ${CMAKE_COMMAND} -E make_directory ${OBJ_DIR}
  COMMAND ${CMAKE_COMMAND} -E copy ${INC_DIR}/vmlinux_6.18.h ${OBJ_DIR}/vmlinux.h
  DEPENDS ${INC_DIR}/vmlinux_6.18.h
  COMMENT "Copying vmlinux.h")
add_custom_target(vmlinux_h DEPENDS ${OBJ_DIR}/vmlinux.h)

# ------------------------------------------------------------- BPF object
# An OBJECT library, so CMake records its compile command (and the clang
# -target bpf flags) in compile_commands.json.
add_library(execsnoop_bpf OBJECT ${SRC_DIR}/execsnoop.bpf.c)
set_target_properties(execsnoop_bpf PROPERTIES POSITION_INDEPENDENT_CODE OFF)
target_include_directories(execsnoop_bpf PRIVATE ${INC_DIR} ${OBJ_DIR} ${SRC_DIR})
target_compile_options(execsnoop_bpf PRIVATE
  -target bpf -g -O2 -Ubpf -D__TARGET_ARCH_${BPF_ARCH})
add_dependencies(execsnoop_bpf vmlinux_h)

# --------------------------------------------------------- skeleton header
add_custom_command(
  OUTPUT ${OBJ_DIR}/execsnoop.skel.h
  # name: bpftool would otherwise derive "execsnoop_bpf_c" from the object file name
  COMMAND sh -c "${BPFTOOL_DIR}/bpftool gen skeleton $<TARGET_OBJECTS:execsnoop_bpf> name execsnoop_bpf > ${OBJ_DIR}/execsnoop.skel.h"
  DEPENDS execsnoop_bpf bpftool_build
  VERBATIM
  COMMENT "Generating execsnoop.skel.h")
add_custom_target(skel_h DEPENDS ${OBJ_DIR}/execsnoop.skel.h)

# ------------------------------------------------------- user-space loader
add_executable(execsnoop ${SRC_DIR}/execsnoop.c ${OBJ_DIR}/execsnoop.skel.h)
set_source_files_properties(${OBJ_DIR}/execsnoop.skel.h PROPERTIES GENERATED TRUE)
target_include_directories(execsnoop PRIVATE ${INC_DIR} ${OBJ_DIR} ${SRC_DIR})
target_compile_options(execsnoop PRIVATE -g -O2 -Wall)
target_link_libraries(execsnoop PRIVATE bpf_so ${ELF_LIB} ${Z_LIB})
set_target_properties(execsnoop PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY ${BIN_DIR}
  # Find libbpf next to the binary: build/lib in the build tree, and
  # <prefix>/<libdir> after installation (no LD_LIBRARY_PATH needed).
  BUILD_RPATH "\$ORIGIN/../lib"
  INSTALL_RPATH "\$ORIGIN/../${CMAKE_INSTALL_LIBDIR}")
add_dependencies(execsnoop skel_h libbpf_build)

# Resource directory of the clang that compiles the user-space code. Without it
# clangd may not find stddef.h when it reads compile_commands.json.
if(CMAKE_C_COMPILER_ID MATCHES "Clang")
  execute_process(COMMAND ${CMAKE_C_COMPILER} -print-resource-dir
                  OUTPUT_VARIABLE CLANG_RESOURCE_DIR
                  OUTPUT_STRIP_TRAILING_WHITESPACE)
  if(CLANG_RESOURCE_DIR)
    target_compile_options(execsnoop PRIVATE -resource-dir=${CLANG_RESOURCE_DIR})
  endif()
endif()

# ------------------------------------------------------------- installation
# cmake --install build [--prefix <dir>]
install(TARGETS execsnoop RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES ${LIB_DIR}/libbpf.so.1 DESTINATION ${CMAKE_INSTALL_LIBDIR})

# Kernel BTF, if export_btf.sh has copied one next to this file. Checked at
# install time, so it is optional.
install(CODE "
  if(EXISTS \"${CMAKE_SOURCE_DIR}/vmlinux.btf\")
    file(INSTALL DESTINATION \"\${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_DATADIR}/execsnoop\"
         TYPE FILE FILES \"${CMAKE_SOURCE_DIR}/vmlinux.btf\")
    message(STATUS \"installed kernel BTF to \${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_DATADIR}/execsnoop\")
  endif()")
