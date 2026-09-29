cmake_minimum_required(VERSION 3.16)

if(NOT DEFINED CMAKE_TOOLCHAIN_FILE)
  if(NOT DEFINED ENV{VITASDK})
    message(FATAL_ERROR "Set VITASDK to your VitaSDK installation")
  endif()
  set(CMAKE_TOOLCHAIN_FILE "$ENV{VITASDK}/share/vita.toolchain.cmake" CACHE PATH "Vita toolchain")
endif()

project(nfsmw_vita_port VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include("$ENV{VITASDK}/share/vita.cmake" REQUIRED)

# Direct retail-data code proven in M0-M8. Keep it building while it is wired
# into the playable runtime instead of throwing away the original-data work.
add_library(nfsmw_original_data STATIC
  src/decomp/bCrc32.cpp
  src/port/BundleProbe.cpp
  src/port/TpkMetadata.cpp
  src/port/TpkTexture.cpp
  src/port/JdlzFile.cpp
  src/port/TpkInventory.cpp
)
target_include_directories(nfsmw_original_data PUBLIC include)
target_compile_options(nfsmw_original_data PRIVATE -Wall -Wextra)
target_link_libraries(nfsmw_original_data PUBLIC vita2d)

set(VISH runtime/vish)

add_library(nfsmw_assets STATIC
  ${VISH}/src/assets/asset_store.c
)
target_include_directories(nfsmw_assets PUBLIC ${VISH}/include)
target_compile_options(nfsmw_assets PRIVATE -Wall -Wextra)

add_library(nfsmw_career STATIC
  ${VISH}/src/career/career_store.c
  ${VISH}/src/career/settings.c
)
target_include_directories(nfsmw_career PUBLIC ${VISH}/include)
target_compile_options(nfsmw_career PRIVATE -Wall -Wextra)

add_library(nfsmw_drive STATIC
  ${VISH}/src/world/drive_scene.c
  ${VISH}/src/world/traffic.c
  ${VISH}/src/world/free_roam.c
  ${VISH}/src/world/drive_stream.c
  ${VISH}/src/world/world_motion.c
  ${VISH}/src/world/first_race.c
  ${VISH}/src/world/quick_race.c
  ${VISH}/src/world/breakable_props.c
  ${VISH}/src/world/prologue.c
  ${VISH}/src/render/drive_projection.c
)
target_include_directories(nfsmw_drive PUBLIC ${VISH}/include)
target_compile_options(nfsmw_drive PRIVATE -Wall -Wextra)
target_link_libraries(nfsmw_drive PUBLIC m)

add_executable(nfsmw_vita_port
  ${VISH}/src/main.c
  ${VISH}/src/platform/vita/gfx_guard.c
  ${VISH}/src/platform/vita/platform.c
  ${VISH}/src/render/video_renderer.c
  ${VISH}/src/render/frontend_scene.c
  ${VISH}/src/platform/vita/video_player.c
  ${VISH}/src/platform/vita/splash_audio.c
  ${VISH}/src/platform/vita/drive_audio.c
  ${VISH}/src/render/menu_scene.c
  ${VISH}/src/platform/vita/career_ui.c
  ${VISH}/src/platform/vita/settings_ui.c
  ${VISH}/src/platform/vita/quick_race_ui.c
  ${VISH}/src/render/drive_renderer.c
  ${VISH}/src/render/drive_hud.c
  ${VISH}/src/render/world_map.c
  ${VISH}/src/render/world_textures.c
)

target_include_directories(nfsmw_vita_port PRIVATE ${VISH}/include)
target_compile_options(nfsmw_vita_port PRIVATE -Wall -Wextra)

target_link_options(nfsmw_vita_port PRIVATE
  "-Wl,-T,${CMAKE_CURRENT_SOURCE_DIR}/${VISH}/cmake/vita_module_gap.ld"
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
  "${CMAKE_CURRENT_SOURCE_DIR}/${VISH}/cmake/vita_module_gap.ld"
)

target_link_libraries(nfsmw_vita_port PRIVATE
  nfsmw_assets
  nfsmw_career
  nfsmw_drive
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
vita_create_vpk(nfsmw_vita_port.vpk "NFMV00001" nfsmw_vita_port.self
  VERSION "00.10"
  NAME "NFSMW Vita Port"
)
