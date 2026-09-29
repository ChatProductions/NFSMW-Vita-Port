cmake_minimum_required(VERSION 3.16)

if(NOT DEFINED CMAKE_TOOLCHAIN_FILE)
  if(NOT DEFINED ENV{VITASDK})
    message(FATAL_ERROR "Set VITASDK to your VitaSDK installation")
  endif()
  set(CMAKE_TOOLCHAIN_FILE "$ENV{VITASDK}/share/vita.toolchain.cmake" CACHE PATH "Vita toolchain")
endif()

project(nfsmw_vita_port VERSION 0.12.0 LANGUAGES C CXX)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
include("$ENV{VITASDK}/share/vita.cmake" REQUIRED)

set(M12_FFMPEG_ROOT "${CMAKE_SOURCE_DIR}/build/deps/ffmpeg-vp6-vita" CACHE PATH "M12 Vita FFmpeg prefix")
foreach(lib avformat avcodec avutil swscale)
  if(NOT EXISTS "${M12_FFMPEG_ROOT}/lib/lib${lib}.a")
    message(FATAL_ERROR "Missing M12 Vita FFmpeg library: ${M12_FFMPEG_ROOT}/lib/lib${lib}.a")
  endif()
endforeach()

add_executable(nfsmw_vita_port
  src/m12_main.cpp
  src/port/GeometryFile.cpp
  src/port/RawWorld.cpp
  src/port/JdlzFile.cpp
  src/port/TpkInventory.cpp
  src/port/TpkMetadata.cpp
  src/port/TpkTexture.cpp
  src/port/RetailFrontend.cpp
  src/port/RetailVideo.cpp
)

target_include_directories(nfsmw_vita_port PRIVATE
  include
  "${M12_FFMPEG_ROOT}/include"
)
target_compile_options(nfsmw_vita_port PRIVATE -Wall -Wextra)

target_link_libraries(nfsmw_vita_port PRIVATE
  "${M12_FFMPEG_ROOT}/lib/libavformat.a"
  "${M12_FFMPEG_ROOT}/lib/libavcodec.a"
  "${M12_FFMPEG_ROOT}/lib/libswscale.a"
  "${M12_FFMPEG_ROOT}/lib/libavutil.a"
  vita2d
  png
  jpeg
  z
  freetype
  pthread
  m
  SceTouch_stub
  SceGxm_stub
  SceDisplay_stub
  SceCtrl_stub
  SceSysmodule_stub
  ScePgf_stub
  ScePvf_stub
  SceAppMgr_stub
  SceCommonDialog_stub
)

vita_create_self(nfsmw_vita_port.self nfsmw_vita_port UNSAFE)
vita_create_vpk(nfsmw_vita_port.vpk "NFMV00001" nfsmw_vita_port.self
  VERSION "00.12"
  NAME "NFSMW Vita Port M12"
)
