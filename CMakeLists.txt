cmake_minimum_required(VERSION 3.16)

if(NOT DEFINED CMAKE_TOOLCHAIN_FILE)
  if(NOT DEFINED ENV{VITASDK})
    message(FATAL_ERROR "Set VITASDK to your VitaSDK installation")
  endif()
  set(CMAKE_TOOLCHAIN_FILE "$ENV{VITASDK}/share/vita.toolchain.cmake" CACHE PATH "Vita toolchain")
endif()

project(nfsmw_vita_port VERSION 0.0.1 LANGUAGES C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include("$ENV{VITASDK}/share/vita.cmake" REQUIRED)

add_library(nfsmw_decomp STATIC
  src/decomp/bCrc32.cpp
)

target_include_directories(nfsmw_decomp PUBLIC include)
target_compile_options(nfsmw_decomp PRIVATE -Wall -Wextra)

add_executable(nfsmw_vita_port
  src/main.cpp
  src/port/BundleProbe.cpp
  src/port/TpkMetadata.cpp
  src/port/TpkTexture.cpp
)

target_include_directories(nfsmw_vita_port PRIVATE include)
target_compile_options(nfsmw_vita_port PRIVATE -Wall -Wextra)

target_link_libraries(nfsmw_vita_port PRIVATE
  nfsmw_decomp
  vita2d
  png
  jpeg
  z
  freetype
  m
  SceGxm_stub
  SceDisplay_stub
  SceCtrl_stub
  SceSysmodule_stub
  ScePgf_stub
  ScePvf_stub
  SceCommonDialog_stub
  SceAppMgr_stub
)

vita_create_self(nfsmw_vita_port.self nfsmw_vita_port UNSAFE)

vita_create_vpk(nfsmw_vita_port.vpk "NFMV00001" nfsmw_vita_port.self
  VERSION "00.05"
  NAME "NFSMW Vita Port M4"
)
