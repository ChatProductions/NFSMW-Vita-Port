cmake_minimum_required(VERSION 3.16)

if(NOT DEFINED CMAKE_TOOLCHAIN_FILE)
  if(NOT DEFINED ENV{VITASDK})
    message(FATAL_ERROR "Set VITASDK to your VitaSDK installation")
  endif()
  set(CMAKE_TOOLCHAIN_FILE "$ENV{VITASDK}/share/vita.toolchain.cmake" CACHE PATH "Vita toolchain")
endif()

project(nfsmw_vita_port VERSION 0.0.10 LANGUAGES C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include("$ENV{VITASDK}/share/vita.cmake" REQUIRED)

# Original-data readers proven by M0-M8. They stay in the runtime build tree so
# we can replace prepared caches with direct retail-file loaders subsystem by subsystem.
add_library(nfsmw_formats STATIC
  src/decomp/bCrc32.cpp
  src/port/BundleProbe.cpp
  src/port/TpkMetadata.cpp
  src/port/TpkTexture.cpp
  src/port/JdlzFile.cpp
  src/port/TpkInventory.cpp
)
target_include_directories(nfsmw_formats PUBLIC include)
target_compile_options(nfsmw_formats PRIVATE -Wall -Wextra)

# Playable Vita runtime imported from the MIT Vish implementation. This is now
# the application entry point; the old milestone slideshow is deliberately not built.
set(NFSMW_RUNTIME_SOURCES
  runtime/src/main.c
  runtime/src/assets/asset_store.c
  runtime/src/career/career_store.c
  runtime/src/career/settings.c
  runtime/src/world/drive_scene.c
  runtime/src/world/traffic.c
  runtime/src/world/free_roam.c
  runtime/src/world/drive_stream.c
  runtime/src/world/world_motion.c
  runtime/src/world/first_race.c
  runtime/src/world/quick_race.c
  runtime/src/world/breakable_props.c
  runtime/src/world/prologue.c
  runtime/src/render/drive_projection.c
  runtime/src/platform/vita/gfx_guard.c
  runtime/src/platform/vita/platform.c
  runtime/src/render/video_renderer.c
  runtime/src/render/frontend_scene.c
  runtime/src/platform/vita/video_player.c
  runtime/src/platform/vita/splash_audio.c
  runtime/src/platform/vita/drive_audio.c
  runtime/src/render/menu_scene.c
  runtime/src/platform/vita/career_ui.c
  runtime/src/platform/vita/settings_ui.c
  runtime/src/platform/vita/quick_race_ui.c
  runtime/src/render/drive_renderer.c
  runtime/src/render/drive_hud.c
  runtime/src/render/world_map.c
  runtime/src/render/world_textures.c
)

add_executable(nfsmw_vita_port ${NFSMW_RUNTIME_SOURCES})

target_include_directories(nfsmw_vita_port PRIVATE
  runtime/include
  include
)
target_compile_options(nfsmw_vita_port PRIVATE -Wall -Wextra)

target_link_options(nfsmw_vita_port PRIVATE
  "-Wl,-T,${CMAKE_CURRENT_SOURCE_DIR}/runtime/cmake/vita_module_gap.ld"
)
foreach(call
    sceGxmBeginScene
    sceGxmReserveVertexDefaultUniformBuffer
    sceGxmReserveFragmentDefaultUniformBuffer
    sceGxmSetUniformDataF
    sceGxmDraw
    sceGxmEndScene)
  target_link_options(nfsmw_vita_port PRIVATE "-Wl,--wrap=${call}")
endforeach()
set_property(TARGET nfsmw_vita_port APPEND PROPERTY LINK_DEPENDS
  "${CMAKE_CURRENT_SOURCE_DIR}/runtime/cmake/vita_module_gap.ld"
)

target_link_libraries(nfsmw_vita_port PRIVATE
  vita2d
  png
  jpeg
  z
  freetype
  m
  SceTouch_stub
  SceGxm_stub
  SceDisplay_stub
  SceCtrl_stub
  SceSysmodule_stub
  SceAvPlayer_stub
  SceIme_stub
  SceAppMgr_stub
  SceAudio_stub
  ScePgf_stub
  ScePvf_stub
  SceCommonDialog_stub
)

vita_create_self(nfsmw_vita_port.self nfsmw_vita_port UNSAFE)

# Keep the clean-port title id. It does not overwrite the separate Vish prototype.
vita_create_vpk(nfsmw_vita_port.vpk "NFMV00001" nfsmw_vita_port.self
  VERSION "00.10"
  NAME "NFSMW Vita Port"
)
