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

add_library(vitasdk_debugscreen STATIC
  third_party/vitasdk_debugscreen/debugScreen.c
)
target_include_directories(vitasdk_debugscreen PUBLIC third_party/vitasdk_debugscreen)

add_executable(nfsmw_vita_port
  src/main.cpp
)

target_include_directories(nfsmw_vita_port PRIVATE
  include
  third_party/vitasdk_debugscreen
)
target_compile_options(nfsmw_vita_port PRIVATE -Wall -Wextra)

target_link_libraries(nfsmw_vita_port PRIVATE
  nfsmw_decomp
  vitasdk_debugscreen
  SceDisplay_stub
  SceCtrl_stub
  SceSysmem_stub
  SceThreadmgr_stub
  SceProcessmgr_stub
)

vita_create_self(nfsmw_vita_port.self nfsmw_vita_port UNSAFE)

vita_create_vpk(nfsmw_vita_port.vpk "NFMV00001" nfsmw_vita_port.self
  VERSION "00.01"
  NAME "NFSMW Vita Port M0"
)
