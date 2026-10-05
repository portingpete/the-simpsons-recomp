cmake_minimum_required(VERSION 3.24)
project(SimpsonsNative LANGUAGES CXX)
if(NOT WIN32 OR NOT CMAKE_SIZEOF_VOID_P EQUAL 8 OR NOT CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
  message(FATAL_ERROR "Native x64 Windows ClangCL is required")
endif()
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
option(SIMPSONS_DIAGNOSTIC "Permit explicit traps for incomplete translation" OFF)
# The title-screen ThinLTO gain did not transfer to measured gameplay. Keep
# ordinary code generation as the default; ThinLTO remains an opt-in trial.
option(SIMPSONS_THINLTO "Enable Clang ThinLTO for the native game" OFF)
set(CMAKE_INTERPROCEDURAL_OPTIMIZATION "${SIMPSONS_THINLTO}")
if(SIMPSONS_THINLTO)
  set(SIMPSONS_THINLTO_CACHE "${CMAKE_BINARY_DIR}/thinlto-cache")
  file(MAKE_DIRECTORY "${SIMPSONS_THINLTO_CACHE}")
  file(TO_CMAKE_PATH "${SIMPSONS_THINLTO_CACHE}" SIMPSONS_THINLTO_CACHE_CMAKE)
  set(CMAKE_EXE_LINKER_FLAGS
    "${CMAKE_EXE_LINKER_FLAGS} /opt:lldltojobs=8 /lldltocache:${SIMPSONS_THINLTO_CACHE_CMAKE} /lldltocachepolicy:cache_size_bytes=2g")
  set(CMAKE_JOB_POOLS "thinlto_link=1")
  set(CMAKE_JOB_POOL_LINK "thinlto_link")
  message(STATUS "SIMPSONS_THINLTO=ON cache=\"${SIMPSONS_THINLTO_CACHE_CMAKE}\"")
else()
  message(STATUS "SIMPSONS_THINLTO=OFF")
endif()
# Optional IR-based PGO is independent of ThinLTO. No /GL, fast-math,
# architecture, semantic, or guest-clock changes.
set(SIMPSONS_PGO_MODE "OFF" CACHE STRING "IR-based PGO mode: OFF, GENERATE, or USE")
set_property(CACHE SIMPSONS_PGO_MODE PROPERTY STRINGS OFF GENERATE USE)
set(SIMPSONS_PGO_DIR "${CMAKE_BINARY_DIR}/pgo" CACHE PATH "Output directory for GENERATE raw profiles")
set(SIMPSONS_PGO_PROFILE "" CACHE FILEPATH "Merged .profdata file for USE builds")
if(NOT SIMPSONS_PGO_MODE STREQUAL "OFF" AND NOT SIMPSONS_PGO_MODE STREQUAL "GENERATE" AND NOT SIMPSONS_PGO_MODE STREQUAL "USE")
  message(FATAL_ERROR "SIMPSONS_PGO_MODE must be OFF, GENERATE, or USE, got '${SIMPSONS_PGO_MODE}'.")
endif()
if(SIMPSONS_PGO_MODE STREQUAL "GENERATE")
  get_filename_component(SIMPSONS_PGO_GENERATE_DIR_ABS "${SIMPSONS_PGO_DIR}" ABSOLUTE)
  file(MAKE_DIRECTORY "${SIMPSONS_PGO_GENERATE_DIR_ABS}")
  execute_process(COMMAND "${CMAKE_CXX_COMPILER}" -print-resource-dir
    OUTPUT_VARIABLE SIMPSONS_PGO_RESOURCE_DIR OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
  string(STRIP "${SIMPSONS_PGO_RESOURCE_DIR}" SIMPSONS_PGO_RESOURCE_DIR)
  set(SIMPSONS_PGO_RUNTIME_LIB "${SIMPSONS_PGO_RESOURCE_DIR}/lib/windows/clang_rt.profile-x86_64.lib")
  if(SIMPSONS_PGO_RESOURCE_DIR STREQUAL "" OR NOT EXISTS "${SIMPSONS_PGO_RUNTIME_LIB}")
    message(FATAL_ERROR "SIMPSONS_PGO_MODE=GENERATE requires clang profile runtime '${SIMPSONS_PGO_RUNTIME_LIB}'.")
  endif()
  get_filename_component(SIMPSONS_PGO_RUNTIME_LIB_DIR "${SIMPSONS_PGO_RUNTIME_LIB}" DIRECTORY)
  add_link_options("$<$<CONFIG:Release>:/LIBPATH:${SIMPSONS_PGO_RUNTIME_LIB_DIR}>" "$<$<CONFIG:RelWithDebInfo>:/LIBPATH:${SIMPSONS_PGO_RUNTIME_LIB_DIR}>")
  message(STATUS "SIMPSONS_PGO_MODE=GENERATE rawdir=\"${SIMPSONS_PGO_GENERATE_DIR_ABS}\"")
elseif(SIMPSONS_PGO_MODE STREQUAL "USE")
  if(SIMPSONS_PGO_PROFILE STREQUAL "")
    message(FATAL_ERROR "SIMPSONS_PGO_MODE=USE requires -DSIMPSONS_PGO_PROFILE=<merged .profdata>.")
  endif()
  get_filename_component(SIMPSONS_PGO_PROFILE_ABS "${SIMPSONS_PGO_PROFILE}" ABSOLUTE)
  if(NOT EXISTS "${SIMPSONS_PGO_PROFILE_ABS}")
    message(FATAL_ERROR "SIMPSONS_PGO_PROFILE not found: '${SIMPSONS_PGO_PROFILE_ABS}'.")
  endif()
  file(SIZE "${SIMPSONS_PGO_PROFILE_ABS}" SIMPSONS_PGO_PROFILE_SIZE)
  if(SIMPSONS_PGO_PROFILE_SIZE EQUAL 0)
    message(FATAL_ERROR "SIMPSONS_PGO_PROFILE is empty: '${SIMPSONS_PGO_PROFILE_ABS}'.")
  endif()
  message(STATUS "SIMPSONS_PGO_MODE=USE profile=\"${SIMPSONS_PGO_PROFILE_ABS}\"")
else()
  message(STATUS "SIMPSONS_PGO_MODE=OFF")
endif()
find_package(Python3 3.11 REQUIRED COMPONENTS Interpreter)
set(NATIVE_VIDEO_PACKAGE "${CMAKE_SOURCE_DIR}/build/native-assets/frontend/frontend.str")
set(NATIVE_VIDEO_GAME_PACKAGE "${CMAKE_SOURCE_DIR}/build/native-assets/simpsons_chars/simpsons_chars_global.str")
add_custom_command(OUTPUT "${NATIVE_VIDEO_PACKAGE}" "${NATIVE_VIDEO_GAME_PACKAGE}"
  COMMAND "${Python3_EXECUTABLE}" -B "${CMAKE_SOURCE_DIR}/tools/build_native_video_menu.py" --output-root "${CMAKE_SOURCE_DIR}/build/native-assets"
  DEPENDS tools/build_native_video_menu.py tools/build_native_mouse_menu.py tools/inspect_assets.py "Simpsons Game, The (USA)/frontend/frontend.str" "Simpsons Game, The (USA)/simpsons_chars/simpsons_chars_global.str"
  VERBATIM)
add_custom_target(NativeVideoMenu
  COMMAND "${CMAKE_COMMAND}" -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/native-assets/frontend"
  COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${NATIVE_VIDEO_PACKAGE}" "${CMAKE_CURRENT_BINARY_DIR}/native-assets/frontend/frontend.str"
  COMMAND "${CMAKE_COMMAND}" -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/native-assets/simpsons_chars"
  COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${NATIVE_VIDEO_GAME_PACKAGE}" "${CMAKE_CURRENT_BINARY_DIR}/native-assets/simpsons_chars/simpsons_chars_global.str"
  DEPENDS "${NATIVE_VIDEO_PACKAGE}" "${NATIVE_VIDEO_GAME_PACKAGE}" VERBATIM)
set(SIMPSONS_AUDIO_CODEC "${CMAKE_SOURCE_DIR}/build/audio-codec/install")
execute_process(COMMAND "${Python3_EXECUTABLE}" -B "${CMAKE_SOURCE_DIR}/tools/build_native_audio_codec.py" --verify
  RESULT_VARIABLE audio_result)
if(NOT audio_result EQUAL 0)
  message(FATAL_ERROR "Native audio dependency verification failed; run python tools/build_native_audio_codec.py")
endif()
add_custom_target(VerifyNativeAudio
  COMMAND "${Python3_EXECUTABLE}" -B "${CMAKE_SOURCE_DIR}/tools/build_native_audio_codec.py" --verify
  COMMAND "${CMAKE_COMMAND}" -E copy_if_different
    "${SIMPSONS_AUDIO_CODEC}/bin/avcodec-simpsonsxma-62.dll"
    "${SIMPSONS_AUDIO_CODEC}/bin/avutil-simpsonsxma-60.dll"
    "${SIMPSONS_AUDIO_CODEC}/bin/libwinpthread-1.dll" "${CMAKE_CURRENT_BINARY_DIR}"
  VERBATIM)
foreach(CODEC_COMPONENT avcodec avutil)
  if(CODEC_COMPONENT STREQUAL "avcodec")
    set(CODEC_MAJOR 62)
  else()
    set(CODEC_MAJOR 60)
  endif()
  add_library(Simpsons_${CODEC_COMPONENT} SHARED IMPORTED)
  set_target_properties(Simpsons_${CODEC_COMPONENT} PROPERTIES
    IMPORTED_IMPLIB "${SIMPSONS_AUDIO_CODEC}/lib/${CODEC_COMPONENT}-simpsonsxma.lib"
    IMPORTED_LOCATION "${SIMPSONS_AUDIO_CODEC}/bin/${CODEC_COMPONENT}-simpsonsxma-${CODEC_MAJOR}.dll"
    INTERFACE_INCLUDE_DIRECTORIES "${SIMPSONS_AUDIO_CODEC}/include")
endforeach()
add_library(SimpsonsAudio STATIC audio/native_xma_codec.cpp audio/xma_source.cpp audio/ea_xma_block.cpp audio/audio_catalog.cpp audio/resident_audio_catalog.cpp audio/amx_audio_catalog.cpp audio/resident_xma.cpp)
target_include_directories(SimpsonsAudio PUBLIC "${CMAKE_SOURCE_DIR}")
target_compile_definitions(SimpsonsAudio PUBLIC NOMINMAX WIN32_LEAN_AND_MEAN)
target_compile_options(SimpsonsAudio PRIVATE /fp:strict)
target_link_libraries(SimpsonsAudio PUBLIC Simpsons_avcodec Simpsons_avutil)
add_dependencies(SimpsonsAudio VerifyNativeAudio)
add_library(SimpsonsAudioOutput STATIC audio/native_audio_output.cpp audio/dac_gain.cpp audio/dac_processor.cpp)
target_include_directories(SimpsonsAudioOutput PUBLIC "${CMAKE_SOURCE_DIR}")
target_compile_definitions(SimpsonsAudioOutput PUBLIC NOMINMAX WIN32_LEAN_AND_MEAN)
target_compile_options(SimpsonsAudioOutput PRIVATE /fp:strict /W4 /WX)
target_link_libraries(SimpsonsAudioOutput PUBLIC xaudio2 ole32 ksuser uuid)
set(SIMPSONS_GENERATED_DIR "${CMAKE_SOURCE_DIR}/build/generated")
set(SIMPSONS_EFFECT_CATALOG "${SIMPSONS_GENERATED_DIR}/effect_catalog.h")
add_custom_command(OUTPUT "${SIMPSONS_EFFECT_CATALOG}"
  COMMAND "${Python3_EXECUTABLE}" -B "${CMAKE_SOURCE_DIR}/tools/generate_effect_catalog.py"
  DEPENDS tools/generate_effect_catalog.py tools/analyze_effect_catalog.py
    tools/analyze_post_effect_catalog.py tools/analyze_poststart_integration.py
    analysis/native-effect-catalog.json analysis/native-post-effect-catalog.json analysis/simpsons.pe
  VERBATIM)
add_custom_target(VerifyEffectCatalog
  COMMAND "${Python3_EXECUTABLE}" -B "${CMAKE_SOURCE_DIR}/tools/generate_effect_catalog.py" --check
  DEPENDS "${SIMPSONS_EFFECT_CATALOG}" VERBATIM)
set(GATE_ARGS --verify)
if(SIMPSONS_DIAGNOSTIC)
  list(APPEND GATE_ARGS --diagnostic)
endif()
execute_process(COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tools/recompile.py" ${GATE_ARGS} RESULT_VARIABLE result)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "AOT gate failed; run tools/build.ps1")
endif()
include("${SIMPSONS_GENERATED_DIR}/sources.cmake")
add_custom_target(VerifyAOT COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tools/recompile.py" ${GATE_ARGS})
add_library(SimpsonsPPC STATIC ${SIMPSONS_PPC_SOURCES})
add_dependencies(SimpsonsPPC VerifyAOT)
target_include_directories(SimpsonsPPC PUBLIC "${SIMPSONS_GENERATED_DIR}" "${CMAKE_SOURCE_DIR}/third_party/XenonRecomp/thirdparty/simde")
target_compile_definitions(SimpsonsPPC PUBLIC NOMINMAX WIN32_LEAN_AND_MEAN _CRT_SECURE_NO_WARNINGS)
target_compile_options(SimpsonsPPC PRIVATE /fp:strict /clang:-mssse3)
# Keep the checked scalar memory helper visible to the native compiler only in
# the measured hot chunks. The original generated bodies and FP rules remain.
file(READ "${CMAKE_SOURCE_DIR}/config/inline_memory.json" INLINE_MEMORY_JSON)
string(JSON INLINE_MEMORY_COUNT LENGTH "${INLINE_MEMORY_JSON}")
math(EXPR INLINE_MEMORY_LAST "${INLINE_MEMORY_COUNT} - 1")
foreach(INDEX RANGE ${INLINE_MEMORY_LAST})
  string(JSON CHUNK GET "${INLINE_MEMORY_JSON}" ${INDEX} chunk)
  string(JSON ENTRY GET "${INLINE_MEMORY_JSON}" ${INDEX} entry)
  set(INLINE_MEMORY_SOURCE "${SIMPSONS_GENERATED_DIR}/ppc_recomp.${CHUNK}.cpp")
  file(READ "${INLINE_MEMORY_SOURCE}" INLINE_MEMORY_BODY)
  string(FIND "${INLINE_MEMORY_BODY}" "PPC_FUNC_IMPL(__imp__sub_${ENTRY}) {" INLINE_MEMORY_FOUND)
  if(INLINE_MEMORY_FOUND EQUAL -1)
    message(FATAL_ERROR "Inline memory chunk ${CHUNK} no longer contains original ${ENTRY}; requalify the selection")
  endif()
  set_source_files_properties("${INLINE_MEMORY_SOURCE}" PROPERTIES
    COMPILE_OPTIONS "/FI${CMAKE_SOURCE_DIR}/runtime/aot_inline_memory.h")
endforeach()
# Fast guest memory for the recompiled original code (see runtime/aot_fast_pointer.h):
# permissions come from the page protection of the reserved guest window instead of a
# per-access table lookup. Native hooks and tests keep the fully checked path.
option(SIMPSONS_FAST_GUEST_MEMORY "Force the hardware-checked fast memory path into every original AOT chunk" ON)
if(SIMPSONS_FAST_GUEST_MEMORY)
  foreach(FAST_MEMORY_SOURCE ${SIMPSONS_PPC_SOURCES})
    if(FAST_MEMORY_SOURCE MATCHES "ppc_recomp[.][0-9]+[.]cpp$")
      set_source_files_properties("${FAST_MEMORY_SOURCE}" PROPERTIES
        COMPILE_OPTIONS "/FI${CMAKE_SOURCE_DIR}/runtime/aot_fast_pointer.h")
    endif()
  endforeach()
  target_compile_definitions(SimpsonsPPC PUBLIC SIMPSONS_FAST_GUEST_MEMORY=1)
  message(STATUS "SIMPSONS_FAST_GUEST_MEMORY=ON")
endif()
file(READ "${CMAKE_SOURCE_DIR}/config/native_sources.json" NATIVE_SOURCES_JSON)
string(JSON NATIVE_SOURCE_COUNT LENGTH "${NATIVE_SOURCES_JSON}")
math(EXPR NATIVE_SOURCE_LAST "${NATIVE_SOURCE_COUNT} - 1")
set(NATIVE_SOURCES)
foreach(INDEX RANGE ${NATIVE_SOURCE_LAST})
  string(JSON SOURCE GET "${NATIVE_SOURCES_JSON}" ${INDEX})
  list(APPEND NATIVE_SOURCES "${SOURCE}")
endforeach()
add_library(SimpsonsRuntime STATIC ${NATIVE_SOURCES})
target_include_directories(SimpsonsRuntime PUBLIC "${CMAKE_SOURCE_DIR}")
target_link_libraries(SimpsonsRuntime PUBLIC SimpsonsPPC SimpsonsAudio SimpsonsAudioOutput bcrypt user32 gdi32 xinput)
find_program(SIMPSONS_FXC fxc REQUIRED)
set(SIMPSONS_SHADER_DIR "${CMAKE_SOURCE_DIR}/build/shaders")
file(MAKE_DIRECTORY "${SIMPSONS_SHADER_DIR}")
set(SIMPSONS_SCREEN_HEADERS)
foreach(ENTRY VSFlat VSTextured PSFlat PSTextured PSOriginalFlat PSOriginalTextured)
  if(ENTRY MATCHES "^VS")
    set(PROFILE vs_5_0)
  else()
    set(PROFILE ps_5_0)
  endif()
  set(HEADER "${SIMPSONS_SHADER_DIR}/${ENTRY}.h")
  add_custom_command(OUTPUT "${HEADER}"
    COMMAND "${SIMPSONS_FXC}" /nologo /Ges /Gis /O3 /T ${PROFILE} /E ${ENTRY}
      /Fh "${HEADER}" /Vn "k${ENTRY}" "${CMAKE_SOURCE_DIR}/renderer/screen.hlsl"
    DEPENDS renderer/screen.hlsl VERBATIM)
  list(APPEND SIMPSONS_SCREEN_HEADERS "${HEADER}")
endforeach()
set(SIMPSONS_FOURTAP_HEADERS)
set(SIMPSONS_EDGE_HEADERS)
foreach(ENTRY VSEdge PSEdge)
  if(ENTRY MATCHES "^VS")
    set(PROFILE vs_5_0)
  else()
    set(PROFILE ps_5_0)
  endif()
  set(HEADER "${SIMPSONS_SHADER_DIR}/${ENTRY}.h")
  add_custom_command(OUTPUT "${HEADER}"
    COMMAND "${SIMPSONS_FXC}" /nologo /Ges /Gis /O3 /T ${PROFILE} /E ${ENTRY}
      /Fh "${HEADER}" /Vn "k${ENTRY}" "${CMAKE_SOURCE_DIR}/renderer/edge_shader.hlsl"
    DEPENDS renderer/edge_shader.hlsl VERBATIM)
  list(APPEND SIMPSONS_EDGE_HEADERS "${HEADER}")
endforeach()
set(SIMPSONS_MOVIE_HEADERS)
set(SIMPSONS_AA_HEADERS)
foreach(ENTRY VSAA PSAA)
  if(ENTRY MATCHES "^VS")
    set(PROFILE vs_5_0)
  else()
    set(PROFILE ps_5_0)
  endif()
  set(HEADER "${SIMPSONS_SHADER_DIR}/${ENTRY}.h")
  add_custom_command(OUTPUT "${HEADER}"
    COMMAND "${SIMPSONS_FXC}" /nologo /Ges /Gis /O3 /T ${PROFILE} /E ${ENTRY}
      /Fh "${HEADER}" /Vn "k${ENTRY}" "${CMAKE_SOURCE_DIR}/renderer/aa_shader.hlsl"
    DEPENDS renderer/aa_shader.hlsl VERBATIM)
  list(APPEND SIMPSONS_AA_HEADERS "${HEADER}")
endforeach()
set(SIMPSONS_EDGEAA_HEADERS)
set(SIMPSONS_NATIVE_AA_HEADERS)
foreach(ENTRY VSAntialiasing PSFXAA PSSSAA4x)
  if(ENTRY MATCHES "^VS")
    set(PROFILE vs_5_0)
  else()
    set(PROFILE ps_5_0)
  endif()
  set(HEADER "${SIMPSONS_SHADER_DIR}/${ENTRY}.h")
  add_custom_command(OUTPUT "${HEADER}"
    COMMAND "${SIMPSONS_FXC}" /nologo /Ges /Gis /O3 /T ${PROFILE} /E ${ENTRY}
      /Fh "${HEADER}" /Vn "k${ENTRY}" "${CMAKE_SOURCE_DIR}/renderer/antialiasing.hlsl"
    DEPENDS renderer/antialiasing.hlsl VERBATIM)
  list(APPEND SIMPSONS_NATIVE_AA_HEADERS "${HEADER}")
endforeach()
foreach(ENTRY VSEdgeAA PSEdgeAA)
  if(ENTRY MATCHES "^VS")
    set(PROFILE vs_5_0)
  else()
    set(PROFILE ps_5_0)
  endif()
  set(HEADER "${SIMPSONS_SHADER_DIR}/${ENTRY}.h")
  add_custom_command(OUTPUT "${HEADER}"
    COMMAND "${SIMPSONS_FXC}" /nologo /Ges /Gis /O3 /T ${PROFILE} /E ${ENTRY}
      /Fh "${HEADER}" /Vn "k${ENTRY}" "${CMAKE_SOURCE_DIR}/renderer/edgeaa_shader.hlsl"
    DEPENDS renderer/edgeaa_shader.hlsl VERBATIM)
  list(APPEND SIMPSONS_EDGEAA_HEADERS "${HEADER}")
endforeach()
foreach(ENTRY PSMovie PSMoviePacked)
  set(HEADER "${SIMPSONS_SHADER_DIR}/${ENTRY}.h")
  add_custom_command(OUTPUT "${HEADER}"
    COMMAND "${SIMPSONS_FXC}" /nologo /Ges /Gis /O3 /T ps_5_0 /E ${ENTRY}
      /Fh "${HEADER}" /Vn "k${ENTRY}" "${CMAKE_SOURCE_DIR}/renderer/movie_shader.hlsl"
    DEPENDS renderer/movie_shader.hlsl VERBATIM)
  list(APPEND SIMPSONS_MOVIE_HEADERS "${HEADER}")
endforeach()
foreach(ENTRY VSFourTap PSFourTap)
  if(ENTRY MATCHES "^VS")
    set(PROFILE vs_5_0)
  else()
    set(PROFILE ps_5_0)
  endif()
  set(HEADER "${SIMPSONS_SHADER_DIR}/${ENTRY}.h")
  add_custom_command(OUTPUT "${HEADER}"
    COMMAND "${SIMPSONS_FXC}" /nologo /Ges /Gis /O3 /T ${PROFILE} /E ${ENTRY}
      /Fh "${HEADER}" /Vn "k${ENTRY}" "${CMAKE_SOURCE_DIR}/renderer/fourtapblend.hlsl"
    DEPENDS renderer/fourtapblend.hlsl VERBATIM)
  list(APPEND SIMPSONS_FOURTAP_HEADERS "${HEADER}")
endforeach()
set(SIMPSONS_SHADOW_HEADERS)
set(SIMPSONS_SHADOW_ALPHA_HEADERS)
foreach(ENTRY PSShadowDepthAlpha VSShadowAlphaProbe)
  if(ENTRY MATCHES "^VS")
    set(PROFILE vs_5_0)
  else()
    set(PROFILE ps_5_0)
  endif()
  set(HEADER "${SIMPSONS_SHADER_DIR}/${ENTRY}.h")
  add_custom_command(OUTPUT "${HEADER}"
    COMMAND "${SIMPSONS_FXC}" /nologo /Ges /Gis /O3 /T ${PROFILE} /E ${ENTRY}
      /Fh "${HEADER}" /Vn "k${ENTRY}" "${CMAKE_SOURCE_DIR}/renderer/shadow_alpha_shader.hlsl"
    DEPENDS renderer/shadow_alpha_shader.hlsl VERBATIM)
  list(APPEND SIMPSONS_SHADOW_ALPHA_HEADERS "${HEADER}")
endforeach()
foreach(ENTRY VSShadowDepth GSShadowDepthProbe)
  if(ENTRY MATCHES "^VS")
    set(PROFILE vs_5_0)
  else()
    set(PROFILE gs_5_0)
  endif()
  set(HEADER "${SIMPSONS_SHADER_DIR}/${ENTRY}.h")
  add_custom_command(OUTPUT "${HEADER}"
    COMMAND "${SIMPSONS_FXC}" /nologo /Ges /Gis /O3 /T ${PROFILE} /E ${ENTRY}
      /Fh "${HEADER}" /Vn "k${ENTRY}" "${CMAKE_SOURCE_DIR}/renderer/shadow_depth_shader.hlsl"
    DEPENDS renderer/shadow_depth_shader.hlsl VERBATIM)
  list(APPEND SIMPSONS_SHADOW_HEADERS "${HEADER}")
endforeach()
set(SIMPSONS_ZPREPASS_HEADERS)
foreach(ENTRY VSZPrepass GSZPrepassProbe)
  if(ENTRY MATCHES "^VS")
    set(PROFILE vs_5_0)
  else()
    set(PROFILE gs_5_0)
  endif()
  set(HEADER "${SIMPSONS_SHADER_DIR}/${ENTRY}.h")
  add_custom_command(OUTPUT "${HEADER}"
    COMMAND "${SIMPSONS_FXC}" /nologo /Ges /Gis /O3 /T ${PROFILE} /E ${ENTRY}
      /Fh "${HEADER}" /Vn "k${ENTRY}" "${CMAKE_SOURCE_DIR}/renderer/zprepass_shader.hlsl"
    DEPENDS renderer/zprepass_shader.hlsl VERBATIM)
  list(APPEND SIMPSONS_ZPREPASS_HEADERS "${HEADER}")
endforeach()
set(SIMPSONS_IM2D_HEADERS)
set(SIMPSONS_MONO_SOURCE "${SIMPSONS_SHADER_DIR}/mono_shader.hlsl")
add_custom_command(OUTPUT "${SIMPSONS_MONO_SOURCE}"
  COMMAND "${Python3_EXECUTABLE}" -B "${CMAKE_SOURCE_DIR}/tools/analyze_mono_shader.py" --verify --hlsl "${SIMPSONS_MONO_SOURCE}"
  DEPENDS tools/analyze_mono_shader.py tools/analyze_screen_shaders.py tools/analyze_edge_shaders.py tools/analyze_fourtap_shaders.py
  VERBATIM)
set(SIMPSONS_MONO_HEADERS)
foreach(ENTRY VSMono PSMono GSMonoProbe VSMonoPixelProbe)
  if(ENTRY MATCHES "^VS")
    set(PROFILE vs_5_0)
  elseif(ENTRY MATCHES "^GS")
    set(PROFILE gs_5_0)
  else()
    set(PROFILE ps_5_0)
  endif()
  set(HEADER "${SIMPSONS_SHADER_DIR}/${ENTRY}.h")
  add_custom_command(OUTPUT "${HEADER}"
    COMMAND "${SIMPSONS_FXC}" /nologo /Ges /Gis /O3 /T ${PROFILE} /E ${ENTRY}
      /Fh "${HEADER}" /Vn "k${ENTRY}" "${SIMPSONS_MONO_SOURCE}"
    DEPENDS "${SIMPSONS_MONO_SOURCE}" VERBATIM)
  list(APPEND SIMPSONS_MONO_HEADERS "${HEADER}")
endforeach()
set(SIMPSONS_POST_FILTER_HEADERS)
foreach(ENTRY VSPostAutoTextured PSPostAlphaToRGBA PSPostTextured PSPostFiltered PSPostGlow
    PSPostAlphaToRGBADraw PSPostTexturedDraw PSPostFilteredDraw PSPostGlowDraw GSPostProbe)
  if(ENTRY MATCHES "^VS")
    set(PROFILE vs_5_0)
  elseif(ENTRY MATCHES "^GS")
    set(PROFILE gs_5_0)
  else()
    set(PROFILE ps_5_0)
  endif()
  set(HEADER "${SIMPSONS_SHADER_DIR}/${ENTRY}.h")
  add_custom_command(OUTPUT "${HEADER}"
    COMMAND "${Python3_EXECUTABLE}" -B "${CMAKE_SOURCE_DIR}/tools/analyze_post_filter_shaders.py" --verify
    COMMAND "${SIMPSONS_FXC}" /nologo /Ges /Gis /O3 /T ${PROFILE} /E ${ENTRY}
      /Fh "${HEADER}" /Vn "k${ENTRY}" "${CMAKE_SOURCE_DIR}/renderer/post_filter_shader.hlsl"
    DEPENDS renderer/post_filter_shader.hlsl tools/analyze_post_filter_shaders.py
      tools/analyze_screen_shaders.py tools/analyze_edge_shaders.py tools/analyze_fourtap_shaders.py analysis/simpsons.pe VERBATIM)
  list(APPEND SIMPSONS_POST_FILTER_HEADERS "${HEADER}")
endforeach()
set(SIMPSONS_MONO_DRAW_HEADER "${SIMPSONS_SHADER_DIR}/PSMonoDraw.h")
add_custom_command(OUTPUT "${SIMPSONS_MONO_DRAW_HEADER}"
  COMMAND "${SIMPSONS_FXC}" /nologo /Ges /Gis /O3 /T ps_5_0 /E PSMonoDraw
    /I "${SIMPSONS_SHADER_DIR}" /Fh "${SIMPSONS_MONO_DRAW_HEADER}" /Vn kPSMonoDraw "${CMAKE_SOURCE_DIR}/renderer/mono_draw.hlsl"
  DEPENDS renderer/mono_draw.hlsl renderer/shadow_mesh.hlsl "${SIMPSONS_MONO_SOURCE}" VERBATIM)
set(SIMPSONS_RIGID_HEADERS)
foreach(ENTRY VSRigid PSRigid GSRigidProbe VSRigidPixelProbe PSRigidNormalProbe)
  if(ENTRY MATCHES "^VS")
    set(PROFILE vs_5_0)
  elseif(ENTRY MATCHES "^GS")
    set(PROFILE gs_5_0)
  else()
    set(PROFILE ps_5_0)
  endif()
  set(HEADER "${SIMPSONS_SHADER_DIR}/${ENTRY}.h")
  add_custom_command(OUTPUT "${HEADER}"
    COMMAND "${SIMPSONS_FXC}" /nologo /Ges /Gis /O3 /T ${PROFILE} /E ${ENTRY}
      /Fh "${HEADER}" /Vn "k${ENTRY}" "${CMAKE_SOURCE_DIR}/renderer/rigid_shader.hlsl"
    DEPENDS renderer/rigid_shader.hlsl VERBATIM)
  list(APPEND SIMPSONS_RIGID_HEADERS "${HEADER}")
endforeach()
set(SIMPSONS_SHADOW_MESH_HEADER "${SIMPSONS_SHADER_DIR}/PSShadowMeshDepth.h")
foreach(ENTRY VSRigidTextured PSRigidTextured)
  if(ENTRY MATCHES "^VS")
    set(PROFILE vs_5_0)
  else()
    set(PROFILE ps_5_0)
  endif()
  set(HEADER "${SIMPSONS_SHADER_DIR}/${ENTRY}.h")
  add_custom_command(OUTPUT "${HEADER}"
    COMMAND "${SIMPSONS_FXC}" /nologo /Ges /Gis /O3 /T ${PROFILE} /E ${ENTRY}
      /Fh "${HEADER}" /Vn "k${ENTRY}" "${CMAKE_SOURCE_DIR}/renderer/rigid_textured_shader.hlsl"
    DEPENDS renderer/rigid_textured_shader.hlsl renderer/rigid_shader.hlsl VERBATIM)
  list(APPEND SIMPSONS_RIGID_HEADERS "${HEADER}")
endforeach()
set(SIMPSONS_MULTITONE_SOURCE "${SIMPSONS_SHADER_DIR}/rigid_multitone_shader.hlsl")
add_custom_command(OUTPUT "${SIMPSONS_MULTITONE_SOURCE}"
  COMMAND "${Python3_EXECUTABLE}" -B "${CMAKE_SOURCE_DIR}/tools/analyze_rigid_multitone_shader.py"
    --emit-hlsl "${SIMPSONS_MULTITONE_SOURCE}"
  DEPENDS tools/analyze_rigid_multitone_shader.py tools/analyze_rigid_shader.py
    tools/analyze_screen_shaders.py tools/analyze_edge_shaders.py analysis/simpsons.pe VERBATIM)
foreach(ENTRY VSRigidMultitone PSRigidMultitone PSRigidMultitoneDraw GSRigidMultitoneProbe VSRigidMultitonePixelProbe)
  if(ENTRY MATCHES "^VS")
    set(PROFILE vs_5_0)
  elseif(ENTRY MATCHES "^GS")
    set(PROFILE gs_5_0)
  else()
    set(PROFILE ps_5_0)
  endif()
  if(ENTRY STREQUAL "GSRigidMultitoneProbe" OR ENTRY STREQUAL "VSRigidMultitonePixelProbe")
    set(SOURCE "${CMAKE_SOURCE_DIR}/renderer/rigid_multitone_probe.hlsl")
  elseif(ENTRY STREQUAL "PSRigidMultitoneDraw")
    set(SOURCE "${CMAKE_SOURCE_DIR}/renderer/rigid_multitone_draw.hlsl")
  else()
    set(SOURCE "${SIMPSONS_MULTITONE_SOURCE}")
  endif()
  set(HEADER "${SIMPSONS_SHADER_DIR}/${ENTRY}.h")
  add_custom_command(OUTPUT "${HEADER}"
    COMMAND "${SIMPSONS_FXC}" /nologo /Ges /Gis /WX /O3 /T ${PROFILE} /E ${ENTRY}
      /I "${SIMPSONS_SHADER_DIR}" /Fh "${HEADER}" /Vn "k${ENTRY}" "${SOURCE}"
    DEPENDS "${SIMPSONS_MULTITONE_SOURCE}" renderer/rigid_multitone_probe.hlsl renderer/rigid_multitone_draw.hlsl renderer/shadow_mesh.hlsl VERBATIM)
  list(APPEND SIMPSONS_RIGID_HEADERS "${HEADER}")
endforeach()
foreach(ENTRY VSRigidGloss PSRigidGloss VSRigidGlossPixelProbe GSRigidGlossProbe)
  if(ENTRY MATCHES "^VS")
    set(PROFILE vs_5_0)
  elseif(ENTRY MATCHES "^GS")
    set(PROFILE gs_5_0)
  else()
    set(PROFILE ps_5_0)
  endif()
  set(HEADER "${SIMPSONS_SHADER_DIR}/${ENTRY}.h")
  add_custom_command(OUTPUT "${HEADER}"
    COMMAND "${SIMPSONS_FXC}" /nologo /Ges /Gis /O3 /T ${PROFILE} /E ${ENTRY}
      /Fh "${HEADER}" /Vn "k${ENTRY}" "${CMAKE_SOURCE_DIR}/renderer/rigid_gloss_shader.hlsl"
    DEPENDS renderer/rigid_gloss_shader.hlsl VERBATIM)
  list(APPEND SIMPSONS_RIGID_HEADERS "${HEADER}")
endforeach()
foreach(ENTRY VSRigidDualTextured PSRigidDualTextured GSRigidDualProbe VSRigidDualPixelProbe)
  if(ENTRY MATCHES "^VS")
    set(PROFILE vs_5_0)
  elseif(ENTRY MATCHES "^GS")
    set(PROFILE gs_5_0)
  else()
    set(PROFILE ps_5_0)
  endif()
  set(HEADER "${SIMPSONS_SHADER_DIR}/${ENTRY}.h")
  add_custom_command(OUTPUT "${HEADER}"
    COMMAND "${SIMPSONS_FXC}" /nologo /Ges /Gis /O3 /T ${PROFILE} /E ${ENTRY}
      /Fh "${HEADER}" /Vn "k${ENTRY}" "${CMAKE_SOURCE_DIR}/renderer/rigid_dualtextured_shader.hlsl"
    DEPENDS renderer/rigid_dualtextured_shader.hlsl renderer/rigid_shader.hlsl VERBATIM)
  list(APPEND SIMPSONS_RIGID_HEADERS "${HEADER}")
endforeach()
set(SIMPSONS_RIGID_UV_CHECK "${SIMPSONS_SHADER_DIR}/rigid_dualtextured_uv_verified.stamp")
add_custom_command(OUTPUT "${SIMPSONS_RIGID_UV_CHECK}"
  COMMAND "${Python3_EXECUTABLE}" -B "${CMAKE_SOURCE_DIR}/tools/analyze_rigid_dualtextured_uv_shader.py"
    --verify-hlsl "${CMAKE_SOURCE_DIR}/renderer/rigid_dualtextured_uv_shader.hlsl"
  COMMAND "${CMAKE_COMMAND}" -E touch "${SIMPSONS_RIGID_UV_CHECK}"
  DEPENDS tools/analyze_rigid_dualtextured_uv_shader.py tools/analyze_chocolate_shader.py
    tools/analyze_rigid_shader.py tools/analyze_post_effect_catalog.py tools/analyze_effect_catalog.py
    tools/analyze_screen_shaders.py tools/analyze_edge_shaders.py
    renderer/rigid_dualtextured_uv_shader.hlsl analysis/simpsons.pe VERBATIM)
foreach(ENTRY VSRigidDualTexturedUV PSRigidDualTexturedUV VSRigidDualTexturedUVAlpha PSRigidDualTexturedUVAlpha
    PSRigidDualTexturedUVDraw PSRigidDualTexturedUVAlphaDraw GSRigidDualTexturedUVProbe GSRigidDualTexturedUVAlphaProbe
    VSRigidDualTexturedUVPixelProbe VSRigidDualTexturedUVAlphaPixelProbe)
  if(ENTRY MATCHES "^VS")
    set(PROFILE vs_5_0)
  elseif(ENTRY MATCHES "^GS")
    set(PROFILE gs_5_0)
  else()
    set(PROFILE ps_5_0)
  endif()
  if(ENTRY STREQUAL "PSRigidDualTexturedUVDraw")
    set(SOURCE "${CMAKE_SOURCE_DIR}/renderer/rigid_dualtextured_uv_draw.hlsl")
  elseif(ENTRY STREQUAL "PSRigidDualTexturedUVAlphaDraw")
    set(SOURCE "${CMAKE_SOURCE_DIR}/renderer/rigid_dualtextured_uv_alpha_draw.hlsl")
  elseif(ENTRY MATCHES "Probe$")
    set(SOURCE "${CMAKE_SOURCE_DIR}/renderer/rigid_dualtextured_uv_probe.hlsl")
  else()
    set(SOURCE "${CMAKE_SOURCE_DIR}/renderer/rigid_dualtextured_uv_shader.hlsl")
  endif()
  set(HEADER "${SIMPSONS_SHADER_DIR}/${ENTRY}.h")
  add_custom_command(OUTPUT "${HEADER}"
    COMMAND "${SIMPSONS_FXC}" /nologo /Ges /Gis /WX /O3 /T ${PROFILE} /E ${ENTRY}
      /Fh "${HEADER}" /Vn "k${ENTRY}" "${SOURCE}"
    DEPENDS "${SIMPSONS_RIGID_UV_CHECK}" renderer/rigid_dualtextured_uv_shader.hlsl
      renderer/rigid_dualtextured_uv_draw.hlsl renderer/rigid_dualtextured_uv_alpha_draw.hlsl
      renderer/rigid_dualtextured_uv_probe.hlsl renderer/shadow_mesh.hlsl VERBATIM)
  list(APPEND SIMPSONS_RIGID_HEADERS "${HEADER}")
endforeach()
set(SIMPSONS_RIGID_SINGLE_UV_CHECK "${SIMPSONS_SHADER_DIR}/rigid_uv_verified.stamp")
add_custom_command(OUTPUT "${SIMPSONS_RIGID_SINGLE_UV_CHECK}"
  COMMAND "${Python3_EXECUTABLE}" -B "${CMAKE_SOURCE_DIR}/tools/analyze_rigid_uv_shader.py"
    --verify-hlsl "${CMAKE_SOURCE_DIR}/renderer/rigid_uv_shader.hlsl"
  COMMAND "${CMAKE_COMMAND}" -E touch "${SIMPSONS_RIGID_SINGLE_UV_CHECK}"
  DEPENDS tools/analyze_rigid_uv_shader.py tools/analyze_chocolate_shader.py
    tools/analyze_rigid_shader.py tools/analyze_post_effect_catalog.py tools/analyze_effect_catalog.py
    tools/analyze_screen_shaders.py tools/analyze_edge_shaders.py
    renderer/rigid_uv_shader.hlsl analysis/simpsons.pe VERBATIM)
set(SIMPSONS_RIGID_SINGLE_UV_HEADERS)
foreach(ENTRY VSRigidUV PSRigidUV VSRigidUVAlpha PSRigidUVAlpha
    PSRigidUVDraw PSRigidUVAlphaDraw GSRigidUVProbe GSRigidUVAlphaProbe
    VSRigidUVPixelProbe VSRigidUVAlphaPixelProbe)
  if(ENTRY MATCHES "^VS")
    set(PROFILE vs_5_0)
  elseif(ENTRY MATCHES "^GS")
    set(PROFILE gs_5_0)
  else()
    set(PROFILE ps_5_0)
  endif()
  if(ENTRY STREQUAL "PSRigidUVDraw")
    set(SOURCE "${CMAKE_SOURCE_DIR}/renderer/rigid_uv_draw.hlsl")
  elseif(ENTRY STREQUAL "PSRigidUVAlphaDraw")
    set(SOURCE "${CMAKE_SOURCE_DIR}/renderer/rigid_uv_alpha_draw.hlsl")
  elseif(ENTRY MATCHES "Probe$")
    set(SOURCE "${CMAKE_SOURCE_DIR}/renderer/rigid_uv_probe.hlsl")
  else()
    set(SOURCE "${CMAKE_SOURCE_DIR}/renderer/rigid_uv_shader.hlsl")
  endif()
  set(HEADER "${SIMPSONS_SHADER_DIR}/${ENTRY}.h")
  add_custom_command(OUTPUT "${HEADER}"
    COMMAND "${SIMPSONS_FXC}" /nologo /Ges /Gis /WX /O3 /T ${PROFILE} /E ${ENTRY}
      /Fh "${HEADER}" /Vn "k${ENTRY}" "${SOURCE}"
    DEPENDS "${SIMPSONS_RIGID_SINGLE_UV_CHECK}" renderer/rigid_uv_shader.hlsl
      renderer/rigid_uv_draw.hlsl renderer/rigid_uv_alpha_draw.hlsl
      renderer/rigid_uv_probe.hlsl renderer/shadow_mesh.hlsl VERBATIM)
  list(APPEND SIMPSONS_RIGID_SINGLE_UV_HEADERS "${HEADER}")
  list(APPEND SIMPSONS_RIGID_HEADERS "${HEADER}")
endforeach()
set(SIMPSONS_FLIPBOOK_CHECK "${SIMPSONS_SHADER_DIR}/flipbook_verified.stamp")
add_custom_command(OUTPUT "${SIMPSONS_FLIPBOOK_CHECK}"
  COMMAND "${Python3_EXECUTABLE}" -B "${CMAKE_SOURCE_DIR}/tools/analyze_flipbook_shader.py"
    --verify-hlsl "${CMAKE_SOURCE_DIR}/renderer/flipbook_shader.hlsl"
  COMMAND "${CMAKE_COMMAND}" -E touch "${SIMPSONS_FLIPBOOK_CHECK}"
  DEPENDS tools/analyze_flipbook_shader.py tools/analyze_chocolate_shader.py
    tools/analyze_rigid_shader.py tools/analyze_post_effect_catalog.py tools/analyze_effect_catalog.py
    tools/analyze_screen_shaders.py tools/analyze_edge_shaders.py
    renderer/flipbook_shader.hlsl analysis/simpsons.pe VERBATIM)
set(SIMPSONS_FLIPBOOK_HEADERS)
foreach(ENTRY VSFlipbook PSFlipbook VSFlipbookAlpha PSFlipbookAlpha
    PSFlipbookDraw PSFlipbookAlphaDraw GSFlipbookProbe GSFlipbookAlphaProbe
    VSFlipbookPixelProbe VSFlipbookAlphaPixelProbe)
  if(ENTRY MATCHES "^VS")
    set(PROFILE vs_5_0)
  elseif(ENTRY MATCHES "^GS")
    set(PROFILE gs_5_0)
  else()
    set(PROFILE ps_5_0)
  endif()
  if(ENTRY STREQUAL "PSFlipbookDraw")
    set(SOURCE "${CMAKE_SOURCE_DIR}/renderer/flipbook_draw.hlsl")
  elseif(ENTRY STREQUAL "PSFlipbookAlphaDraw")
    set(SOURCE "${CMAKE_SOURCE_DIR}/renderer/flipbook_alpha_draw.hlsl")
  elseif(ENTRY MATCHES "Probe$")
    set(SOURCE "${CMAKE_SOURCE_DIR}/renderer/flipbook_probe.hlsl")
  else()
    set(SOURCE "${CMAKE_SOURCE_DIR}/renderer/flipbook_shader.hlsl")
  endif()
  set(HEADER "${SIMPSONS_SHADER_DIR}/${ENTRY}.h")
  add_custom_command(OUTPUT "${HEADER}"
    COMMAND "${SIMPSONS_FXC}" /nologo /Ges /Gis /WX /O3 /T ${PROFILE} /E ${ENTRY}
      /Fh "${HEADER}" /Vn "k${ENTRY}" "${SOURCE}"
    DEPENDS "${SIMPSONS_FLIPBOOK_CHECK}" renderer/flipbook_shader.hlsl
      renderer/flipbook_draw.hlsl renderer/flipbook_alpha_draw.hlsl
      renderer/flipbook_probe.hlsl VERBATIM)
  list(APPEND SIMPSONS_FLIPBOOK_HEADERS "${HEADER}")
  list(APPEND SIMPSONS_RIGID_HEADERS "${HEADER}")
endforeach()
foreach(ENTRY VSRigidNormalTangent GSRigidNormalTangentProbe)
  if(ENTRY MATCHES "^VS")
    set(PROFILE vs_5_0)
  else()
    set(PROFILE gs_5_0)
  endif()
  set(HEADER "${SIMPSONS_SHADER_DIR}/${ENTRY}.h")
  add_custom_command(OUTPUT "${HEADER}"
    COMMAND "${SIMPSONS_FXC}" /nologo /Ges /Gis /O3 /T ${PROFILE} /E ${ENTRY}
      /Fh "${HEADER}" /Vn "k${ENTRY}" "${CMAKE_SOURCE_DIR}/renderer/rigid_normalmap_input.hlsl"
    DEPENDS renderer/rigid_normalmap_input.hlsl VERBATIM)
  list(APPEND SIMPSONS_RIGID_HEADERS "${HEADER}")
endforeach()
set(SIMPSONS_NORMALMAP_SOURCE "${SIMPSONS_SHADER_DIR}/rigid_normalmap_shader.hlsl")
add_custom_command(OUTPUT "${SIMPSONS_NORMALMAP_SOURCE}"
  COMMAND "${Python3_EXECUTABLE}" -B "${CMAKE_SOURCE_DIR}/tools/analyze_rigid_normalmap_shader.py"
    --emit-hlsl "${SIMPSONS_NORMALMAP_SOURCE}"
  DEPENDS tools/analyze_rigid_normalmap_shader.py tools/analyze_rigid_shader.py
    tools/analyze_screen_shaders.py tools/analyze_edge_shaders.py analysis/simpsons.pe VERBATIM)
# Legacy VS-only source retained for the existing tangent-layout oracle.
set(SIMPSONS_NORMALMAP_VS_SOURCE "${SIMPSONS_SHADER_DIR}/rigid_normalmap_vs.hlsl")
add_custom_command(OUTPUT "${SIMPSONS_NORMALMAP_VS_SOURCE}"
  COMMAND "${Python3_EXECUTABLE}" -B "${CMAKE_SOURCE_DIR}/tools/analyze_rigid_normalmap_shader.py"
    --emit-vs-hlsl "${SIMPSONS_NORMALMAP_VS_SOURCE}"
  DEPENDS tools/analyze_rigid_normalmap_shader.py tools/analyze_rigid_shader.py
    tools/analyze_screen_shaders.py tools/analyze_edge_shaders.py analysis/simpsons.pe VERBATIM)
foreach(ENTRY VSRigidNormalmap PSRigidNormalmap PSRigidNormalmapDraw GSRigidNormalmapProbe VSRigidNormalmapPixelProbe)
  if(ENTRY MATCHES "^VS")
    set(PROFILE vs_5_0)
  elseif(ENTRY MATCHES "^GS")
    set(PROFILE gs_5_0)
  else()
    set(PROFILE ps_5_0)
  endif()
  if(ENTRY STREQUAL "PSRigidNormalmapDraw")
    set(SOURCE "${CMAKE_SOURCE_DIR}/renderer/rigid_normalmap_draw.hlsl")
  else()
    set(SOURCE "${SIMPSONS_NORMALMAP_SOURCE}")
  endif()
  set(HEADER "${SIMPSONS_SHADER_DIR}/${ENTRY}.h")
  add_custom_command(OUTPUT "${HEADER}"
    COMMAND "${SIMPSONS_FXC}" /nologo /Ges /Gis /WX /O3 /T ${PROFILE} /E ${ENTRY}
      /I "${SIMPSONS_SHADER_DIR}" /Fh "${HEADER}" /Vn "k${ENTRY}" "${SOURCE}"
    DEPENDS "${SIMPSONS_NORMALMAP_SOURCE}" renderer/rigid_normalmap_draw.hlsl renderer/shadow_mesh.hlsl VERBATIM)
  list(APPEND SIMPSONS_RIGID_HEADERS "${HEADER}")
endforeach()
set(SIMPSONS_SKIN_SOURCE "${SIMPSONS_SHADER_DIR}/skin_shader.hlsl")
add_custom_command(OUTPUT "${SIMPSONS_SKIN_SOURCE}"
  COMMAND "${Python3_EXECUTABLE}" -B "${CMAKE_SOURCE_DIR}/tools/analyze_skin_shader.py"
    --emit-hlsl "${SIMPSONS_SKIN_SOURCE}"
  DEPENDS tools/analyze_skin_shader.py tools/skin_shader_emit.py tools/analyze_rigid_shader.py
    tools/analyze_screen_shaders.py tools/analyze_edge_shaders.py analysis/simpsons.pe VERBATIM)
foreach(ENTRY VSSkin PSSkin PSSkinDraw GSSkinProbe VSSkinPixelProbe)
  if(ENTRY MATCHES "^VS")
    set(PROFILE vs_5_0)
  elseif(ENTRY MATCHES "^GS")
    set(PROFILE gs_5_0)
  else()
    set(PROFILE ps_5_0)
  endif()
  if(ENTRY STREQUAL "PSSkinDraw")
    set(SOURCE "${CMAKE_SOURCE_DIR}/renderer/skin_draw.hlsl")
  else()
    set(SOURCE "${SIMPSONS_SKIN_SOURCE}")
  endif()
  set(HEADER "${SIMPSONS_SHADER_DIR}/${ENTRY}.h")
  add_custom_command(OUTPUT "${HEADER}"
    COMMAND "${SIMPSONS_FXC}" /nologo /Ges /Gis /WX /O3 /T ${PROFILE} /E ${ENTRY}
      /I "${SIMPSONS_SHADER_DIR}" /Fh "${HEADER}" /Vn "k${ENTRY}" "${SOURCE}"
    DEPENDS "${SIMPSONS_SKIN_SOURCE}" renderer/skin_draw.hlsl renderer/shadow_mesh.hlsl VERBATIM)
  list(APPEND SIMPSONS_RIGID_HEADERS "${HEADER}")
endforeach()
set(SIMPSONS_SKIN_ALPHA_SOURCE "${SIMPSONS_SHADER_DIR}/skin_alpha_shader.hlsl")
add_custom_command(OUTPUT "${SIMPSONS_SKIN_ALPHA_SOURCE}"
  COMMAND "${Python3_EXECUTABLE}" -B "${CMAKE_SOURCE_DIR}/tools/analyze_skin_alpha_shader.py"
    --emit-hlsl "${SIMPSONS_SKIN_ALPHA_SOURCE}"
  DEPENDS tools/analyze_skin_alpha_shader.py tools/analyze_skin_shader.py tools/skin_shader_emit.py
    tools/analyze_168f8alpha_shader.py tools/analyze_rigid_shader.py tools/analyze_screen_shaders.py
    tools/analyze_edge_shaders.py analysis/simpsons.pe VERBATIM)
foreach(ENTRY PSSkinAlpha PSSkinAlphaDraw)
  if(ENTRY STREQUAL "PSSkinAlphaDraw")
    set(SOURCE "${CMAKE_SOURCE_DIR}/renderer/skin_alpha_draw.hlsl")
  else()
    set(SOURCE "${SIMPSONS_SKIN_ALPHA_SOURCE}")
  endif()
  set(HEADER "${SIMPSONS_SHADER_DIR}/${ENTRY}.h")
  add_custom_command(OUTPUT "${HEADER}"
    COMMAND "${SIMPSONS_FXC}" /nologo /Ges /Gis /WX /O3 /T ps_5_0 /E ${ENTRY}
      /I "${SIMPSONS_SHADER_DIR}" /Fh "${HEADER}" /Vn "k${ENTRY}" "${SOURCE}"
    DEPENDS "${SIMPSONS_SKIN_ALPHA_SOURCE}" renderer/skin_alpha_draw.hlsl renderer/shadow_mesh.hlsl VERBATIM)
  list(APPEND SIMPSONS_RIGID_HEADERS "${HEADER}")
endforeach()
set(SIMPSONS_SKIN_DUAL_SOURCE "${SIMPSONS_SHADER_DIR}/skin_dualtextured_shader.hlsl")
add_custom_command(OUTPUT "${SIMPSONS_SKIN_DUAL_SOURCE}"
  COMMAND "${Python3_EXECUTABLE}" -B "${CMAKE_SOURCE_DIR}/tools/analyze_skin_dualtextured_shader.py"
    --emit-hlsl "${SIMPSONS_SKIN_DUAL_SOURCE}"
  DEPENDS tools/analyze_skin_dualtextured_shader.py tools/skin_shader_emit.py tools/analyze_rigid_shader.py
    tools/analyze_screen_shaders.py tools/analyze_edge_shaders.py analysis/simpsons.pe VERBATIM)
foreach(ENTRY VSSkinDual PSSkinDual PSSkinDualDraw GSSkinDualProbe VSSkinDualPixelProbe)
  if(ENTRY MATCHES "^VS")
    set(PROFILE vs_5_0)
  elseif(ENTRY MATCHES "^GS")
    set(PROFILE gs_5_0)
  else()
    set(PROFILE ps_5_0)
  endif()
  set(HEADER "${SIMPSONS_SHADER_DIR}/${ENTRY}.h")
  if(ENTRY STREQUAL "PSSkinDualDraw")
    set(SOURCE "${CMAKE_SOURCE_DIR}/renderer/skin_dualtextured_draw.hlsl")
  else()
    set(SOURCE "${SIMPSONS_SKIN_DUAL_SOURCE}")
  endif()
  add_custom_command(OUTPUT "${HEADER}"
    COMMAND "${SIMPSONS_FXC}" /nologo /Ges /Gis /WX /O3 /T ${PROFILE} /E ${ENTRY}
      /I "${SIMPSONS_SHADER_DIR}" /Fh "${HEADER}" /Vn "k${ENTRY}" "${SOURCE}"
    DEPENDS "${SIMPSONS_SKIN_DUAL_SOURCE}" renderer/skin_dualtextured_draw.hlsl renderer/shadow_mesh.hlsl VERBATIM)
  list(APPEND SIMPSONS_RIGID_HEADERS "${HEADER}")
endforeach()
set(SIMPSONS_SKIN_DUAL_ALPHA_SOURCE "${SIMPSONS_SHADER_DIR}/skin_dualalpha_shader.hlsl")
add_custom_command(OUTPUT "${SIMPSONS_SKIN_DUAL_ALPHA_SOURCE}"
  COMMAND "${Python3_EXECUTABLE}" -B "${CMAKE_SOURCE_DIR}/tools/analyze_skin_dualalpha_shader.py"
    --emit-hlsl "${SIMPSONS_SKIN_DUAL_ALPHA_SOURCE}"
  DEPENDS tools/analyze_skin_dualalpha_shader.py tools/analyze_skin_dualtextured_shader.py
    tools/analyze_skin_alpha_shader.py tools/skin_shader_emit.py tools/analyze_rigid_shader.py
    tools/analyze_screen_shaders.py tools/analyze_edge_shaders.py analysis/simpsons.pe VERBATIM)
foreach(ENTRY VSSkinDualAlpha PSSkinDualAlpha PSSkinDualAlphaDraw GSSkinDualAlphaProbe VSSkinDualAlphaPixelProbe)
  if(ENTRY MATCHES "^VS")
    set(PROFILE vs_5_0)
  elseif(ENTRY MATCHES "^GS")
    set(PROFILE gs_5_0)
  else()
    set(PROFILE ps_5_0)
  endif()
  if(ENTRY STREQUAL "PSSkinDualAlphaDraw")
    set(SOURCE "${CMAKE_SOURCE_DIR}/renderer/skin_dualalpha_draw.hlsl")
  else()
    set(SOURCE "${SIMPSONS_SKIN_DUAL_ALPHA_SOURCE}")
  endif()
  set(HEADER "${SIMPSONS_SHADER_DIR}/${ENTRY}.h")
  add_custom_command(OUTPUT "${HEADER}"
    COMMAND "${SIMPSONS_FXC}" /nologo /Ges /Gis /WX /O3 /T ${PROFILE} /E ${ENTRY}
      /I "${SIMPSONS_SHADER_DIR}" /Fh "${HEADER}" /Vn "k${ENTRY}" "${SOURCE}"
    DEPENDS "${SIMPSONS_SKIN_DUAL_ALPHA_SOURCE}" renderer/skin_dualalpha_draw.hlsl renderer/shadow_mesh.hlsl VERBATIM)
  list(APPEND SIMPSONS_SKIN_DUAL_ALPHA_HEADERS "${HEADER}")
  list(APPEND SIMPSONS_RIGID_HEADERS "${HEADER}")
endforeach()
set_property(SOURCE renderer/skin_mesh.cpp renderer/screen_pipeline.cpp APPEND PROPERTY OBJECT_DEPENDS
  "${SIMPSONS_SKIN_DUAL_ALPHA_HEADERS}")
set(SIMPSONS_SKIN_TEXTURED_SOURCE "${SIMPSONS_SHADER_DIR}/skin_textured_shader.hlsl")
add_custom_command(OUTPUT "${SIMPSONS_SKIN_TEXTURED_SOURCE}"
  COMMAND "${Python3_EXECUTABLE}" -B "${CMAKE_SOURCE_DIR}/tools/analyze_skin_textured_shader.py"
    --emit-hlsl "${SIMPSONS_SKIN_TEXTURED_SOURCE}"
  DEPENDS tools/analyze_skin_textured_shader.py tools/analyze_skin_shader.py tools/skin_shader_emit.py
    tools/analyze_skin_dualtextured_shader.py tools/analyze_skin_alpha_shader.py
    tools/analyze_168f8alpha_shader.py
    tools/analyze_rigid_shader.py tools/analyze_screen_shaders.py tools/analyze_edge_shaders.py
    analysis/simpsons.pe VERBATIM)
foreach(ENTRY VSSkinTextured PSSkinTextured VSSkinTexturedAlpha PSSkinTexturedAlpha
    GSSkinTexturedProbe GSSkinTexturedAlphaProbe VSSkinTexturedPixelProbe VSSkinTexturedAlphaPixelProbe
    PSSkinTexturedDraw PSSkinTexturedAlphaDraw)
  if(ENTRY MATCHES "^VS")
    set(PROFILE vs_5_0)
  elseif(ENTRY MATCHES "^GS")
    set(PROFILE gs_5_0)
  else()
    set(PROFILE ps_5_0)
  endif()
  if(ENTRY MATCHES "Draw$")
    set(SOURCE "${CMAKE_SOURCE_DIR}/renderer/skin_textured_draw.hlsl")
  else()
    set(SOURCE "${SIMPSONS_SKIN_TEXTURED_SOURCE}")
  endif()
  set(HEADER "${SIMPSONS_SHADER_DIR}/${ENTRY}.h")
  add_custom_command(OUTPUT "${HEADER}"
    COMMAND "${SIMPSONS_FXC}" /nologo /Ges /Gis /WX /O3 /T ${PROFILE} /E ${ENTRY}
      /I "${SIMPSONS_SHADER_DIR}" /Fh "${HEADER}" /Vn "k${ENTRY}" "${SOURCE}"
    DEPENDS "${SIMPSONS_SKIN_TEXTURED_SOURCE}" renderer/skin_textured_draw.hlsl renderer/shadow_mesh.hlsl VERBATIM)
  list(APPEND SIMPSONS_SKIN_TEXTURED_HEADERS "${HEADER}")
  list(APPEND SIMPSONS_RIGID_HEADERS "${HEADER}")
endforeach()
set_property(SOURCE renderer/skin_mesh.cpp renderer/screen_pipeline.cpp APPEND PROPERTY OBJECT_DEPENDS
  "${SIMPSONS_SKIN_TEXTURED_HEADERS}")
set(SIMPSONS_SKY_SOURCE "${SIMPSONS_SHADER_DIR}/sky_shader.hlsl")
add_custom_command(OUTPUT "${SIMPSONS_SKY_SOURCE}"
  COMMAND "${Python3_EXECUTABLE}" -B "${CMAKE_SOURCE_DIR}/tools/analyze_sky_shader.py"
    --emit-hlsl "${SIMPSONS_SKY_SOURCE}"
  DEPENDS tools/analyze_sky_shader.py tools/analyze_rigid_shader.py
    tools/analyze_screen_shaders.py tools/analyze_edge_shaders.py analysis/simpsons.pe VERBATIM)
foreach(ENTRY VSSky PSSky VSSkyOpaque PSSkyOpaque)
  if(ENTRY MATCHES "^VS")
    set(PROFILE vs_5_0)
  else()
    set(PROFILE ps_5_0)
  endif()
  set(HEADER "${SIMPSONS_SHADER_DIR}/${ENTRY}.h")
  add_custom_command(OUTPUT "${HEADER}"
    COMMAND "${SIMPSONS_FXC}" /nologo /Ges /Gis /WX /O3 /T ${PROFILE} /E ${ENTRY}
      /I "${SIMPSONS_SHADER_DIR}" /Fh "${HEADER}" /Vn "k${ENTRY}" "${SIMPSONS_SKY_SOURCE}"
    DEPENDS "${SIMPSONS_SKY_SOURCE}" VERBATIM)
  list(APPEND SIMPSONS_RIGID_HEADERS "${HEADER}")
endforeach()
set(SIMPSONS_SKY_DRAW_HEADER "${SIMPSONS_SHADER_DIR}/PSSkyDraw.h")
foreach(ENTRY PSSkyDraw PSSkyOpaqueDraw)
  set(HEADER "${SIMPSONS_SHADER_DIR}/${ENTRY}.h")
  add_custom_command(OUTPUT "${HEADER}"
    COMMAND "${SIMPSONS_FXC}" /nologo /Ges /Gis /WX /O3 /T ps_5_0 /E ${ENTRY}
      /I "${SIMPSONS_SHADER_DIR}" /Fh "${HEADER}" /Vn k${ENTRY}
      "${CMAKE_SOURCE_DIR}/renderer/sky_draw.hlsl"
    DEPENDS "${SIMPSONS_SKY_SOURCE}" renderer/sky_draw.hlsl renderer/shadow_mesh.hlsl VERBATIM)
  list(APPEND SIMPSONS_RIGID_HEADERS "${HEADER}")
endforeach()
# Use the custom commands' exact paths for these generated headers. The
# Windows compiler depfile may spell them relatively, creating separate Ninja
# nodes and otherwise delaying shader consumer rebuilds until a second build.
set_property(SOURCE renderer/sky_mesh.cpp APPEND PROPERTY OBJECT_DEPENDS
  "${SIMPSONS_SKY_DRAW_HEADER};${SIMPSONS_SHADER_DIR}/VSSky.h;${SIMPSONS_SHADER_DIR}/VSSkyOpaque.h;${SIMPSONS_SHADER_DIR}/PSSkyOpaqueDraw.h")
set_property(SOURCE renderer/screen_pipeline.cpp APPEND PROPERTY OBJECT_DEPENDS
  "${SIMPSONS_SHADER_DIR}/VSSky.h;${SIMPSONS_SHADER_DIR}/PSSky.h;${SIMPSONS_SHADER_DIR}/VSSkyOpaque.h;${SIMPSONS_SHADER_DIR}/PSSkyOpaque.h")
set(SIMPSONS_CHOCOLATE_SOURCE "${SIMPSONS_SHADER_DIR}/chocolate_shader.hlsl")
add_custom_command(OUTPUT "${SIMPSONS_CHOCOLATE_SOURCE}"
  COMMAND "${Python3_EXECUTABLE}" -B "${CMAKE_SOURCE_DIR}/tools/analyze_chocolate_shader.py"
    --emit-hlsl "${SIMPSONS_CHOCOLATE_SOURCE}"
  DEPENDS tools/analyze_chocolate_shader.py tools/analyze_rigid_shader.py
    tools/analyze_screen_shaders.py tools/analyze_edge_shaders.py analysis/simpsons.pe VERBATIM)
foreach(ENTRY VSChocolate PSChocolate)
  if(ENTRY MATCHES "^VS")
    set(PROFILE vs_5_0)
  else()
    set(PROFILE ps_5_0)
  endif()
  set(HEADER "${SIMPSONS_SHADER_DIR}/${ENTRY}.h")
  add_custom_command(OUTPUT "${HEADER}"
    COMMAND "${SIMPSONS_FXC}" /nologo /Ges /Gis /WX /O3 /T ${PROFILE} /E ${ENTRY}
      /I "${SIMPSONS_SHADER_DIR}" /Fh "${HEADER}" /Vn "k${ENTRY}" "${SIMPSONS_CHOCOLATE_SOURCE}"
    DEPENDS "${SIMPSONS_CHOCOLATE_SOURCE}" VERBATIM)
  list(APPEND SIMPSONS_RIGID_HEADERS "${HEADER}")
endforeach()
set(SIMPSONS_A168_SOURCE "${SIMPSONS_SHADER_DIR}/a168_shader.hlsl")
add_custom_command(OUTPUT "${SIMPSONS_A168_SOURCE}"
  COMMAND "${Python3_EXECUTABLE}" -B "${CMAKE_SOURCE_DIR}/tools/analyze_168f8alpha_shader.py"
    --emit-hlsl "${SIMPSONS_A168_SOURCE}"
  DEPENDS tools/analyze_168f8alpha_shader.py tools/analyze_rigid_shader.py
    tools/analyze_screen_shaders.py tools/analyze_edge_shaders.py analysis/simpsons.pe VERBATIM)
foreach(ENTRY VS168F8 PS168F8)
  if(ENTRY MATCHES "^VS")
    set(PROFILE vs_5_0)
  else()
    set(PROFILE ps_5_0)
  endif()
  set(HEADER "${SIMPSONS_SHADER_DIR}/${ENTRY}.h")
  add_custom_command(OUTPUT "${HEADER}"
    COMMAND "${SIMPSONS_FXC}" /nologo /Ges /Gis /WX /O3 /T ${PROFILE} /E ${ENTRY}
      /I "${SIMPSONS_SHADER_DIR}" /Fh "${HEADER}" /Vn "k${ENTRY}" "${SIMPSONS_A168_SOURCE}"
    DEPENDS "${SIMPSONS_A168_SOURCE}" VERBATIM)
  list(APPEND SIMPSONS_RIGID_HEADERS "${HEADER}")
endforeach()
set(SIMPSONS_RIGID_ALPHA_SOURCE "${SIMPSONS_SHADER_DIR}/rigid_alpha_shader.hlsl")
add_custom_command(OUTPUT "${SIMPSONS_RIGID_ALPHA_SOURCE}"
  COMMAND "${Python3_EXECUTABLE}" -B "${CMAKE_SOURCE_DIR}/tools/analyze_rigid_alpha_shader.py"
    --emit-hlsl "${SIMPSONS_RIGID_ALPHA_SOURCE}"
  DEPENDS tools/analyze_rigid_alpha_shader.py tools/analyze_168f8alpha_shader.py
    tools/analyze_rigid_shader.py tools/analyze_post_effect_catalog.py tools/analyze_effect_catalog.py
    tools/analyze_screen_shaders.py tools/analyze_edge_shaders.py analysis/simpsons.pe VERBATIM)
set(SIMPSONS_RIGID_ALPHA_HEADERS)
foreach(ENTRY VSRigidAlpha PSRigidAlpha GSRigidAlphaProbe VSRigidAlphaPixelProbe)
  if(ENTRY MATCHES "^VS")
    set(PROFILE vs_5_0)
  elseif(ENTRY MATCHES "^GS")
    set(PROFILE gs_5_0)
  else()
    set(PROFILE ps_5_0)
  endif()
  set(HEADER "${SIMPSONS_SHADER_DIR}/${ENTRY}.h")
  add_custom_command(OUTPUT "${HEADER}"
    COMMAND "${SIMPSONS_FXC}" /nologo /Ges /Gis /WX /O3 /T ${PROFILE} /E ${ENTRY}
      /I "${SIMPSONS_SHADER_DIR}" /Fh "${HEADER}" /Vn "k${ENTRY}"
      "${CMAKE_SOURCE_DIR}/renderer/rigid_alpha_probe.hlsl"
    DEPENDS "${SIMPSONS_RIGID_ALPHA_SOURCE}" renderer/rigid_alpha_probe.hlsl VERBATIM)
  list(APPEND SIMPSONS_RIGID_ALPHA_HEADERS "${HEADER}")
endforeach()
set(SIMPSONS_RIGID_ALPHA_DRAW_HEADER "${SIMPSONS_SHADER_DIR}/PSRigidAlphaDraw.h")
add_custom_command(OUTPUT "${SIMPSONS_RIGID_ALPHA_DRAW_HEADER}"
  COMMAND "${SIMPSONS_FXC}" /nologo /Ges /Gis /WX /O3 /T ps_5_0 /E PSRigidAlphaDraw
    /I "${SIMPSONS_SHADER_DIR}" /Fh "${SIMPSONS_RIGID_ALPHA_DRAW_HEADER}" /Vn kPSRigidAlphaDraw
    "${CMAKE_SOURCE_DIR}/renderer/rigid_alpha_draw.hlsl"
  DEPENDS "${SIMPSONS_RIGID_ALPHA_SOURCE}" renderer/rigid_alpha_draw.hlsl renderer/shadow_mesh.hlsl VERBATIM)
list(APPEND SIMPSONS_RIGID_HEADERS ${SIMPSONS_RIGID_ALPHA_HEADERS} "${SIMPSONS_RIGID_ALPHA_DRAW_HEADER}")
set(SIMPSONS_RIGID_FAMILY_ALPHA_SOURCE "${SIMPSONS_SHADER_DIR}/rigid_family_alpha_shader.hlsl")
set(SIMPSONS_RIGID_FAMILY_ALPHA_REFERENCES
  "K:/Simpsons/RexGlueCurrent/include/rex/graphics/format/ucode.h"
  "K:/Simpsons/RexGlueCurrent/include/rex/graphics/xenos.h")
add_custom_command(OUTPUT "${SIMPSONS_RIGID_FAMILY_ALPHA_SOURCE}"
  COMMAND "${Python3_EXECUTABLE}" -B "${CMAKE_SOURCE_DIR}/tools/analyze_rigid_family_alpha_shader.py"
    --verify --emit-hlsl "${SIMPSONS_RIGID_FAMILY_ALPHA_SOURCE}"
  DEPENDS tools/analyze_rigid_family_alpha_shader.py tools/analyze_rigid_shader.py tools/analyze_168f8alpha_shader.py
    tools/analyze_rigid_dualtextured_uv_shader.py tools/analyze_chocolate_shader.py
    tools/analyze_edge_shaders.py tools/analyze_screen_shaders.py tools/analyze_post_effect_catalog.py tools/analyze_effect_catalog.py
    tools/analyze_fourtap_shaders.py ${SIMPSONS_RIGID_FAMILY_ALPHA_REFERENCES}
    analysis/simpsons.pe VERBATIM)
set(SIMPSONS_RIGID_FAMILY_ALPHA_HEADERS)
foreach(ENTRY VSRigidFamilyAlpha PSRigidGlossAlpha PSRigidMultitoneAlpha PSRigidNormalmapAlpha
    GSRigidFamilyAlphaProbe VSRigidFamilyAlphaPixelProbe PSRigidGlossAlphaDraw PSRigidMultitoneAlphaDraw PSRigidNormalmapAlphaDraw)
  if(ENTRY MATCHES "^VS")
    set(PROFILE vs_5_0)
  elseif(ENTRY MATCHES "^GS")
    set(PROFILE gs_5_0)
  else()
    set(PROFILE ps_5_0)
  endif()
  if(ENTRY MATCHES "Probe$")
    set(SOURCE "${CMAKE_SOURCE_DIR}/renderer/rigid_family_alpha_probe.hlsl")
  elseif(ENTRY MATCHES "Draw$")
    set(SOURCE "${CMAKE_SOURCE_DIR}/renderer/rigid_family_alpha_draw.hlsl")
  else()
    set(SOURCE "${SIMPSONS_RIGID_FAMILY_ALPHA_SOURCE}")
  endif()
  set(HEADER "${SIMPSONS_SHADER_DIR}/${ENTRY}.h")
  add_custom_command(OUTPUT "${HEADER}"
    COMMAND "${SIMPSONS_FXC}" /nologo /Ges /Gis /WX /O3 /T ${PROFILE} /E ${ENTRY}
      /I "${SIMPSONS_SHADER_DIR}" /Fh "${HEADER}" /Vn "k${ENTRY}" "${SOURCE}"
    DEPENDS "${SIMPSONS_RIGID_FAMILY_ALPHA_SOURCE}" renderer/rigid_family_alpha_probe.hlsl
      renderer/rigid_family_alpha_draw.hlsl renderer/shadow_mesh.hlsl VERBATIM)
  list(APPEND SIMPSONS_RIGID_FAMILY_ALPHA_HEADERS "${HEADER}")
endforeach()
list(APPEND SIMPSONS_RIGID_HEADERS ${SIMPSONS_RIGID_FAMILY_ALPHA_HEADERS})
set(SIMPSONS_VFX_CHECK "${SIMPSONS_SHADER_DIR}/vfx_rigid_verified.stamp")
add_custom_command(OUTPUT "${SIMPSONS_VFX_CHECK}"
  COMMAND "${Python3_EXECUTABLE}" -B "${CMAKE_SOURCE_DIR}/tools/analyze_vfx_rigid_shader.py"
    --verify-hlsl "${CMAKE_SOURCE_DIR}/renderer/vfx_rigid_shader.hlsl"
  COMMAND "${CMAKE_COMMAND}" -E touch "${SIMPSONS_VFX_CHECK}"
  DEPENDS tools/analyze_vfx_rigid_shader.py tools/analyze_rigid_shader.py tools/analyze_168f8alpha_shader.py
    tools/analyze_screen_shaders.py tools/analyze_edge_shaders.py renderer/vfx_rigid_shader.hlsl
    "${CMAKE_SOURCE_DIR}/analysis/simpsons.pe" VERBATIM)
foreach(ENTRY VSVfxRigid PSVfxRigid PSVfxRigidDraw)
  if(ENTRY STREQUAL "VSVfxRigid")
    set(PROFILE vs_5_0)
  else()
    set(PROFILE ps_5_0)
  endif()
  set(HEADER "${SIMPSONS_SHADER_DIR}/${ENTRY}.h")
  add_custom_command(OUTPUT "${HEADER}"
    COMMAND "${SIMPSONS_FXC}" /nologo /Ges /Gis /WX /O3 /T ${PROFILE} /E ${ENTRY}
      /Fh "${HEADER}" /Vn k${ENTRY} "${CMAKE_SOURCE_DIR}/renderer/vfx_rigid_draw.hlsl"
    DEPENDS "${SIMPSONS_VFX_CHECK}" renderer/vfx_rigid_shader.hlsl renderer/vfx_rigid_draw.hlsl renderer/shadow_mesh.hlsl VERBATIM)
  list(APPEND SIMPSONS_RIGID_HEADERS "${HEADER}")
endforeach()
set(SIMPSONS_CHOCOLATE_UV_PROBE_HEADER "${SIMPSONS_SHADER_DIR}/GSChocolateUVProbe.h")
add_custom_command(OUTPUT "${SIMPSONS_CHOCOLATE_UV_PROBE_HEADER}"
  COMMAND "${SIMPSONS_FXC}" /nologo /Ges /Gis /WX /O3 /T gs_5_0 /E GSChocolateUVProbe
    /I "${SIMPSONS_SHADER_DIR}" /Fh "${SIMPSONS_CHOCOLATE_UV_PROBE_HEADER}" /Vn kGSChocolateUVProbe
    "${CMAKE_SOURCE_DIR}/tests/chocolate_uv_probe.hlsl"
  DEPENDS tests/chocolate_uv_probe.hlsl "${SIMPSONS_CHOCOLATE_SOURCE}" VERBATIM)
set(SIMPSONS_RIGID_TEXTURED_DRAW_HEADER "${SIMPSONS_SHADER_DIR}/PSRigidTexturedDraw.h")
add_custom_command(OUTPUT "${SIMPSONS_RIGID_TEXTURED_DRAW_HEADER}"
  COMMAND "${SIMPSONS_FXC}" /nologo /Ges /Gis /O3 /T ps_5_0 /E PSRigidTexturedDraw
    /Fh "${SIMPSONS_RIGID_TEXTURED_DRAW_HEADER}" /Vn kPSRigidTexturedDraw "${CMAKE_SOURCE_DIR}/renderer/rigid_textured_draw.hlsl"
  DEPENDS renderer/rigid_textured_draw.hlsl renderer/rigid_textured_shader.hlsl renderer/rigid_shader.hlsl renderer/shadow_mesh.hlsl VERBATIM)
set(SIMPSONS_A168_DRAW_HEADER "${SIMPSONS_SHADER_DIR}/PS168F8Draw.h")
add_custom_command(OUTPUT "${SIMPSONS_A168_DRAW_HEADER}"
  COMMAND "${SIMPSONS_FXC}" /nologo /Ges /Gis /O3 /T ps_5_0 /E PS168F8Draw
    /I "${SIMPSONS_SHADER_DIR}" /Fh "${SIMPSONS_A168_DRAW_HEADER}" /Vn kPS168F8Draw "${CMAKE_SOURCE_DIR}/renderer/a168_draw.hlsl"
  DEPENDS renderer/a168_draw.hlsl "${SIMPSONS_A168_SOURCE}" renderer/shadow_mesh.hlsl VERBATIM)
list(APPEND SIMPSONS_RIGID_HEADERS "${SIMPSONS_A168_DRAW_HEADER}")
set(SIMPSONS_CHOCOLATE_DRAW_HEADER "${SIMPSONS_SHADER_DIR}/PSChocolateDraw.h")
add_custom_command(OUTPUT "${SIMPSONS_CHOCOLATE_DRAW_HEADER}"
  COMMAND "${SIMPSONS_FXC}" /nologo /Ges /Gis /O3 /T ps_5_0 /E PSChocolateDraw
    /I "${SIMPSONS_SHADER_DIR}" /Fh "${SIMPSONS_CHOCOLATE_DRAW_HEADER}" /Vn kPSChocolateDraw "${CMAKE_SOURCE_DIR}/renderer/chocolate_draw.hlsl"
  DEPENDS renderer/chocolate_draw.hlsl "${SIMPSONS_CHOCOLATE_SOURCE}" renderer/shadow_mesh.hlsl VERBATIM)
list(APPEND SIMPSONS_RIGID_HEADERS "${SIMPSONS_CHOCOLATE_DRAW_HEADER}")
set(SIMPSONS_RIGID_DRAW_HEADER "${SIMPSONS_SHADER_DIR}/PSRigidDraw.h")
add_custom_command(OUTPUT "${SIMPSONS_RIGID_DRAW_HEADER}"
  COMMAND "${SIMPSONS_FXC}" /nologo /Ges /Gis /O3 /T ps_5_0 /E PSRigidDraw
    /Fh "${SIMPSONS_RIGID_DRAW_HEADER}" /Vn kPSRigidDraw "${CMAKE_SOURCE_DIR}/renderer/rigid_draw.hlsl"
  DEPENDS renderer/rigid_draw.hlsl renderer/rigid_shader.hlsl renderer/shadow_mesh.hlsl VERBATIM)
set(SIMPSONS_RIGID_DUAL_DRAW_HEADER "${SIMPSONS_SHADER_DIR}/PSRigidDualTexturedDraw.h")
add_custom_command(OUTPUT "${SIMPSONS_RIGID_DUAL_DRAW_HEADER}"
  COMMAND "${SIMPSONS_FXC}" /nologo /Ges /Gis /O3 /T ps_5_0 /E PSRigidDualTexturedDraw
    /Fh "${SIMPSONS_RIGID_DUAL_DRAW_HEADER}" /Vn kPSRigidDualTexturedDraw "${CMAKE_SOURCE_DIR}/renderer/rigid_dualtextured_draw.hlsl"
  DEPENDS renderer/rigid_dualtextured_draw.hlsl renderer/rigid_dualtextured_shader.hlsl renderer/rigid_shader.hlsl renderer/shadow_mesh.hlsl VERBATIM)
set(SIMPSONS_RIGID_GLOSS_DRAW_HEADER "${SIMPSONS_SHADER_DIR}/PSRigidGlossDraw.h")
add_custom_command(OUTPUT "${SIMPSONS_RIGID_GLOSS_DRAW_HEADER}"
  COMMAND "${SIMPSONS_FXC}" /nologo /Ges /Gis /O3 /T ps_5_0 /E PSRigidGlossDraw
    /Fh "${SIMPSONS_RIGID_GLOSS_DRAW_HEADER}" /Vn kPSRigidGlossDraw "${CMAKE_SOURCE_DIR}/renderer/rigid_gloss_draw.hlsl"
  DEPENDS renderer/rigid_gloss_draw.hlsl renderer/rigid_gloss_shader.hlsl renderer/rigid_shader.hlsl renderer/shadow_mesh.hlsl VERBATIM)
add_custom_command(OUTPUT "${SIMPSONS_SHADOW_MESH_HEADER}"
  COMMAND "${SIMPSONS_FXC}" /nologo /Ges /Gis /O3 /T ps_5_0 /E PSShadowMeshDepth
    /Fh "${SIMPSONS_SHADOW_MESH_HEADER}" /Vn kPSShadowMeshDepth "${CMAKE_SOURCE_DIR}/renderer/shadow_mesh.hlsl"
  DEPENDS renderer/shadow_mesh.hlsl VERBATIM)
foreach(ENTRY VSIm2D PSIm2DFlat PSIm2DTextured GSIm2DProbe)
  if(ENTRY MATCHES "^VS")
    set(PROFILE vs_5_0)
  elseif(ENTRY MATCHES "^GS")
    set(PROFILE gs_5_0)
  else()
    set(PROFILE ps_5_0)
  endif()
  set(HEADER "${SIMPSONS_SHADER_DIR}/${ENTRY}.h")
  add_custom_command(OUTPUT "${HEADER}"
    COMMAND "${SIMPSONS_FXC}" /nologo /Ges /Gis /O3 /T ${PROFILE} /E ${ENTRY}
      /Fh "${HEADER}" /Vn "k${ENTRY}" "${CMAKE_SOURCE_DIR}/renderer/im2d.hlsl"
    DEPENDS renderer/im2d.hlsl VERBATIM)
  list(APPEND SIMPSONS_IM2D_HEADERS "${HEADER}")
endforeach()
set(SIMPSONS_IM2D_DRAW_HEADERS)
foreach(ENTRY VSIm2DDraw PSIm2DDrawFlat PSIm2DDrawTextured PSIm2DDepthFlat PSIm2DDepthTextured)
  if(ENTRY MATCHES "^VS")
    set(PROFILE vs_5_0)
  else()
    set(PROFILE ps_5_0)
  endif()
  set(HEADER "${SIMPSONS_SHADER_DIR}/${ENTRY}.h")
  add_custom_command(OUTPUT "${HEADER}"
    COMMAND "${SIMPSONS_FXC}" /nologo /Ges /Gis /O3 /T ${PROFILE} /E ${ENTRY}
      /Fh "${HEADER}" /Vn "k${ENTRY}" "${CMAKE_SOURCE_DIR}/renderer/im2d_draw.hlsl"
    DEPENDS renderer/im2d_draw.hlsl renderer/im2d.hlsl VERBATIM)
  list(APPEND SIMPSONS_IM2D_DRAW_HEADERS "${HEADER}")
endforeach()
set(SIMPSONS_PARTICLE_SOURCE "${SIMPSONS_SHADER_DIR}/particle_shader.hlsl")
add_custom_command(OUTPUT "${SIMPSONS_PARTICLE_SOURCE}"
  COMMAND "${Python3_EXECUTABLE}" -B "${CMAKE_SOURCE_DIR}/tools/analyze_particle_shader.py"
    --emit-hlsl "${SIMPSONS_PARTICLE_SOURCE}"
  DEPENDS tools/analyze_particle_shader.py tools/analyze_rigid_shader.py tools/analyze_screen_shaders.py
    tools/analyze_edge_shaders.py analysis/simpsons.pe VERBATIM)
set(SIMPSONS_PROJECTED_PARTICLE_SOURCE "${SIMPSONS_SHADER_DIR}/particle_projected_shader.hlsl")
add_custom_command(OUTPUT "${SIMPSONS_PROJECTED_PARTICLE_SOURCE}"
  COMMAND "${Python3_EXECUTABLE}" -B "${CMAKE_SOURCE_DIR}/tools/analyze_projected_particle_shader.py"
    --emit-hlsl "${SIMPSONS_PROJECTED_PARTICLE_SOURCE}"
  DEPENDS tools/analyze_projected_particle_shader.py tools/analyze_particle_shader.py tools/analyze_rigid_shader.py
    tools/analyze_screen_shaders.py tools/analyze_edge_shaders.py tools/analyze_fourtap_shaders.py analysis/simpsons.pe VERBATIM)
set(SIMPSONS_DUAL_PARTICLE_SOURCE "${SIMPSONS_SHADER_DIR}/particle_dual_shader.hlsl")
add_custom_command(OUTPUT "${SIMPSONS_DUAL_PARTICLE_SOURCE}"
  COMMAND "${Python3_EXECUTABLE}" -B "${CMAKE_SOURCE_DIR}/tools/analyze_dual_particle_shader.py"
    --emit-hlsl "${SIMPSONS_DUAL_PARTICLE_SOURCE}"
  DEPENDS tools/analyze_dual_particle_shader.py tools/analyze_projected_particle_shader.py tools/analyze_particle_shader.py
    tools/analyze_rigid_shader.py tools/analyze_screen_shaders.py tools/analyze_edge_shaders.py analysis/simpsons.pe VERBATIM)
set(SIMPSONS_TYPE5_PARTICLE_SOURCE "${SIMPSONS_SHADER_DIR}/particle_type5_shader.hlsl")
add_custom_command(OUTPUT "${SIMPSONS_TYPE5_PARTICLE_SOURCE}"
  COMMAND "${Python3_EXECUTABLE}" -B "${CMAKE_SOURCE_DIR}/tools/analyze_particle_type5_shader.py"
    --emit-hlsl "${SIMPSONS_TYPE5_PARTICLE_SOURCE}"
  DEPENDS tools/analyze_particle_type5_shader.py tools/analyze_particle_shader.py
    tools/analyze_rigid_shader.py tools/analyze_screen_shaders.py tools/analyze_edge_shaders.py
    "${SIMPSONS_PARTICLE_SOURCE}" analysis/simpsons.pe VERBATIM)
set(SIMPSONS_DUAL_PROJECTED_PARTICLE_SOURCE "${SIMPSONS_SHADER_DIR}/particle_dual_projected_shader.hlsl")
add_custom_command(OUTPUT "${SIMPSONS_DUAL_PROJECTED_PARTICLE_SOURCE}"
  COMMAND "${Python3_EXECUTABLE}" -B "${CMAKE_SOURCE_DIR}/tools/analyze_dual_projected_particle_shader.py"
    --emit-hlsl "${SIMPSONS_DUAL_PROJECTED_PARTICLE_SOURCE}"
  DEPENDS tools/analyze_dual_projected_particle_shader.py tools/analyze_dual_particle_shader.py
    tools/analyze_projected_particle_shader.py tools/analyze_particle_shader.py tools/analyze_rigid_shader.py
    tools/analyze_screen_shaders.py tools/analyze_edge_shaders.py tools/analyze_fourtap_shaders.py analysis/simpsons.pe VERBATIM)
set(SIMPSONS_PARTICLE_HEADERS)
foreach(ENTRY VSParticle VSParticleType5 PSParticle PSParticleDraw GSParticleProbe PSParticleProjected PSParticleProjectedDraw VSParticleProjectedProbe PSParticleDual PSParticleDualDraw PSParticleDualProjected PSParticleDualProjectedDraw)
  if(ENTRY MATCHES "^VS")
    set(PROFILE vs_5_0)
  elseif(ENTRY MATCHES "^GS")
    set(PROFILE gs_5_0)
  else()
    set(PROFILE ps_5_0)
  endif()
  if(ENTRY MATCHES "^PSParticle.*Draw$")
    set(SOURCE "${CMAKE_SOURCE_DIR}/renderer/particle_draw.hlsl")
  elseif(ENTRY MATCHES "Type5")
    set(SOURCE "${SIMPSONS_TYPE5_PARTICLE_SOURCE}")
  elseif(ENTRY MATCHES "DualProjected")
    set(SOURCE "${SIMPSONS_DUAL_PROJECTED_PARTICLE_SOURCE}")
  elseif(ENTRY MATCHES "Dual")
    set(SOURCE "${SIMPSONS_DUAL_PARTICLE_SOURCE}")
  elseif(ENTRY MATCHES "Projected")
    set(SOURCE "${SIMPSONS_PROJECTED_PARTICLE_SOURCE}")
  else()
    set(SOURCE "${SIMPSONS_PARTICLE_SOURCE}")
  endif()
  set(HEADER "${SIMPSONS_SHADER_DIR}/${ENTRY}.h")
  add_custom_command(OUTPUT "${HEADER}"
    COMMAND "${SIMPSONS_FXC}" /nologo /Ges /Gis /O3 /T ${PROFILE} /E ${ENTRY}
      /I "${SIMPSONS_SHADER_DIR}" /Fh "${HEADER}" /Vn "k${ENTRY}" "${SOURCE}"
    DEPENDS "${SIMPSONS_PARTICLE_SOURCE}" "${SIMPSONS_PROJECTED_PARTICLE_SOURCE}" "${SIMPSONS_DUAL_PARTICLE_SOURCE}"
      "${SIMPSONS_TYPE5_PARTICLE_SOURCE}" "${SIMPSONS_DUAL_PROJECTED_PARTICLE_SOURCE}" renderer/particle_draw.hlsl renderer/shadow_mesh.hlsl VERBATIM)
  list(APPEND SIMPSONS_PARTICLE_HEADERS "${HEADER}")
endforeach()
set(SIMPSONS_CORONA_HEADERS)
foreach(ENTRY VSCoronaQuery PSCoronaQuery PSCoronaQueryPacked PSCoronaSprite PSCoronaSpritePacked)
  if(ENTRY MATCHES "^VS")
    set(PROFILE vs_5_0)
  else()
    set(PROFILE ps_5_0)
  endif()
  set(HEADER "${SIMPSONS_SHADER_DIR}/${ENTRY}.h")
  add_custom_command(OUTPUT "${HEADER}"
    COMMAND "${Python3_EXECUTABLE}" -B "${CMAKE_SOURCE_DIR}/tools/analyze_corona_shaders.py"
    COMMAND "${SIMPSONS_FXC}" /nologo /Ges /Gis /O3 /T ${PROFILE} /E ${ENTRY}
      /Fh "${HEADER}" /Vn "k${ENTRY}" "${CMAKE_SOURCE_DIR}/renderer/corona_shader.hlsl"
    DEPENDS renderer/corona_shader.hlsl tools/analyze_corona_shaders.py analysis/simpsons.pe VERBATIM)
  list(APPEND SIMPSONS_CORONA_HEADERS "${HEADER}")
endforeach()
set(SIMPSONS_IMMEDIATE_HEADERS)
foreach(ENTRY VSImmediate PSImmediate PSImmediateDual PSImmediateDraw PSImmediateDualDraw VSImmediateProjected PSImmediateProjected PSImmediateProjectedDraw PSImmediateProjectedDual PSImmediateProjectedDualDraw VSRadial PSRadial PSRadialQuery PSRadialDraw)
  if(ENTRY MATCHES "^VS")
    set(PROFILE vs_5_0)
  else()
    set(PROFILE ps_5_0)
  endif()
  set(HEADER "${SIMPSONS_SHADER_DIR}/${ENTRY}.h")
  add_custom_command(OUTPUT "${HEADER}"
    COMMAND "${Python3_EXECUTABLE}" -B "${CMAKE_SOURCE_DIR}/tools/analyze_immediate_shaders.py"
    COMMAND "${Python3_EXECUTABLE}" -B "${CMAKE_SOURCE_DIR}/tools/analyze_radial_shaders.py"
    COMMAND "${Python3_EXECUTABLE}" -B "${CMAKE_SOURCE_DIR}/tools/analyze_projected_immediate_shaders.py"
    COMMAND "${SIMPSONS_FXC}" /nologo /Ges /Gis /O3 /T ${PROFILE} /E ${ENTRY}
      /Fh "${HEADER}" /Vn "k${ENTRY}" "${CMAKE_SOURCE_DIR}/renderer/immediate_draw.hlsl"
    DEPENDS renderer/immediate_shader.hlsl renderer/immediate_draw.hlsl renderer/radial_shader.hlsl renderer/projected_immediate_shader.hlsl renderer/shadow_mesh.hlsl
      tools/analyze_immediate_shaders.py tools/analyze_radial_shaders.py tools/analyze_projected_immediate_shaders.py analysis/simpsons.pe VERBATIM)
  list(APPEND SIMPSONS_IMMEDIATE_HEADERS "${HEADER}")
endforeach()
set(SIMPSONS_BALL_EFFECT_HEADERS)
foreach(ENTRY VSBallEffect PSBallEffect PSBallEffectDraw)
  if(ENTRY MATCHES "^VS")
    set(PROFILE vs_5_0)
  else()
    set(PROFILE ps_5_0)
  endif()
  set(HEADER "${SIMPSONS_SHADER_DIR}/${ENTRY}.h")
  add_custom_command(OUTPUT "${HEADER}"
    COMMAND "${Python3_EXECUTABLE}" -B "${CMAKE_SOURCE_DIR}/tools/analyze_ball_effect_shaders.py" --verify
    COMMAND "${SIMPSONS_FXC}" /nologo /Ges /Gis /O3 /T ${PROFILE} /E ${ENTRY}
      /Fh "${HEADER}" /Vn "k${ENTRY}" "${CMAKE_SOURCE_DIR}/renderer/ball_effect.hlsl"
    DEPENDS renderer/ball_effect.hlsl renderer/shadow_mesh.hlsl tools/analyze_ball_effect_shaders.py analysis/simpsons.pe VERBATIM)
  list(APPEND SIMPSONS_BALL_EFFECT_HEADERS "${HEADER}")
endforeach()
set_property(SOURCE renderer/ball_effect.cpp APPEND PROPERTY OBJECT_DEPENDS "${SIMPSONS_BALL_EFFECT_HEADERS}")
set(SIMPSONS_BALL_COMPOSITE_HEADERS)
foreach(ENTRY PSBallEncode PSBallEncodeDraw PSBallComposite PSBallCompositeDraw)
  set(HEADER "${SIMPSONS_SHADER_DIR}/${ENTRY}.h")
  add_custom_command(OUTPUT "${HEADER}"
    COMMAND "${Python3_EXECUTABLE}" -B "${CMAKE_SOURCE_DIR}/tools/analyze_ball_composite_shaders.py" --verify
    COMMAND "${SIMPSONS_FXC}" /nologo /Ges /Gis /O3 /T ps_5_0 /E ${ENTRY}
      /Fh "${HEADER}" /Vn "k${ENTRY}" "${CMAKE_SOURCE_DIR}/renderer/ball_composite.hlsl"
    DEPENDS renderer/ball_composite.hlsl renderer/post_filter_shader.hlsl tools/analyze_ball_composite_shaders.py analysis/simpsons.pe VERBATIM)
  list(APPEND SIMPSONS_BALL_COMPOSITE_HEADERS "${HEADER}")
endforeach()
set(SIMPSONS_LUMA_HEADERS)
foreach(ENTRY PSLuma PSLumaDraw)
  set(HEADER "${SIMPSONS_SHADER_DIR}/${ENTRY}.h")
  add_custom_command(OUTPUT "${HEADER}"
    COMMAND "${Python3_EXECUTABLE}" -B "${CMAKE_SOURCE_DIR}/tools/analyze_luma_shader.py" --verify
    COMMAND "${SIMPSONS_FXC}" /nologo /Ges /Gis /O3 /T ps_5_0 /E ${ENTRY}
      /Fh "${HEADER}" /Vn "k${ENTRY}" "${CMAKE_SOURCE_DIR}/renderer/luma_shader.hlsl"
    DEPENDS renderer/luma_shader.hlsl renderer/post_filter_shader.hlsl tools/analyze_luma_shader.py
      tools/analyze_screen_shaders.py tools/analyze_edge_shaders.py tools/analyze_fourtap_shaders.py analysis/simpsons.pe VERBATIM)
  list(APPEND SIMPSONS_LUMA_HEADERS "${HEADER}")
endforeach()
set(SIMPSONS_SCREEN_EFFECT_HEADERS)
foreach(ENTRY PSDof PSBlur PSBloom PSFogLinear PSFogExp PSFogExp2 PSSat PSFlatEffect PSModulatedFlat
    PSDofDraw PSBlurDraw PSBloomDraw PSFogLinearDraw PSFogExpDraw PSFogExp2Draw PSSatDraw PSFlatEffectDraw PSModulatedFlatDraw)
  set(HEADER "${SIMPSONS_SHADER_DIR}/${ENTRY}.h")
  add_custom_command(OUTPUT "${HEADER}"
    COMMAND "${Python3_EXECUTABLE}" -B "${CMAKE_SOURCE_DIR}/tools/analyze_screen_effect_shaders.py" --verify
    COMMAND "${SIMPSONS_FXC}" /nologo /Ges /Gis /O3 /T ps_5_0 /E ${ENTRY}
      /Fh "${HEADER}" /Vn "k${ENTRY}" "${CMAKE_SOURCE_DIR}/renderer/screen_effects.hlsl"
    DEPENDS renderer/screen_effects.hlsl renderer/post_filter_shader.hlsl tools/analyze_screen_effect_shaders.py
      tools/xenos_pixel_reference.py tools/analyze_screen_shaders.py tools/analyze_edge_shaders.py tools/analyze_fourtap_shaders.py analysis/simpsons.pe VERBATIM)
  list(APPEND SIMPSONS_SCREEN_EFFECT_HEADERS "${HEADER}")
endforeach()
set_property(SOURCE renderer/post_filter.cpp APPEND PROPERTY OBJECT_DEPENDS "${SIMPSONS_POST_FILTER_HEADERS};${SIMPSONS_BALL_COMPOSITE_HEADERS};${SIMPSONS_LUMA_HEADERS};${SIMPSONS_SCREEN_EFFECT_HEADERS};${SIMPSONS_SHADER_DIR}/VSTextured.h")
# Ninja must rebuild the consumers in the same invocation that regenerates
# embedded shader bytecode. Explicit file dependencies also avoid the Windows
# include-dependency path spelling leaving the executable one build behind.
include(cmake/remaining_effect_shaders.cmake)
set_property(SOURCE renderer/rigid_mesh.cpp APPEND PROPERTY OBJECT_DEPENDS
  "${SIMPSONS_RIGID_HEADERS};${SIMPSONS_RIGID_DRAW_HEADER};${SIMPSONS_RIGID_TEXTURED_DRAW_HEADER};${SIMPSONS_RIGID_DUAL_DRAW_HEADER};${SIMPSONS_RIGID_GLOSS_DRAW_HEADER}")
set_property(SOURCE renderer/screen_pipeline.cpp APPEND PROPERTY OBJECT_DEPENDS
  "${SIMPSONS_RIGID_HEADERS}")
set_property(SOURCE renderer/skin_mesh.cpp APPEND PROPERTY OBJECT_DEPENDS
  "${SIMPSONS_SHADER_DIR}/VSSkin.h;${SIMPSONS_SHADER_DIR}/PSSkinDraw.h;${SIMPSONS_SHADER_DIR}/PSSkinAlphaDraw.h;${SIMPSONS_SHADER_DIR}/VSSkinDual.h;${SIMPSONS_SHADER_DIR}/PSSkinDualDraw.h")
add_library(SimpsonsGraphics STATIC
  renderer/native_backend.cpp
  renderer/d3d_call_stats.cpp
  renderer/native_input_prompts.cpp
  renderer/cube_texture.cpp
  renderer/screen_pipeline.cpp
  renderer/shadow_mesh.cpp
  renderer/post_filter.cpp
  renderer/mono_mesh.cpp
  ${SIMPSONS_MONO_HEADERS}
  ${SIMPSONS_MONO_DRAW_HEADER}
  ${SIMPSONS_POST_FILTER_HEADERS}
  renderer/zprepass_mesh.cpp
  renderer/rigid_mesh.cpp
  renderer/skin_mesh.cpp
  renderer/sky_mesh.cpp
  renderer/particle_draw.cpp
  renderer/immediate_draw.cpp
  renderer/ball_effect.cpp
  ${SIMPSONS_BALL_EFFECT_HEADERS}
  ${SIMPSONS_BALL_COMPOSITE_HEADERS}
  ${SIMPSONS_LUMA_HEADERS}
  ${SIMPSONS_SCREEN_EFFECT_HEADERS}
  ${SIMPSONS_IMMEDIATE_HEADERS}
  ${SIMPSONS_PARTICLE_HEADERS}
  ${SIMPSONS_CORONA_HEADERS}
  ${SIMPSONS_ZPREPASS_HEADERS}
  ${SIMPSONS_RIGID_HEADERS}
  ${SIMPSONS_RIGID_DRAW_HEADER}
  ${SIMPSONS_RIGID_TEXTURED_DRAW_HEADER}
  ${SIMPSONS_RIGID_DUAL_DRAW_HEADER}
  ${SIMPSONS_RIGID_GLOSS_DRAW_HEADER}
  ${SIMPSONS_SHADOW_ALPHA_HEADERS}
  ${SIMPSONS_SHADOW_MESH_HEADER}
  renderer/native_texture_stream.cpp
  renderer/itxd_blocks.cpp
  renderer/depth_resources.cpp
  renderer/driver_resources.cpp
  renderer/material_resources.cpp
  renderer/effect_resources.cpp
  renderer/effect_reflection.cpp
  renderer/declaration_resources.cpp
  renderer/im2d_vertices.cpp
  renderer/im2d_pipeline.cpp
  renderer/movie_state.cpp
  renderer/movie_geometry.cpp
  renderer/movie_pipeline.cpp
  renderer/engine_state.cpp
  renderer/target_bindings.cpp
  renderer/engine_binding_reset.cpp
  renderer/native_viewport.cpp
  renderer/antialiasing.cpp
  ${SIMPSONS_NATIVE_AA_HEADERS}
  renderer/native_presentation.cpp
  renderer/native_recording.cpp
  ${SIMPSONS_SCREEN_HEADERS} ${SIMPSONS_FOURTAP_HEADERS} ${SIMPSONS_IM2D_DRAW_HEADERS} ${SIMPSONS_MOVIE_HEADERS} ${SIMPSONS_EDGE_HEADERS} ${SIMPSONS_AA_HEADERS} ${SIMPSONS_EDGEAA_HEADERS} ${SIMPSONS_SHADOW_HEADERS} "${SIMPSONS_EFFECT_CATALOG}")
add_dependencies(SimpsonsGraphics VerifyEffectCatalog)
target_include_directories(SimpsonsGraphics PRIVATE "${SIMPSONS_SHADER_DIR}" "${SIMPSONS_GENERATED_DIR}")
target_include_directories(SimpsonsGraphics PUBLIC "${CMAKE_SOURCE_DIR}")
target_compile_definitions(SimpsonsGraphics PUBLIC NOMINMAX WIN32_LEAN_AND_MEAN)
target_link_libraries(SimpsonsGraphics PUBLIC d3d11 dxgi)
target_link_libraries(SimpsonsRuntime PUBLIC SimpsonsGraphics)
add_executable(ParticleBackendTests tests/test_particle_backend.cpp)
target_link_libraries(ParticleBackendTests PRIVATE SimpsonsGraphics)
add_executable(ParticleProjectedShaderTests tests/test_particle_projected_shader.cpp ${SIMPSONS_PARTICLE_HEADERS})
target_include_directories(ParticleProjectedShaderTests PRIVATE "${SIMPSONS_SHADER_DIR}")
target_compile_definitions(ParticleProjectedShaderTests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
target_compile_options(ParticleProjectedShaderTests PRIVATE /fp:strict /W4 /WX)
target_link_libraries(ParticleProjectedShaderTests PRIVATE SimpsonsGraphics)
set(SIMPSONS_PARTICLE_FAMILY_FIXTURE "${SIMPSONS_SHADER_DIR}/particle_family_fixtures.h")
add_custom_command(OUTPUT "${SIMPSONS_PARTICLE_FAMILY_FIXTURE}"
  COMMAND "${Python3_EXECUTABLE}" -B "${CMAKE_SOURCE_DIR}/tests/test_dual_projected_particle_shader.py"
    --emit-cpp "${SIMPSONS_PARTICLE_FAMILY_FIXTURE}"
  DEPENDS tests/test_dual_projected_particle_shader.py tools/analyze_dual_projected_particle_shader.py
    tools/analyze_particle_type5_shader.py tools/analyze_dual_particle_shader.py tools/analyze_projected_particle_shader.py
    tools/analyze_particle_shader.py tools/analyze_rigid_shader.py tools/analyze_screen_shaders.py analysis/simpsons.pe VERBATIM)
add_executable(ParticleFamilyShaderTests tests/test_particle_family_shader.cpp ${SIMPSONS_PARTICLE_HEADERS} "${SIMPSONS_PARTICLE_FAMILY_FIXTURE}")
target_include_directories(ParticleFamilyShaderTests PRIVATE "${SIMPSONS_SHADER_DIR}")
target_compile_definitions(ParticleFamilyShaderTests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
target_compile_options(ParticleFamilyShaderTests PRIVATE /fp:strict /W4 /WX)
target_link_libraries(ParticleFamilyShaderTests PRIVATE SimpsonsGraphics)
# RelWithDebInfo normally restricts inlining to explicitly inline functions.
# These CPU adapters and AOT helpers run for every original UI primitive; use
# the normal optimized inliner while retaining symbols and strict PPC FP rules.
foreach(NATIVE_TARGET SimpsonsPPC SimpsonsRuntime SimpsonsGraphics)
  target_compile_options(${NATIVE_TARGET} PRIVATE $<$<CONFIG:RelWithDebInfo>:/Ob2>)
endforeach()
add_executable(SimpsonsNative app/main.cpp)
target_link_libraries(SimpsonsNative PRIVATE SimpsonsRuntime)
target_link_options(SimpsonsNative PRIVATE /STACK:67108864,1048576)
add_dependencies(SimpsonsNative NativeVideoMenu)
# A separate output can be built while the normal game is still running.
# It uses the same runtime and entry point; recording is enabled by its launcher.
# Keep it current whenever the normal game is built: recording and resume
# shortcuts launch this executable and otherwise silently retain old features.
add_executable(SimpsonsInputRecorder app/main.cpp)
target_link_libraries(SimpsonsInputRecorder PRIVATE SimpsonsRuntime)
target_link_options(SimpsonsInputRecorder PRIVATE /STACK:67108864,1048576)
add_dependencies(SimpsonsInputRecorder NativeVideoMenu)
add_dependencies(SimpsonsNative SimpsonsInputRecorder)
# Optional IR-based PGO compile flags, production targets only. Test drivers
# stay ordinary native code and link the instrumented/optimized libraries.
# Release/RelWithDebInfo only; /fp:strict, /Ob2, and existing flags are kept.
if(SIMPSONS_PGO_MODE STREQUAL "GENERATE")
  foreach(SIMPSONS_PGO_TARGET SimpsonsPPC SimpsonsRuntime SimpsonsGraphics SimpsonsAudio SimpsonsAudioOutput SimpsonsNative)
    target_compile_options(${SIMPSONS_PGO_TARGET} PRIVATE "$<$<CONFIG:Release>:-fprofile-generate=${SIMPSONS_PGO_GENERATE_DIR_ABS}>" "$<$<CONFIG:RelWithDebInfo>:-fprofile-generate=${SIMPSONS_PGO_GENERATE_DIR_ABS}>" "$<$<CONFIG:Release>:/clang:-fprofile-update=atomic>" "$<$<CONFIG:RelWithDebInfo>:/clang:-fprofile-update=atomic>")
  endforeach()
elseif(SIMPSONS_PGO_MODE STREQUAL "USE")
  foreach(SIMPSONS_PGO_TARGET SimpsonsPPC SimpsonsRuntime SimpsonsGraphics SimpsonsAudio SimpsonsAudioOutput SimpsonsNative)
    target_compile_options(${SIMPSONS_PGO_TARGET} PRIVATE "$<$<CONFIG:Release>:/clang:-fprofile-use=${SIMPSONS_PGO_PROFILE_ABS}>" "$<$<CONFIG:RelWithDebInfo>:/clang:-fprofile-use=${SIMPSONS_PGO_PROFILE_ABS}>")
  endforeach()
endif()
add_executable(SimpsonsLauncher WIN32 app/launcher.cpp)
add_dependencies(SimpsonsLauncher SimpsonsNative)
target_compile_definitions(SimpsonsLauncher PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
target_compile_options(SimpsonsLauncher PRIVATE /W4 /WX /EHsc)
target_link_libraries(SimpsonsLauncher PRIVATE user32 gdi32 shell32)
include(CTest)
if(BUILD_TESTING)
  include(cmake/remaining_effect_tests.cmake)
  add_executable(InputRecordingCrashFixture tests/input_recording_crash_fixture.cpp runtime/native_input_recording.cpp)
  target_include_directories(InputRecordingCrashFixture PRIVATE "${CMAKE_SOURCE_DIR}")
  target_compile_definitions(InputRecordingCrashFixture PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
  target_compile_options(InputRecordingCrashFixture PRIVATE /W4 /WX /EHsc)
  add_test(NAME NativeInputRecordingCrashPersistence COMMAND "${Python3_EXECUTABLE}" -B "${CMAKE_SOURCE_DIR}/tests/test_input_recording_crash.py" "$<TARGET_FILE:InputRecordingCrashFixture>")
  add_test(NAME NativeInputRecordingLaunch COMMAND "${Python3_EXECUTABLE}" -B "${CMAKE_SOURCE_DIR}/tests/test_record_gameplay_inputs.py" -v)
  add_test(NAME NativeGameplayCheckpoint COMMAND "${Python3_EXECUTABLE}" -B "${CMAKE_SOURCE_DIR}/tests/test_gameplay_checkpoint.py" -v)
  add_executable(BallEffectBackendTests tests/test_ball_effect_backend.cpp)
  target_compile_options(BallEffectBackendTests PRIVATE /fp:strict /W4 /WX)
  target_link_libraries(BallEffectBackendTests PRIVATE SimpsonsGraphics)
  add_test(NAME NativeBallEffectWARP COMMAND BallEffectBackendTests)
  add_test(NAME NativeBallEffectHardware COMMAND BallEffectBackendTests --hardware)
  add_test(NAME OriginalBallEffectShaderEvidence COMMAND "${Python3_EXECUTABLE}" -B "${CMAKE_SOURCE_DIR}/tools/analyze_ball_effect_shaders.py" --verify --self-test)
  add_test(NAME OriginalBallCompositeShaderEvidence COMMAND "${Python3_EXECUTABLE}" -B "${CMAKE_SOURCE_DIR}/tools/analyze_ball_composite_shaders.py" --verify --self-test)
  add_test(NAME OriginalLumaShaderEvidence COMMAND "${Python3_EXECUTABLE}" -B "${CMAKE_SOURCE_DIR}/tools/analyze_luma_shader.py" --verify --self-test)
  add_test(NAME OriginalScreenEffectShaderEvidence COMMAND "${Python3_EXECUTABLE}" -B "${CMAKE_SOURCE_DIR}/tools/analyze_screen_effect_shaders.py" --verify --self-test)
  add_executable(ScreenEffectShaderTests tests/test_screen_effect_shaders.cpp ${SIMPSONS_POST_FILTER_HEADERS} ${SIMPSONS_SCREEN_EFFECT_HEADERS})
  target_include_directories(ScreenEffectShaderTests PRIVATE "${SIMPSONS_SHADER_DIR}")
  target_compile_definitions(ScreenEffectShaderTests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
  target_compile_options(ScreenEffectShaderTests PRIVATE /fp:strict /W4 /WX)
  target_link_libraries(ScreenEffectShaderTests PRIVATE d3d11 dxgi)
  add_test(NAME NativeScreenEffectShadersWARP COMMAND "${Python3_EXECUTABLE}" -B "${CMAKE_SOURCE_DIR}/tools/check_screen_effect_shaders.py" --executable $<TARGET_FILE:ScreenEffectShaderTests>)
  add_test(NAME NativeScreenEffectShadersHardware COMMAND "${Python3_EXECUTABLE}" -B "${CMAKE_SOURCE_DIR}/tools/check_screen_effect_shaders.py" --executable $<TARGET_FILE:ScreenEffectShaderTests> --hardware)
  set_tests_properties(NativeScreenEffectShadersWARP NativeScreenEffectShadersHardware PROPERTIES TIMEOUT 300)
  add_executable(ImmediateBackendTests tests/test_immediate_backend.cpp)
  target_compile_options(ImmediateBackendTests PRIVATE /fp:strict /W4 /WX)
  target_link_libraries(ImmediateBackendTests PRIVATE SimpsonsGraphics)
  add_test(NAME NativeImmediateWARP COMMAND ImmediateBackendTests)
  add_test(NAME NativeImmediateHardware COMMAND ImmediateBackendTests --hardware)
  add_executable(ProjectedImmediateBackendTests tests/test_projected_immediate_backend.cpp)
  target_compile_options(ProjectedImmediateBackendTests PRIVATE /fp:strict /W4 /WX)
  target_link_libraries(ProjectedImmediateBackendTests PRIVATE SimpsonsGraphics)
  add_test(NAME NativeProjectedImmediateWARP COMMAND ProjectedImmediateBackendTests)
  add_test(NAME NativeProjectedImmediateHardware COMMAND ProjectedImmediateBackendTests --hardware)
  add_test(NAME OriginalProjectedImmediateShaderEvidence COMMAND "${Python3_EXECUTABLE}" -B "${CMAKE_SOURCE_DIR}/tools/analyze_projected_immediate_shaders.py" --self-test)
  add_test(NAME OriginalImmediateShaderEvidence COMMAND "${Python3_EXECUTABLE}" -B "${CMAKE_SOURCE_DIR}/tools/analyze_immediate_shaders.py")
  add_test(NAME OriginalRadialShaderEvidence COMMAND "${Python3_EXECUTABLE}" -B "${CMAKE_SOURCE_DIR}/tools/analyze_radial_shaders.py")
  add_executable(CoronaBackendTests tests/test_corona_backend.cpp)
  target_link_libraries(CoronaBackendTests PRIVATE SimpsonsGraphics)
  add_test(NAME NativeCoronaBackend COMMAND CoronaBackendTests)
  add_test(NAME OriginalCoronaShaderEvidence COMMAND "${Python3_EXECUTABLE}" -B -m unittest discover -s "${CMAKE_SOURCE_DIR}/tests" -p test_corona_shaders.py -v)
  add_executable(RigidPacketOwnerTests tests/test_rigid_packet_owner.cpp)
  target_include_directories(RigidPacketOwnerTests PRIVATE "${CMAKE_SOURCE_DIR}")
  target_compile_options(RigidPacketOwnerTests PRIVATE /W4 /WX /EHsc)
  add_test(NAME NativeRigidPacketOwner COMMAND RigidPacketOwnerTests)
  add_test(NAME NativeGameplayEvidence COMMAND "${Python3_EXECUTABLE}" -B -m unittest discover -s "${CMAKE_SOURCE_DIR}/tests" -p test_gameplay_evidence.py -v)
  add_executable(CharacterMeshTests tests/test_character_mesh.cpp)
  target_compile_options(CharacterMeshTests PRIVATE /fp:strict /W4 /WX)
  target_link_libraries(CharacterMeshTests PRIVATE SimpsonsRuntime)
  add_test(NAME NativeCharacterMeshDecode COMMAND CharacterMeshTests)
  add_executable(StaticShadowMeshTests tests/test_static_shadow_mesh.cpp)
  target_compile_options(StaticShadowMeshTests PRIVATE /fp:strict /W4 /WX)
  target_link_libraries(StaticShadowMeshTests PRIVATE SimpsonsRuntime)
  add_test(NAME NativeStaticShadowMeshDecode COMMAND StaticShadowMeshTests)
  add_test(NAME OriginalCharacterMeshEvidence COMMAND "${Python3_EXECUTABLE}" -B "${CMAKE_SOURCE_DIR}/tools/analyze_character_mesh.py" --verify)
  add_executable(ShadowMeshTests tests/test_shadow_mesh.cpp)
  add_executable(ZPrepassMeshTests tests/test_zprepass_mesh.cpp)
  add_executable(MonoMeshTests tests/test_mono_mesh.cpp)
  target_compile_options(MonoMeshTests PRIVATE /fp:strict /W4 /WX)
  target_link_libraries(MonoMeshTests PRIVATE SimpsonsGraphics)
  target_include_directories(MonoMeshTests PRIVATE "${SIMPSONS_SHADER_DIR}")
  add_test(NAME NativeMonoMeshWARP COMMAND MonoMeshTests "${CMAKE_SOURCE_DIR}/analysis/simpsons.pe")
  add_test(NAME NativeMonoMeshHardware COMMAND MonoMeshTests "${CMAKE_SOURCE_DIR}/analysis/simpsons.pe" --hardware)
  set_tests_properties(NativeMonoMeshWARP NativeMonoMeshHardware PROPERTIES TIMEOUT 60)

  add_executable(ZPrepassVertexTests tests/test_zprepass_vertices.cpp)
  add_executable(RigidVertexTests tests/test_rigid_vertices.cpp)
  target_compile_options(RigidVertexTests PRIVATE /fp:strict /W4 /WX)
  target_link_libraries(RigidVertexTests PRIVATE SimpsonsRuntime)
  add_test(NAME NativeRigidVertexDecode COMMAND RigidVertexTests)
  add_executable(SkinVertexTests tests/test_skin_vertices.cpp)
  target_compile_options(SkinVertexTests PRIVATE /fp:strict /W4 /WX)
  target_link_libraries(SkinVertexTests PRIVATE SimpsonsRuntime)
  add_test(NAME NativeSkinVertexDecode COMMAND SkinVertexTests)
  add_test(NAME OriginalSkinMorphStreams COMMAND "${Python3_EXECUTABLE}" -B "${CMAKE_SOURCE_DIR}/tools/analyze_skin_morph_streams.py")
  add_executable(SkyVertexTests tests/test_sky_vertices.cpp)
  target_compile_options(SkyVertexTests PRIVATE /fp:strict /W4 /WX)
  target_link_libraries(SkyVertexTests PRIVATE SimpsonsRuntime)
  add_test(NAME NativeSkyVertexDecode COMMAND SkyVertexTests)
  add_executable(QuadListEffectTests tests/test_quad_list_effect.cpp)
  add_executable(EightVertexEffectTests tests/test_eight_vertex_effect.cpp)
  add_executable(RibbonEffectTests tests/test_ribbon_effect.cpp)
  add_executable(DecalEffectsTests tests/test_decal_effects.cpp)
  foreach(effect_test QuadListEffectTests EightVertexEffectTests RibbonEffectTests DecalEffectsTests)
    target_compile_options(${effect_test} PRIVATE /fp:strict /W4 /WX)
    target_link_libraries(${effect_test} PRIVATE SimpsonsRuntime)
    target_link_options(${effect_test} PRIVATE /STACK:67108864,1048576)
  endforeach()
  add_test(NAME OriginalQuadListEffect COMMAND QuadListEffectTests "${CMAKE_SOURCE_DIR}/analysis/simpsons.pe")
  add_test(NAME OriginalEightVertexEffect COMMAND EightVertexEffectTests "${CMAKE_SOURCE_DIR}/analysis/simpsons.pe")
  add_test(NAME OriginalRibbonEffect COMMAND RibbonEffectTests "${CMAKE_SOURCE_DIR}/analysis/simpsons.pe")
  add_test(NAME OriginalDecalEffects COMMAND DecalEffectsTests "${CMAKE_SOURCE_DIR}/analysis/simpsons.pe")
  set_tests_properties(OriginalQuadListEffect OriginalEightVertexEffect OriginalRibbonEffect OriginalDecalEffects PROPERTIES TIMEOUT 60)
  add_executable(BeamEndpointEffectTests tests/test_beam_endpoint_effect.cpp)
  target_compile_options(BeamEndpointEffectTests PRIVATE /fp:strict /W4 /WX)
  target_link_libraries(BeamEndpointEffectTests PRIVATE SimpsonsRuntime)
  target_link_options(BeamEndpointEffectTests PRIVATE /STACK:67108864,1048576)
  add_test(NAME OriginalBeamEndpointEffect COMMAND BeamEndpointEffectTests "${CMAKE_SOURCE_DIR}/analysis/simpsons.pe")
  set_tests_properties(OriginalBeamEndpointEffect PROPERTIES TIMEOUT 60)
  add_executable(ImmediateEffectsTests tests/test_immediate_effects.cpp)
  target_compile_options(ImmediateEffectsTests PRIVATE /fp:strict /W4 /WX)
  target_link_libraries(ImmediateEffectsTests PRIVATE SimpsonsRuntime)
  target_link_options(ImmediateEffectsTests PRIVATE /STACK:67108864,1048576)
  add_test(NAME OriginalImmediateEffects COMMAND ImmediateEffectsTests "${CMAKE_SOURCE_DIR}/analysis/simpsons.pe")
  set_tests_properties(OriginalImmediateEffects PROPERTIES TIMEOUT 60)
  add_executable(RadialOriginalTests tests/test_radial_original.cpp)
  target_compile_options(RadialOriginalTests PRIVATE /fp:strict /W4 /WX)
  target_link_libraries(RadialOriginalTests PRIVATE SimpsonsRuntime)
  target_link_options(RadialOriginalTests PRIVATE /STACK:67108864,1048576)
  add_test(NAME OriginalRadialOwnerLoop COMMAND RadialOriginalTests "${CMAKE_SOURCE_DIR}/analysis/simpsons.pe")
  set_tests_properties(OriginalRadialOwnerLoop PROPERTIES TIMEOUT 90)
  add_executable(ConfiguredAudioStreamTests tests/test_audio_stream_configured.cpp)
  target_compile_options(ConfiguredAudioStreamTests PRIVATE /fp:strict /W4 /WX)
  target_link_libraries(ConfiguredAudioStreamTests PRIVATE SimpsonsRuntime bcrypt)
  target_link_options(ConfiguredAudioStreamTests PRIVATE /STACK:67108864,1048576)
  add_test(NAME OriginalConfiguredMusCancel COMMAND ConfiguredAudioStreamTests "${CMAKE_SOURCE_DIR}/analysis/simpsons.pe" 0)
  add_test(NAME OriginalConfiguredMusComplete COMMAND ConfiguredAudioStreamTests "${CMAKE_SOURCE_DIR}/analysis/simpsons.pe" 1)
  add_test(NAME PrepareConfiguredMusFixture COMMAND "${Python3_EXECUTABLE}" -B "${CMAKE_SOURCE_DIR}/tools/prepare_configured_mus_fixture.py")
  set_tests_properties(PrepareConfiguredMusFixture PROPERTIES FIXTURES_SETUP ConfiguredMusDescriptor TIMEOUT 60)
  set_tests_properties(OriginalConfiguredMusCancel OriginalConfiguredMusComplete PROPERTIES FIXTURES_REQUIRED ConfiguredMusDescriptor)
  set_tests_properties(OriginalConfiguredMusCancel OriginalConfiguredMusComplete PROPERTIES TIMEOUT 60)
  add_executable(GeometryExtentTests tests/test_geometry_extent.cpp)
  target_include_directories(GeometryExtentTests PRIVATE "${CMAKE_SOURCE_DIR}")
  target_compile_options(GeometryExtentTests PRIVATE /W4 /WX)
  add_test(NAME NativeGeometryExtent COMMAND GeometryExtentTests)
  add_executable(LargeGeometryDecoderTests tests/test_large_geometry_decoders.cpp)
  target_compile_options(LargeGeometryDecoderTests PRIVATE /fp:strict /W4 /WX)
  target_link_libraries(LargeGeometryDecoderTests PRIVATE SimpsonsRuntime)
  add_test(NAME NativeLargeGeometryDecode COMMAND LargeGeometryDecoderTests)
  add_test(NAME OriginalGeometryExtentContract COMMAND "${Python3_EXECUTABLE}" -B "${CMAKE_SOURCE_DIR}/tools/analyze_geometry_extent_contract.py")
  add_executable(OriginalSkyLargeOwnerPassTests tests/test_sky_large_owner_pass.cpp)
  target_compile_options(OriginalSkyLargeOwnerPassTests PRIVATE /fp:strict /W4 /WX)
  target_link_libraries(OriginalSkyLargeOwnerPassTests PRIVATE SimpsonsRuntime)
  target_link_options(OriginalSkyLargeOwnerPassTests PRIVATE /STACK:67108864,1048576)
  add_test(NAME OriginalSkyLargeOwnerPass COMMAND OriginalSkyLargeOwnerPassTests "${CMAKE_SOURCE_DIR}/analysis/simpsons.pe")
  add_test(NAME OriginalSkyPartialTailOwnerPass COMMAND OriginalSkyLargeOwnerPassTests "${CMAKE_SOURCE_DIR}/analysis/simpsons.pe" partial_tail)
  set_tests_properties(OriginalSkyLargeOwnerPass OriginalSkyPartialTailOwnerPass PROPERTIES TIMEOUT 60 RUN_SERIAL TRUE)
  add_executable(OriginalSkyIndexOwnerPassTests tests/test_sky_index_owner_pass.cpp)
  target_compile_options(OriginalSkyIndexOwnerPassTests PRIVATE /fp:strict /W4 /WX)
  target_link_libraries(OriginalSkyIndexOwnerPassTests PRIVATE SimpsonsRuntime)
  target_link_options(OriginalSkyIndexOwnerPassTests PRIVATE /STACK:67108864,1048576)
  add_test(NAME OriginalSkyLargeIndexOwnerPass COMMAND OriginalSkyIndexOwnerPassTests "${CMAKE_SOURCE_DIR}/analysis/simpsons.pe" large)
  add_test(NAME OriginalSkyOddIndexTailOwnerPass COMMAND OriginalSkyIndexOwnerPassTests "${CMAKE_SOURCE_DIR}/analysis/simpsons.pe" odd_tail)
  add_test(NAME OriginalSkyIndexNativeCapacityPass COMMAND OriginalSkyIndexOwnerPassTests "${CMAKE_SOURCE_DIR}/analysis/simpsons.pe" native_capacity)
  set_tests_properties(OriginalSkyLargeIndexOwnerPass OriginalSkyOddIndexTailOwnerPass OriginalSkyIndexNativeCapacityPass PROPERTIES TIMEOUT 120 RUN_SERIAL TRUE)
  add_test(NAME OriginalIndexExtentContract COMMAND "${Python3_EXECUTABLE}" -B "${CMAKE_SOURCE_DIR}/tools/analyze_index_extent_contract.py")
  add_executable(OriginalSkyShortStripPassTests tests/test_sky_short_strip_pass.cpp)
  target_compile_options(OriginalSkyShortStripPassTests PRIVATE /fp:strict /W4 /WX)
  target_link_libraries(OriginalSkyShortStripPassTests PRIVATE SimpsonsRuntime)
  target_link_options(OriginalSkyShortStripPassTests PRIVATE /STACK:67108864,1048576)
  foreach(short_strip_count IN ITEMS 0 1 2)
    add_test(NAME OriginalSkyShortStrip_${short_strip_count} COMMAND OriginalSkyShortStripPassTests "${CMAKE_SOURCE_DIR}/analysis/simpsons.pe" ${short_strip_count})
    set_tests_properties(OriginalSkyShortStrip_${short_strip_count} PROPERTIES TIMEOUT 60 RUN_SERIAL TRUE)
  endforeach()
  add_test(NAME OriginalShortStripContract COMMAND "${Python3_EXECUTABLE}" -B "${CMAKE_SOURCE_DIR}/tools/analyze_short_strip_contract.py")
  add_executable(OriginalSkyStripBoundaryPassTests tests/test_sky_strip_boundary_pass.cpp)
  target_compile_options(OriginalSkyStripBoundaryPassTests PRIVATE /fp:strict /W4 /WX)
  target_link_libraries(OriginalSkyStripBoundaryPassTests PRIVATE SimpsonsRuntime)
  target_link_options(OriginalSkyStripBoundaryPassTests PRIVATE /STACK:67108864,1048576)
  add_test(NAME OriginalSkyStripBoundaryPass COMMAND OriginalSkyStripBoundaryPassTests "${CMAKE_SOURCE_DIR}/analysis/simpsons.pe")
  set_tests_properties(OriginalSkyStripBoundaryPass PROPERTIES TIMEOUT 120 RUN_SERIAL TRUE)
  add_test(NAME OriginalStripChunkContract COMMAND "${Python3_EXECUTABLE}" -B "${CMAKE_SOURCE_DIR}/tools/analyze_strip_chunk_contract.py")
  add_test(NAME OriginalCopiedITXDLifetimeContract COMMAND "${Python3_EXECUTABLE}" -B
    "${CMAKE_SOURCE_DIR}/tools/analyze_copied_itxd_lifetime_contract.py")
  add_executable(MonoPassTests tests/test_mono_pass.cpp)
  target_compile_options(MonoPassTests PRIVATE /fp:strict /W4 /WX)
  target_link_libraries(MonoPassTests PRIVATE SimpsonsRuntime)
  target_link_options(MonoPassTests PRIVATE /STACK:67108864,1048576)
  add_test(NAME OriginalMonoMaskPass COMMAND MonoPassTests "${CMAKE_SOURCE_DIR}/analysis/simpsons.pe")
  set_tests_properties(OriginalMonoMaskPass PROPERTIES TIMEOUT 60)
  add_executable(MonoImmediatePassTests tests/test_mono_immediate_pass.cpp)
  target_compile_options(MonoImmediatePassTests PRIVATE /fp:strict /W4 /WX)
  target_link_libraries(MonoImmediatePassTests PRIVATE SimpsonsRuntime)
  target_link_options(MonoImmediatePassTests PRIVATE /STACK:67108864,1048576)
  add_executable(EffectProducerEntryTests tests/test_effect_producer_entry.cpp)
  target_compile_options(EffectProducerEntryTests PRIVATE /fp:strict /W4 /WX)
  target_link_libraries(EffectProducerEntryTests PRIVATE SimpsonsRuntime)
  target_link_options(EffectProducerEntryTests PRIVATE /STACK:67108864,1048576)
  add_test(NAME OriginalEffectProducerEntry COMMAND EffectProducerEntryTests
    "${CMAKE_SOURCE_DIR}/analysis/simpsons.pe" "${CMAKE_CURRENT_BINARY_DIR}")
  set_tests_properties(OriginalEffectProducerEntry PROPERTIES TIMEOUT 120 RUN_SERIAL TRUE)
  add_executable(MonoRecordingFrontierTests tests/test_mono_recording_frontier.cpp)
  target_compile_options(MonoRecordingFrontierTests PRIVATE /fp:strict /W4 /WX)
  target_link_libraries(MonoRecordingFrontierTests PRIVATE SimpsonsRuntime)
  target_link_options(MonoRecordingFrontierTests PRIVATE /STACK:67108864,1048576)
  foreach(mono_recording_route default bit2)
    add_test(NAME OriginalMonoRecording_${mono_recording_route} COMMAND MonoRecordingFrontierTests
      "${CMAKE_SOURCE_DIR}/analysis/simpsons.pe" "${mono_recording_route}")
    set_tests_properties(OriginalMonoRecording_${mono_recording_route} PROPERTIES TIMEOUT 90)
  endforeach()
  foreach(mono_route alpha opaque)
    add_test(NAME OriginalMonoImmediate_${mono_route} COMMAND MonoImmediatePassTests
      "${CMAKE_SOURCE_DIR}/analysis/simpsons.pe" ${mono_route})
    set_tests_properties(OriginalMonoImmediate_${mono_route} PROPERTIES TIMEOUT 60 RUN_SERIAL TRUE)
  endforeach()
  add_executable(ZPrepassPassTests tests/test_zprepass_pass.cpp)
  target_compile_options(ZPrepassPassTests PRIVATE /fp:strict /W4 /WX)
  target_link_libraries(ZPrepassPassTests PRIVATE SimpsonsRuntime)
  target_link_options(ZPrepassPassTests PRIVATE /STACK:67108864,1048576)
  add_test(NAME OriginalZPrepassPass COMMAND ZPrepassPassTests "${CMAKE_SOURCE_DIR}/analysis/simpsons.pe")
  set_tests_properties(OriginalZPrepassPass PROPERTIES TIMEOUT 60)
  add_executable(RigidUvPassTests tests/test_rigid_uv_pass.cpp)
  target_compile_options(RigidUvPassTests PRIVATE /fp:strict /W4 /WX)
  target_link_libraries(RigidUvPassTests PRIVATE SimpsonsRuntime)
  target_link_options(RigidUvPassTests PRIVATE /STACK:67108864,1048576)
  add_test(NAME OriginalRigidUvPass COMMAND RigidUvPassTests "${CMAKE_SOURCE_DIR}/analysis/simpsons.pe"
    "${CMAKE_SOURCE_DIR}/build/itxd-palette/loc_split4.itxd")
  set_tests_properties(OriginalRigidUvPass PROPERTIES TIMEOUT 60)
  add_executable(UvPassTests tests/test_uv_pass.cpp)
  target_compile_options(UvPassTests PRIVATE /fp:strict /W4 /WX)
  target_link_libraries(UvPassTests PRIVATE SimpsonsRuntime)
  target_link_options(UvPassTests PRIVATE /STACK:67108864,1048576)
  add_test(NAME OriginalUvPass COMMAND UvPassTests "${CMAKE_SOURCE_DIR}/analysis/simpsons.pe"
    "${CMAKE_SOURCE_DIR}/build/itxd-palette/loc_split4.itxd")
  set_tests_properties(OriginalUvPass PROPERTIES TIMEOUT 60 RUN_SERIAL TRUE)
  add_executable(FlipbookPassTests tests/test_flipbook_pass.cpp)
  target_compile_options(FlipbookPassTests PRIVATE /fp:strict /W4 /WX)
  target_link_libraries(FlipbookPassTests PRIVATE SimpsonsRuntime)
  target_link_options(FlipbookPassTests PRIVATE /STACK:67108864,1048576)
  add_test(NAME OriginalFlipbookPass COMMAND FlipbookPassTests "${CMAKE_SOURCE_DIR}/analysis/simpsons.pe"
    "${CMAKE_SOURCE_DIR}/build/itxd-palette/loc_split4.itxd")
  set_tests_properties(OriginalFlipbookPass PROPERTIES TIMEOUT 60 RUN_SERIAL TRUE)
  add_executable(RigidDualPassTests tests/test_rigid_dual_pass.cpp)
  target_compile_options(RigidDualPassTests PRIVATE /fp:strict /W4 /WX)
  target_link_libraries(RigidDualPassTests PRIVATE SimpsonsRuntime)
  target_link_options(RigidDualPassTests PRIVATE /STACK:67108864,1048576)
  add_test(NAME OriginalRigidDualPass COMMAND RigidDualPassTests "${CMAKE_SOURCE_DIR}/analysis/simpsons.pe"
    "${CMAKE_SOURCE_DIR}/build/itxd-palette/loc_split4.itxd")
  set_tests_properties(OriginalRigidDualPass PROPERTIES TIMEOUT 60 RUN_SERIAL TRUE)
  foreach(FAMILY base textured gloss multitone normalmap)
    string(SUBSTRING "${FAMILY}" 0 1 FAMILY_FIRST)
    string(TOUPPER "${FAMILY_FIRST}" FAMILY_FIRST)
    string(SUBSTRING "${FAMILY}" 1 -1 FAMILY_REST)
    set(FAMILY_LABEL "${FAMILY_FIRST}${FAMILY_REST}")
    add_test(NAME OriginalRigid${FAMILY_LABEL}Pass COMMAND RigidDualPassTests "${CMAKE_SOURCE_DIR}/analysis/simpsons.pe"
      "${CMAKE_SOURCE_DIR}/build/itxd-palette/loc_split4.itxd" ${FAMILY})
    set_tests_properties(OriginalRigid${FAMILY_LABEL}Pass PROPERTIES TIMEOUT 60 RUN_SERIAL TRUE)
  endforeach()
  target_compile_options(ZPrepassVertexTests PRIVATE /fp:strict /W4 /WX)
  target_link_libraries(ZPrepassVertexTests PRIVATE SimpsonsRuntime)
  add_test(NAME NativeZPrepassVertexDecode COMMAND ZPrepassVertexTests)
  target_compile_options(ZPrepassMeshTests PRIVATE /fp:strict /W4 /WX)
  target_link_libraries(ZPrepassMeshTests PRIVATE SimpsonsGraphics)
  target_include_directories(ZPrepassMeshTests PRIVATE "${SIMPSONS_SHADER_DIR}")
  add_test(NAME NativeZPrepassMeshWARP COMMAND ZPrepassMeshTests "${CMAKE_SOURCE_DIR}/analysis/simpsons.pe")
  add_test(NAME NativeZPrepassMeshHardware COMMAND ZPrepassMeshTests "${CMAKE_SOURCE_DIR}/analysis/simpsons.pe" --hardware)
  set_tests_properties(NativeZPrepassMeshWARP NativeZPrepassMeshHardware PROPERTIES TIMEOUT 60)
  add_executable(ZPrepassShaderTests tests/test_zprepass_shader.cpp ${SIMPSONS_ZPREPASS_HEADERS})
  add_executable(PostFilterTests tests/test_post_filter.cpp)
  target_compile_options(PostFilterTests PRIVATE /fp:strict /W4 /WX)
  target_link_libraries(PostFilterTests PRIVATE SimpsonsGraphics)
  target_include_directories(PostFilterTests PRIVATE "${SIMPSONS_SHADER_DIR}")
  add_test(NAME NativePostFilterWARP COMMAND PostFilterTests)
  add_test(NAME NativePostFilterHardware COMMAND PostFilterTests --hardware)
  set_tests_properties(NativePostFilterWARP NativePostFilterHardware PROPERTIES TIMEOUT 60)
  add_executable(PostFilterShaderTests tests/test_post_filter_shaders.cpp ${SIMPSONS_POST_FILTER_HEADERS})
  target_include_directories(PostFilterShaderTests PRIVATE "${SIMPSONS_SHADER_DIR}")
  target_compile_definitions(PostFilterShaderTests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
  target_compile_options(PostFilterShaderTests PRIVATE /fp:strict /W4 /WX)
  target_link_libraries(PostFilterShaderTests PRIVATE d3d11 dxgi)
  add_test(NAME NativePostFilterShaderWARP COMMAND PostFilterShaderTests)
  add_test(NAME NativePostFilterShaderHardware COMMAND PostFilterShaderTests --hardware)
  add_test(NAME OriginalPostFilterShaderEvidence COMMAND "${Python3_EXECUTABLE}" -B "${CMAKE_SOURCE_DIR}/tools/analyze_post_filter_shaders.py" --verify --self-test)
  set_tests_properties(NativePostFilterShaderWARP NativePostFilterShaderHardware PROPERTIES TIMEOUT 60)
  add_executable(MonoShaderTests tests/test_mono_shader.cpp ${SIMPSONS_MONO_HEADERS})
  target_include_directories(MonoShaderTests PRIVATE "${SIMPSONS_SHADER_DIR}")
  target_compile_definitions(MonoShaderTests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
  target_compile_options(MonoShaderTests PRIVATE /fp:strict /W4 /WX)
  target_link_libraries(MonoShaderTests PRIVATE d3d11 dxgi)
  add_test(NAME NativeMonoShaderWARP COMMAND MonoShaderTests)
  add_test(NAME NativeMonoShaderHardware COMMAND MonoShaderTests --hardware)
  add_test(NAME OriginalMonoShaderEvidence COMMAND "${Python3_EXECUTABLE}" -B "${CMAKE_SOURCE_DIR}/tools/analyze_mono_shader.py" --verify --self-test)
  set_tests_properties(NativeMonoShaderWARP NativeMonoShaderHardware PROPERTIES TIMEOUT 60)
  set_property(SOURCE tests/test_rigid_alpha_shader.cpp APPEND PROPERTY OBJECT_DEPENDS
    "${SIMPSONS_RIGID_ALPHA_HEADERS};${SIMPSONS_SHADER_DIR}/PS168F8.h")
  add_executable(RigidAlphaShaderTests tests/test_rigid_alpha_shader.cpp ${SIMPSONS_RIGID_ALPHA_HEADERS}
    "${SIMPSONS_SHADER_DIR}/PS168F8.h")
  target_include_directories(RigidAlphaShaderTests PRIVATE "${SIMPSONS_SHADER_DIR}")
  target_compile_definitions(RigidAlphaShaderTests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
  target_compile_options(RigidAlphaShaderTests PRIVATE /fp:strict /W4 /WX)
  target_link_libraries(RigidAlphaShaderTests PRIVATE d3d11 dxgi)
  add_test(NAME NativeRigidAlphaShaderWARP COMMAND RigidAlphaShaderTests)
  add_test(NAME NativeRigidAlphaShaderHardware COMMAND RigidAlphaShaderTests --hardware)
  add_test(NAME OriginalRigidAlphaInventory COMMAND "${Python3_EXECUTABLE}" -B
    "${CMAKE_SOURCE_DIR}/tests/test_rigid_alpha_shader.py" -v)
  set_tests_properties(NativeRigidAlphaShaderWARP NativeRigidAlphaShaderHardware PROPERTIES TIMEOUT 60)
  add_executable(RigidShaderTests tests/test_rigid_shader.cpp ${SIMPSONS_RIGID_HEADERS})
  add_executable(SkinShaderTests tests/test_skin_shader.cpp ${SIMPSONS_RIGID_HEADERS})
  set(SIMPSONS_SKIN_TEXTURED_FIXTURES "${SIMPSONS_SHADER_DIR}/skin_textured_fixtures.h")
  add_custom_command(OUTPUT "${SIMPSONS_SKIN_TEXTURED_FIXTURES}"
    COMMAND "${Python3_EXECUTABLE}" -B "${CMAKE_SOURCE_DIR}/tests/test_skin_textured_shader.py"
      --emit-cpp "${SIMPSONS_SKIN_TEXTURED_FIXTURES}"
    DEPENDS tests/test_skin_textured_shader.py tools/analyze_skin_textured_shader.py
      tests/test_skin_dualalpha_shader.py
      tools/analyze_skin_shader.py tools/analyze_skin_dualtextured_shader.py tools/skin_shader_emit.py
      tools/analyze_rigid_shader.py tools/analyze_screen_shaders.py tools/analyze_edge_shaders.py
      analysis/simpsons.pe VERBATIM)
  add_executable(SkinTexturedShaderTests tests/test_skin_textured_gpu.cpp
    "${SIMPSONS_SKIN_TEXTURED_FIXTURES}" ${SIMPSONS_SKIN_TEXTURED_HEADERS})
  target_include_directories(SkinTexturedShaderTests PRIVATE "${CMAKE_SOURCE_DIR}" "${SIMPSONS_SHADER_DIR}")
  target_compile_options(SkinTexturedShaderTests PRIVATE /fp:strict /W4 /WX)
  target_link_libraries(SkinTexturedShaderTests PRIVATE d3d11 dxgi)
  add_test(NAME NativeSkinTexturedShaderWARP COMMAND SkinTexturedShaderTests)
  add_test(NAME NativeSkinTexturedShaderHardware COMMAND SkinTexturedShaderTests --hardware)
  add_test(NAME OriginalSkinTexturedInventory COMMAND "${Python3_EXECUTABLE}" -B
    "${CMAKE_SOURCE_DIR}/tests/test_skin_textured_shader.py" -v)
  set_tests_properties(NativeSkinTexturedShaderWARP NativeSkinTexturedShaderHardware PROPERTIES TIMEOUT 60)
  add_executable(SkinTexturedMaterialTests tests/test_skin_textured_material.cpp)
  target_compile_options(SkinTexturedMaterialTests PRIVATE /fp:strict /W4 /WX)
  target_link_libraries(SkinTexturedMaterialTests PRIVATE SimpsonsRuntime)
  add_test(NAME OriginalSkinTexturedMaterial COMMAND SkinTexturedMaterialTests "${CMAKE_SOURCE_DIR}/analysis/simpsons.pe")
  add_executable(SkinPassTests tests/test_skin_pass.cpp)
  target_compile_options(SkinPassTests PRIVATE /fp:strict /W4 /WX)
  target_link_libraries(SkinPassTests PRIVATE SimpsonsRuntime)
  target_link_options(SkinPassTests PRIVATE /STACK:67108864,1048576)
  foreach(FAMILY textured base dual)
    add_test(NAME OriginalSkin${FAMILY}Pass COMMAND SkinPassTests "${CMAKE_SOURCE_DIR}/analysis/simpsons.pe"
      "${CMAKE_SOURCE_DIR}/build/itxd-palette/loc_split4.itxd" ${FAMILY})
    set_tests_properties(OriginalSkin${FAMILY}Pass PROPERTIES TIMEOUT 60 RUN_SERIAL TRUE)
    add_test(NAME OriginalSkin${FAMILY}PaddedPass COMMAND SkinPassTests "${CMAKE_SOURCE_DIR}/analysis/simpsons.pe"
      "${CMAKE_SOURCE_DIR}/build/itxd-palette/loc_split4.itxd" ${FAMILY} padded)
    set_tests_properties(OriginalSkin${FAMILY}PaddedPass PROPERTIES TIMEOUT 60 RUN_SERIAL TRUE)
    add_test(NAME OriginalSkin${FAMILY}InheritedPass COMMAND SkinPassTests "${CMAKE_SOURCE_DIR}/analysis/simpsons.pe"
      "${CMAKE_SOURCE_DIR}/build/itxd-palette/loc_split4.itxd" ${FAMILY} inherited)
    set_tests_properties(OriginalSkin${FAMILY}InheritedPass PROPERTIES TIMEOUT 60 RUN_SERIAL TRUE)
    add_test(NAME OriginalSkin${FAMILY}Groups65Pass COMMAND SkinPassTests "${CMAKE_SOURCE_DIR}/analysis/simpsons.pe"
      "${CMAKE_SOURCE_DIR}/build/itxd-palette/loc_split4.itxd" "${FAMILY}" groups65)
    set_tests_properties(OriginalSkin${FAMILY}Groups65Pass PROPERTIES TIMEOUT 60 RUN_SERIAL TRUE)
    add_test(NAME OriginalSkin${FAMILY}Submeshes65536Pass COMMAND SkinPassTests "${CMAKE_SOURCE_DIR}/analysis/simpsons.pe"
      "${CMAKE_SOURCE_DIR}/build/itxd-palette/loc_split4.itxd" "${FAMILY}" submeshes65536)
    set_tests_properties(OriginalSkin${FAMILY}Submeshes65536Pass PROPERTIES TIMEOUT 60 RUN_SERIAL TRUE)
  endforeach()
  set(SIMPSONS_SKIN_DUAL_ALPHA_FIXTURES "${SIMPSONS_SHADER_DIR}/skin_dualalpha_fixtures.h")
  add_custom_command(OUTPUT "${SIMPSONS_SKIN_DUAL_ALPHA_FIXTURES}"
    COMMAND "${Python3_EXECUTABLE}" -B "${CMAKE_SOURCE_DIR}/tests/test_skin_dualalpha_shader.py"
      --emit-cpp "${SIMPSONS_SKIN_DUAL_ALPHA_FIXTURES}"
    DEPENDS tests/test_skin_dualalpha_shader.py tools/analyze_skin_dualalpha_shader.py
      tools/analyze_skin_dualtextured_shader.py tools/analyze_skin_alpha_shader.py tools/skin_shader_emit.py
      tools/analyze_rigid_shader.py tools/analyze_screen_shaders.py tools/analyze_edge_shaders.py
      analysis/simpsons.pe VERBATIM)
  add_executable(SkinDualAlphaShaderTests tests/test_skin_dualalpha_gpu.cpp
    "${SIMPSONS_SKIN_DUAL_ALPHA_FIXTURES}" ${SIMPSONS_SKIN_DUAL_ALPHA_HEADERS})
  target_include_directories(SkinDualAlphaShaderTests PRIVATE "${CMAKE_SOURCE_DIR}" "${SIMPSONS_SHADER_DIR}")
  target_compile_options(SkinDualAlphaShaderTests PRIVATE /fp:strict /W4 /WX)
  target_link_libraries(SkinDualAlphaShaderTests PRIVATE d3d11 dxgi)
  add_test(NAME NativeSkinDualAlphaShaderWARP COMMAND SkinDualAlphaShaderTests)
  add_test(NAME NativeSkinDualAlphaShaderHardware COMMAND SkinDualAlphaShaderTests --hardware)
  add_test(NAME OriginalSkinDualAlphaInventory COMMAND "${Python3_EXECUTABLE}" -B
    "${CMAKE_SOURCE_DIR}/tests/test_skin_dualalpha_shader.py" -v)
  set_tests_properties(NativeSkinDualAlphaShaderWARP NativeSkinDualAlphaShaderHardware PROPERTIES TIMEOUT 60)
  target_include_directories(SkinShaderTests PRIVATE "${CMAKE_SOURCE_DIR}" "${SIMPSONS_SHADER_DIR}")
  target_compile_definitions(SkinShaderTests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
  target_compile_options(SkinShaderTests PRIVATE /fp:strict /W4 /WX)
  target_link_libraries(SkinShaderTests PRIVATE d3d11 dxgi)
  add_test(NAME NativeSkinShaderWARP COMMAND SkinShaderTests)
  add_test(NAME NativeSkinShaderHardware COMMAND SkinShaderTests --hardware)
  set_tests_properties(NativeSkinShaderWARP NativeSkinShaderHardware PROPERTIES TIMEOUT 60)
  add_executable(RigidMeshTests tests/test_rigid_mesh.cpp "${SIMPSONS_CHOCOLATE_UV_PROBE_HEADER}")
  add_executable(SkyMeshTests tests/test_sky_mesh.cpp)
  add_executable(SkinMeshTests tests/test_skin_mesh.cpp)
  target_compile_options(SkinMeshTests PRIVATE /fp:strict /W4 /WX)
  target_link_libraries(SkinMeshTests PRIVATE SimpsonsGraphics)
  add_test(NAME NativeSkinMeshWARP COMMAND SkinMeshTests "${CMAKE_SOURCE_DIR}/analysis/simpsons.pe")
  add_test(NAME NativeSkinMeshHardware COMMAND SkinMeshTests "${CMAKE_SOURCE_DIR}/analysis/simpsons.pe" --hardware)
  set_tests_properties(NativeSkinMeshWARP NativeSkinMeshHardware PROPERTIES TIMEOUT 60)
  target_compile_options(SkyMeshTests PRIVATE /fp:strict /W4 /WX)
  target_link_libraries(SkyMeshTests PRIVATE SimpsonsGraphics)
  add_test(NAME NativeSkyMeshWARP COMMAND SkyMeshTests "${CMAKE_SOURCE_DIR}/analysis/simpsons.pe")
  add_test(NAME NativeSkyMeshHardware COMMAND SkyMeshTests "${CMAKE_SOURCE_DIR}/analysis/simpsons.pe" --hardware)
  set_tests_properties(NativeSkyMeshWARP NativeSkyMeshHardware PROPERTIES TIMEOUT 60)
  add_executable(OriginalSkyPassTests tests/test_sky_pass.cpp)
  target_compile_options(OriginalSkyPassTests PRIVATE /fp:strict /W4 /WX)
  target_link_libraries(OriginalSkyPassTests PRIVATE SimpsonsRuntime)
  target_link_options(OriginalSkyPassTests PRIVATE /STACK:67108864,1048576)
  add_test(NAME OriginalSkyPass COMMAND OriginalSkyPassTests "${CMAKE_SOURCE_DIR}/analysis/simpsons.pe")
  set_tests_properties(OriginalSkyPass PROPERTIES RUN_SERIAL TRUE)
  add_test(NAME OriginalSkyPaddedPass COMMAND OriginalSkyPassTests "${CMAKE_SOURCE_DIR}/analysis/simpsons.pe" padded)
  set_tests_properties(OriginalSkyPaddedPass PROPERTIES TIMEOUT 60 RUN_SERIAL TRUE)
  add_test(NAME OriginalVertexStrideContract COMMAND "${Python3_EXECUTABLE}" -B "${CMAKE_SOURCE_DIR}/tools/analyze_vertex_stride_contract.py")
  add_test(NAME OriginalSkinBlendContract COMMAND "${Python3_EXECUTABLE}" -B "${CMAKE_SOURCE_DIR}/tools/analyze_skin_blend_contract.py")
  add_test(NAME OriginalFallbackBlendContract COMMAND "${Python3_EXECUTABLE}" -B "${CMAKE_SOURCE_DIR}/tools/analyze_fallback_blend_contract.py")
  add_test(NAME OriginalSubmeshBaseContract COMMAND "${Python3_EXECUTABLE}" -B "${CMAKE_SOURCE_DIR}/tools/analyze_submesh_base_contract.py")
  add_executable(R16IndexValidationTests tests/test_r16_index_validation.cpp)
  target_compile_options(R16IndexValidationTests PRIVATE /W4 /WX)
  target_include_directories(R16IndexValidationTests PRIVATE "${CMAKE_SOURCE_DIR}")
  add_test(NAME NativeR16IndexValidation COMMAND R16IndexValidationTests)
  add_executable(OriginalSkyBasePassTests tests/test_sky_base_pass.cpp)
  target_compile_options(OriginalSkyBasePassTests PRIVATE /fp:strict /W4 /WX)
  target_link_libraries(OriginalSkyBasePassTests PRIVATE SimpsonsRuntime)
  target_link_options(OriginalSkyBasePassTests PRIVATE /STACK:67108864,1048576)
  foreach(BASE_CASE positive negative selected)
    add_test(NAME OriginalSkyBase_${BASE_CASE} COMMAND OriginalSkyBasePassTests "${CMAKE_SOURCE_DIR}/analysis/simpsons.pe" ${BASE_CASE})
    set_tests_properties(OriginalSkyBase_${BASE_CASE} PROPERTIES TIMEOUT 60 RUN_SERIAL TRUE)
  endforeach()
  add_executable(OriginalSkyInheritedPassTests tests/test_sky_inherited_pass.cpp)
  target_compile_options(OriginalSkyInheritedPassTests PRIVATE /fp:strict /W4 /WX)
  target_link_libraries(OriginalSkyInheritedPassTests PRIVATE SimpsonsRuntime)
  target_link_options(OriginalSkyInheritedPassTests PRIVATE /STACK:67108864,1048576)
  add_test(NAME OriginalSkyInheritedPass COMMAND OriginalSkyInheritedPassTests "${CMAKE_SOURCE_DIR}/analysis/simpsons.pe")
  set_tests_properties(OriginalSkyInheritedPass PROPERTIES TIMEOUT 60 RUN_SERIAL TRUE)
  add_executable(OriginalRigidInheritedPassTests tests/test_rigid_inherited_pass.cpp)
  target_compile_options(OriginalRigidInheritedPassTests PRIVATE /fp:strict /W4 /WX)
  target_link_libraries(OriginalRigidInheritedPassTests PRIVATE SimpsonsRuntime)
  target_link_options(OriginalRigidInheritedPassTests PRIVATE /STACK:67108864,1048576)
  foreach(FAMILY base textured dual gloss multitone normalmap)
    add_test(NAME OriginalRigidInherited_${FAMILY} COMMAND OriginalRigidInheritedPassTests "${CMAKE_SOURCE_DIR}/analysis/simpsons.pe"
      "${CMAKE_SOURCE_DIR}/build/itxd-palette/loc_split4.itxd" ${FAMILY})
    set_tests_properties(OriginalRigidInherited_${FAMILY} PROPERTIES TIMEOUT 60 RUN_SERIAL TRUE)
  endforeach()
  add_executable(OriginalRigidSubmeshes65536PassTests tests/test_rigid_submeshes_pass.cpp)
  target_compile_options(OriginalRigidSubmeshes65536PassTests PRIVATE /fp:strict /W4 /WX)
  target_link_libraries(OriginalRigidSubmeshes65536PassTests PRIVATE SimpsonsRuntime)
  target_link_options(OriginalRigidSubmeshes65536PassTests PRIVATE /STACK:67108864,1048576)
  add_test(NAME OriginalRigidTexturedSubmeshes65536Pass COMMAND OriginalRigidSubmeshes65536PassTests
      "${CMAKE_SOURCE_DIR}/analysis/simpsons.pe" "${CMAKE_SOURCE_DIR}/build/itxd-palette/loc_split4.itxd" textured)
  set_tests_properties(OriginalRigidTexturedSubmeshes65536Pass PROPERTIES TIMEOUT 60 RUN_SERIAL TRUE)
  foreach(FAMILY base dual gloss multitone normalmap)
    add_test(NAME OriginalRigid${FAMILY}Submeshes65536Pass COMMAND OriginalRigidSubmeshes65536PassTests
      "${CMAKE_SOURCE_DIR}/analysis/simpsons.pe" "${CMAKE_SOURCE_DIR}/build/itxd-palette/loc_split4.itxd" ${FAMILY})
    set_tests_properties(OriginalRigid${FAMILY}Submeshes65536Pass PROPERTIES TIMEOUT 60 RUN_SERIAL TRUE)
  endforeach()
  add_executable(OriginalRigidZeroSubmeshesPassTests tests/test_rigid_zero_submeshes_pass.cpp)
  target_compile_options(OriginalRigidZeroSubmeshesPassTests PRIVATE /fp:strict /W4 /WX)
  target_link_libraries(OriginalRigidZeroSubmeshesPassTests PRIVATE SimpsonsRuntime)
  target_link_options(OriginalRigidZeroSubmeshesPassTests PRIVATE /STACK:67108864,1048576)
  foreach(ALPHA opaque alpha)
      add_test(NAME OriginalRigidZeroSubmeshes_${ALPHA} COMMAND OriginalRigidZeroSubmeshesPassTests
          "${CMAKE_SOURCE_DIR}/analysis/simpsons.pe" "${CMAKE_SOURCE_DIR}/build/itxd-palette/loc_split4.itxd" textured ${ALPHA})
      set_tests_properties(OriginalRigidZeroSubmeshes_${ALPHA} PROPERTIES TIMEOUT 120 RUN_SERIAL TRUE)
  endforeach()
  foreach(ZERO_ALPHA opaque alpha)
      foreach(ZERO_UNUSED_FIELD row-null row-unmapped collection-null collection-unmapped)
          add_test(NAME OriginalRigidZeroSubmeshes_${ZERO_ALPHA}_${ZERO_UNUSED_FIELD}
              COMMAND OriginalRigidZeroSubmeshesPassTests
                  "${CMAKE_SOURCE_DIR}/analysis/simpsons.pe"
                  "${CMAKE_SOURCE_DIR}/build/itxd-palette/loc_split4.itxd"
                  textured ${ZERO_ALPHA} ${ZERO_UNUSED_FIELD})
          set_tests_properties(OriginalRigidZeroSubmeshes_${ZERO_ALPHA}_${ZERO_UNUSED_FIELD}
              PROPERTIES TIMEOUT 120 RUN_SERIAL TRUE)
      endforeach()
  endforeach()
  add_executable(OriginalRigidFirstCapturePassTests tests/test_rigid_first_capture_pass.cpp)
  target_compile_options(OriginalRigidFirstCapturePassTests PRIVATE /fp:strict /W4 /WX)
  target_link_libraries(OriginalRigidFirstCapturePassTests PRIVATE SimpsonsRuntime)
  target_link_options(OriginalRigidFirstCapturePassTests PRIVATE /STACK:67108864,1048576)
  foreach(PHASE opaque alpha)
    add_test(NAME OriginalRigidFirstCapture_${PHASE} COMMAND OriginalRigidFirstCapturePassTests
      "${CMAKE_SOURCE_DIR}/analysis/simpsons.pe"
      "${CMAKE_SOURCE_DIR}/build/itxd-palette/loc_split4.itxd" textured ${PHASE}
      "${CMAKE_BINARY_DIR}/rigid-first-capture-evidence")
    set_tests_properties(OriginalRigidFirstCapture_${PHASE} PROPERTIES TIMEOUT 120 RUN_SERIAL TRUE)
  endforeach()
  # Contained diagnostic outcomes, each a fresh process: a valid original table
  # above the 64 MiB capture budget, and a real directory blocking one output name.
  foreach(CAPTURE_CASE "BudgetSkip:budget" "RawIoFailure:io-raw" "DrawIoFailure:io-draw")
    string(REPLACE ":" ";" CAPTURE_PARTS "${CAPTURE_CASE}")
    list(GET CAPTURE_PARTS 0 CAPTURE_NAME)
    list(GET CAPTURE_PARTS 1 CAPTURE_MODE)
    foreach(PHASE opaque alpha)
      add_test(NAME OriginalRigidCapture${CAPTURE_NAME}_${PHASE} COMMAND OriginalRigidFirstCapturePassTests
        "${CMAKE_SOURCE_DIR}/analysis/simpsons.pe"
        "${CMAKE_SOURCE_DIR}/build/itxd-palette/loc_split4.itxd" textured ${PHASE}
        "${CMAKE_BINARY_DIR}/rigid-first-capture-evidence" ${CAPTURE_MODE})
      set_tests_properties(OriginalRigidCapture${CAPTURE_NAME}_${PHASE} PROPERTIES TIMEOUT 300 RUN_SERIAL TRUE)
    endforeach()
  endforeach()
  add_executable(RigidMaterialConstantsTests tests/test_rigid_material_constants.cpp)
  target_compile_options(RigidMaterialConstantsTests PRIVATE /fp:strict /W4 /WX)
  target_link_libraries(RigidMaterialConstantsTests PRIVATE SimpsonsGraphics)
  add_test(NAME OriginalRigidMaterialConstants COMMAND RigidMaterialConstantsTests "${CMAKE_SOURCE_DIR}/analysis/simpsons.pe")
  target_include_directories(RigidMeshTests PRIVATE "${SIMPSONS_SHADER_DIR}")
  target_compile_options(RigidMeshTests PRIVATE /fp:strict /W4 /WX)
  target_link_libraries(RigidMeshTests PRIVATE SimpsonsGraphics)
  add_test(NAME NativeRigidMeshWARP COMMAND RigidMeshTests "${CMAKE_SOURCE_DIR}/analysis/simpsons.pe")
  add_test(NAME NativeRigidMeshHardware COMMAND RigidMeshTests "${CMAKE_SOURCE_DIR}/analysis/simpsons.pe" --hardware)
  set_tests_properties(NativeRigidMeshWARP NativeRigidMeshHardware PROPERTIES TIMEOUT 60)
  target_include_directories(RigidShaderTests PRIVATE "${SIMPSONS_SHADER_DIR}")
  target_compile_definitions(RigidShaderTests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
  target_compile_options(RigidShaderTests PRIVATE /fp:strict /W4 /WX)
  target_link_libraries(RigidShaderTests PRIVATE d3d11 dxgi)
  add_test(NAME NativeRigidShaderWARP COMMAND RigidShaderTests)
  add_test(NAME NativeRigidShaderHardware COMMAND RigidShaderTests --hardware)
  add_test(NAME OriginalRigidAluEmission COMMAND "${Python3_EXECUTABLE}" -B "${CMAKE_SOURCE_DIR}/tests/test_rigid_alu_emission.py" -v)
  add_test(NAME OriginalRigidGlossShaderEvidence COMMAND "${Python3_EXECUTABLE}" -B "${CMAKE_SOURCE_DIR}/tools/analyze_rigid_gloss_shader.py" --verify)
  add_test(NAME OriginalRigidMultitoneInventory COMMAND "${Python3_EXECUTABLE}" -B "${CMAKE_SOURCE_DIR}/tests/test_rigid_multitone_shader.py" -v)
  add_test(NAME OriginalRigidNormalmapInventory COMMAND "${Python3_EXECUTABLE}" -B "${CMAKE_SOURCE_DIR}/tests/test_rigid_normalmap_shader.py" -v)
  set(SIMPSONS_RIGID_UV_FIXTURES "${SIMPSONS_SHADER_DIR}/rigid_dualtextured_uv_fixtures.h")
  add_custom_command(OUTPUT "${SIMPSONS_RIGID_UV_FIXTURES}"
    COMMAND "${Python3_EXECUTABLE}" -B "${CMAKE_SOURCE_DIR}/tests/test_rigid_dualtextured_uv_shader.py"
      --emit-fixtures "${SIMPSONS_RIGID_UV_FIXTURES}"
    DEPENDS tests/test_rigid_dualtextured_uv_shader.py tools/analyze_rigid_dualtextured_uv_shader.py
      tools/analyze_chocolate_shader.py tools/analyze_rigid_shader.py tools/analyze_screen_shaders.py
      tools/analyze_edge_shaders.py analysis/simpsons.pe VERBATIM)
  add_executable(RigidDualTexturedUVShaderTests tests/test_rigid_dualtextured_uv_gpu.cpp
    "${SIMPSONS_RIGID_UV_FIXTURES}" ${SIMPSONS_RIGID_HEADERS})
  target_include_directories(RigidDualTexturedUVShaderTests PRIVATE "${SIMPSONS_SHADER_DIR}")
  target_compile_options(RigidDualTexturedUVShaderTests PRIVATE /fp:strict /W4 /WX)
  target_link_libraries(RigidDualTexturedUVShaderTests PRIVATE d3d11 dxgi)
  add_test(NAME OriginalRigidDualTexturedUVInventory COMMAND "${Python3_EXECUTABLE}" -B
    "${CMAKE_SOURCE_DIR}/tests/test_rigid_dualtextured_uv_shader.py" -v)
  add_test(NAME NativeRigidDualTexturedUVShaderWARP COMMAND RigidDualTexturedUVShaderTests)
  add_test(NAME NativeRigidDualTexturedUVShaderHardware COMMAND RigidDualTexturedUVShaderTests --hardware)
  set_tests_properties(NativeRigidDualTexturedUVShaderWARP NativeRigidDualTexturedUVShaderHardware PROPERTIES TIMEOUT 60)
  set(SIMPSONS_RIGID_SINGLE_UV_FIXTURES "${SIMPSONS_SHADER_DIR}/rigid_uv_fixtures.h")
  add_custom_command(OUTPUT "${SIMPSONS_RIGID_SINGLE_UV_FIXTURES}"
    COMMAND "${Python3_EXECUTABLE}" -B "${CMAKE_SOURCE_DIR}/tests/test_rigid_uv_shader.py"
      --emit-fixtures "${SIMPSONS_RIGID_SINGLE_UV_FIXTURES}"
    DEPENDS tests/test_rigid_uv_shader.py tools/analyze_rigid_uv_shader.py
      tools/analyze_chocolate_shader.py tools/analyze_rigid_shader.py tools/analyze_screen_shaders.py
      tools/analyze_edge_shaders.py tools/analyze_post_effect_catalog.py tools/analyze_effect_catalog.py
      analysis/simpsons.pe VERBATIM)
  add_executable(RigidUVShaderTests tests/test_rigid_uv_gpu.cpp
    "${SIMPSONS_RIGID_SINGLE_UV_FIXTURES}" ${SIMPSONS_RIGID_SINGLE_UV_HEADERS})
  target_include_directories(RigidUVShaderTests PRIVATE "${SIMPSONS_SHADER_DIR}")
  target_compile_options(RigidUVShaderTests PRIVATE /fp:strict /W4 /WX)
  target_link_libraries(RigidUVShaderTests PRIVATE d3d11 dxgi)
  add_test(NAME OriginalRigidUVShader COMMAND "${Python3_EXECUTABLE}" -B
    "${CMAKE_SOURCE_DIR}/tests/test_rigid_uv_shader.py")
  add_test(NAME NativeRigidUVShaderWARP COMMAND RigidUVShaderTests)
  add_test(NAME NativeRigidUVShaderHardware COMMAND RigidUVShaderTests --hardware)
  set_tests_properties(OriginalRigidUVShader NativeRigidUVShaderWARP NativeRigidUVShaderHardware PROPERTIES TIMEOUT 60)
  set(SIMPSONS_FLIPBOOK_FIXTURES "${SIMPSONS_SHADER_DIR}/flipbook_fixtures.h")
  add_custom_command(OUTPUT "${SIMPSONS_FLIPBOOK_FIXTURES}"
    COMMAND "${Python3_EXECUTABLE}" -B "${CMAKE_SOURCE_DIR}/tests/test_flipbook_shader.py"
      --emit-fixtures "${SIMPSONS_FLIPBOOK_FIXTURES}"
    DEPENDS tests/test_flipbook_shader.py tools/analyze_flipbook_shader.py
      tools/analyze_chocolate_shader.py tools/analyze_rigid_shader.py tools/analyze_screen_shaders.py
      tools/analyze_edge_shaders.py tools/analyze_post_effect_catalog.py tools/analyze_effect_catalog.py
      analysis/simpsons.pe VERBATIM)
  add_executable(FlipbookShaderTests tests/test_flipbook_gpu.cpp
    "${SIMPSONS_FLIPBOOK_FIXTURES}" ${SIMPSONS_FLIPBOOK_HEADERS})
  target_include_directories(FlipbookShaderTests PRIVATE "${SIMPSONS_SHADER_DIR}")
  target_compile_options(FlipbookShaderTests PRIVATE /fp:strict /W4 /WX)
  target_link_libraries(FlipbookShaderTests PRIVATE d3d11 dxgi)
  add_test(NAME OriginalFlipbookShader COMMAND "${Python3_EXECUTABLE}" -B
    "${CMAKE_SOURCE_DIR}/tests/test_flipbook_shader.py")
  add_test(NAME NativeFlipbookShaderWARP COMMAND FlipbookShaderTests)
  add_test(NAME NativeFlipbookShaderHardware COMMAND FlipbookShaderTests --hardware)
  set_tests_properties(OriginalFlipbookShader NativeFlipbookShaderWARP NativeFlipbookShaderHardware PROPERTIES TIMEOUT 60)
  set(SIMPSONS_RIGID_FAMILY_ALPHA_FIXTURES "${SIMPSONS_SHADER_DIR}/rigid_family_alpha_fixtures.h")
  add_custom_command(OUTPUT "${SIMPSONS_RIGID_FAMILY_ALPHA_FIXTURES}"
    COMMAND "${Python3_EXECUTABLE}" -B "${CMAKE_SOURCE_DIR}/tests/test_rigid_family_alpha_shader.py"
      --emit-fixtures "${SIMPSONS_RIGID_FAMILY_ALPHA_FIXTURES}"
    DEPENDS tests/test_rigid_family_alpha_shader.py tools/analyze_rigid_family_alpha_shader.py
      tools/analyze_rigid_shader.py tools/analyze_168f8alpha_shader.py tools/analyze_rigid_dualtextured_uv_shader.py
      tools/analyze_chocolate_shader.py tools/analyze_edge_shaders.py tools/analyze_screen_shaders.py
      tools/analyze_post_effect_catalog.py tools/analyze_effect_catalog.py tools/analyze_fourtap_shaders.py
      ${SIMPSONS_RIGID_FAMILY_ALPHA_REFERENCES} analysis/simpsons.pe VERBATIM)
  add_executable(RigidFamilyAlphaShaderTests tests/test_rigid_family_alpha_gpu.cpp
    "${SIMPSONS_RIGID_FAMILY_ALPHA_FIXTURES}" ${SIMPSONS_RIGID_FAMILY_ALPHA_HEADERS}
    "${SIMPSONS_SHADER_DIR}/VS168F8.h")
  target_include_directories(RigidFamilyAlphaShaderTests PRIVATE "${SIMPSONS_SHADER_DIR}")
  target_compile_options(RigidFamilyAlphaShaderTests PRIVATE /fp:strict /W4 /WX)
  target_link_libraries(RigidFamilyAlphaShaderTests PRIVATE d3d11 dxgi)
  add_test(NAME OriginalRigidFamilyAlphaInventory COMMAND "${Python3_EXECUTABLE}" -B
    "${CMAKE_SOURCE_DIR}/tests/test_rigid_family_alpha_shader.py" -v)
  add_test(NAME NativeRigidFamilyAlphaShaderWARP COMMAND RigidFamilyAlphaShaderTests)
  add_test(NAME NativeRigidFamilyAlphaShaderHardware COMMAND RigidFamilyAlphaShaderTests --hardware)
  set_tests_properties(NativeRigidFamilyAlphaShaderWARP NativeRigidFamilyAlphaShaderHardware PROPERTIES TIMEOUT 60)
  add_test(NAME OriginalSkinInventory COMMAND "${Python3_EXECUTABLE}" -B
    "${CMAKE_SOURCE_DIR}/tests/test_skin_shader.py" -v)
  add_test(NAME OriginalSkinDualInventory COMMAND "${Python3_EXECUTABLE}" -B
    "${CMAKE_SOURCE_DIR}/tests/test_skin_dualtextured_shader.py" -v)
  add_test(NAME OriginalSkyInventory COMMAND "${Python3_EXECUTABLE}" -B
    "${CMAKE_SOURCE_DIR}/tests/test_sky_shader.py" -v)
  add_test(NAME OriginalChocolateInventory COMMAND "${Python3_EXECUTABLE}" -B
    "${CMAKE_SOURCE_DIR}/tests/test_chocolate_shader.py" -v)
  add_test(NAME OriginalParticleInventory COMMAND "${Python3_EXECUTABLE}" -B
    "${CMAKE_SOURCE_DIR}/tests/test_particle_shader.py" -v)
  add_test(NAME OriginalParticleType5Inventory COMMAND "${Python3_EXECUTABLE}" -B
    "${CMAKE_SOURCE_DIR}/tests/test_particle_type5_shader.py" -v)
  add_test(NAME NativeParticleBackend COMMAND ParticleBackendTests)
  add_test(NAME NativeParticleBackendHardware COMMAND ParticleBackendTests --hardware)
  add_test(NAME OriginalProjectedParticleInventory COMMAND "${Python3_EXECUTABLE}" -B
    "${CMAKE_SOURCE_DIR}/tests/test_projected_particle_shader.py" -v)
  add_test(NAME OriginalDualParticleInventory COMMAND "${Python3_EXECUTABLE}" -B
    "${CMAKE_SOURCE_DIR}/tests/test_dual_particle_shader.py" -v)
  add_test(NAME OriginalDualProjectedParticleInventory COMMAND "${Python3_EXECUTABLE}" -B
    "${CMAKE_SOURCE_DIR}/tests/test_dual_projected_particle_shader.py" -v)
  add_test(NAME OriginalParticleNullProjector COMMAND "${Python3_EXECUTABLE}" -B
    "${CMAKE_SOURCE_DIR}/tests/test_particle_null_projector.py" -v)
  add_test(NAME OriginalAudioRuntimeCoverageAudit COMMAND "${Python3_EXECUTABLE}" -B
    "${CMAKE_SOURCE_DIR}/tests/test_audio_runtime_audit.py" -v)
  add_executable(OriginalParticleDispatchTests tests/test_particle_original_dispatch.cpp)
  target_compile_options(OriginalParticleDispatchTests PRIVATE /fp:strict /W4 /WX)
  target_link_libraries(OriginalParticleDispatchTests PRIVATE SimpsonsRuntime)
  target_link_options(OriginalParticleDispatchTests PRIVATE /STACK:67108864,1048576)
  add_test(NAME OriginalParticleDispatch COMMAND OriginalParticleDispatchTests "${CMAKE_SOURCE_DIR}/analysis/simpsons.pe"
    "${CMAKE_SOURCE_DIR}/build/itxd-palette/loc_split4.itxd")
  set_tests_properties(OriginalParticleDispatch PROPERTIES RUN_SERIAL TRUE)
  add_test(NAME NativeParticleFamilyShaderWARP COMMAND ParticleFamilyShaderTests)
  add_test(NAME NativeParticleFamilyShaderHardware COMMAND ParticleFamilyShaderTests --hardware)
  set_tests_properties(NativeParticleFamilyShaderWARP NativeParticleFamilyShaderHardware PROPERTIES TIMEOUT 60)
  add_test(NAME NativeProjectedParticleShaderWARP COMMAND ParticleProjectedShaderTests)
  add_test(NAME NativeProjectedParticleShaderHardware COMMAND ParticleProjectedShaderTests --hardware)
  set_tests_properties(NativeProjectedParticleShaderWARP NativeProjectedParticleShaderHardware PROPERTIES TIMEOUT 60)
  add_test(NAME Original168F8AlphaInventory COMMAND "${Python3_EXECUTABLE}" -B
    "${CMAKE_SOURCE_DIR}/tests/test_168f8alpha_shader.py" -v)
  add_test(NAME OriginalRigidShaderEvidence COMMAND "${Python3_EXECUTABLE}" -B "${CMAKE_SOURCE_DIR}/tools/analyze_rigid_shader.py" --verify --self-test)
  add_test(NAME OriginalRigidTexturedShaderEvidence COMMAND "${Python3_EXECUTABLE}" -B "${CMAKE_SOURCE_DIR}/tools/analyze_rigid_textured_shader.py" --verify --self-test)
  set_tests_properties(NativeRigidShaderWARP NativeRigidShaderHardware PROPERTIES TIMEOUT 60)
  target_include_directories(ZPrepassShaderTests PRIVATE "${SIMPSONS_SHADER_DIR}")
  target_compile_definitions(ZPrepassShaderTests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
  target_compile_options(ZPrepassShaderTests PRIVATE /fp:strict /W4 /WX)
  target_link_libraries(ZPrepassShaderTests PRIVATE d3d11 dxgi)
  add_test(NAME NativeZPrepassShaderWARP COMMAND ZPrepassShaderTests)
  add_test(NAME NativeZPrepassShaderHardware COMMAND ZPrepassShaderTests --hardware)
  add_test(NAME OriginalZPrepassShaderEvidence COMMAND "${Python3_EXECUTABLE}" -B "${CMAKE_SOURCE_DIR}/tools/analyze_zprepass_shader.py" --verify --self-test)
  set_tests_properties(NativeZPrepassShaderWARP NativeZPrepassShaderHardware PROPERTIES TIMEOUT 60)
  target_compile_options(ShadowMeshTests PRIVATE /fp:strict /W4 /WX)
  target_link_libraries(ShadowMeshTests PRIVATE SimpsonsGraphics)
  target_include_directories(ShadowMeshTests PRIVATE "${SIMPSONS_SHADER_DIR}")
  add_test(NAME NativeShadowMeshWARP COMMAND ShadowMeshTests "${CMAKE_SOURCE_DIR}/analysis/simpsons.pe")
  add_test(NAME NativeShadowMeshHardware COMMAND ShadowMeshTests "${CMAKE_SOURCE_DIR}/analysis/simpsons.pe" --hardware)
  set_tests_properties(NativeShadowMeshWARP NativeShadowMeshHardware PROPERTIES TIMEOUT 60)
  add_test(NAME NativeGameLauncher COMMAND SimpsonsLauncher --self-test)
  set_tests_properties(NativeGameLauncher PROPERTIES TIMEOUT 15)
  add_test(NAME AuditStageRouteTools COMMAND "${Python3_EXECUTABLE}" -B -m unittest discover
    -s "${CMAKE_SOURCE_DIR}/tests" -p test_audit_stage_routes.py -v)
  set_tests_properties(AuditStageRouteTools PROPERTIES TIMEOUT 60)
  add_executable(OriginalLoopRouteTests tests/test_original_loop_routes.cpp)
  target_compile_options(OriginalLoopRouteTests PRIVATE /fp:strict /W4 /WX)
  target_link_libraries(OriginalLoopRouteTests PRIVATE SimpsonsRuntime)
  target_link_options(OriginalLoopRouteTests PRIVATE /STACK:67108864,1048576)
  add_test(NAME OriginalLoopRoutes COMMAND OriginalLoopRouteTests "${CMAKE_SOURCE_DIR}/analysis/simpsons.pe")
  foreach(loop_route_case 0 1)
    add_test(NAME OriginalLoopRouteCase${loop_route_case} COMMAND OriginalLoopRouteTests
      "${CMAKE_SOURCE_DIR}/analysis/simpsons.pe" ${loop_route_case})
  endforeach()
  set_tests_properties(OriginalLoopRoutes OriginalLoopRouteCase0 OriginalLoopRouteCase1 PROPERTIES TIMEOUT 60 RUN_SERIAL TRUE)
  add_executable(FirstMissionCompletionTests tests/test_first_mission_completion.cpp)
  target_compile_options(FirstMissionCompletionTests PRIVATE /fp:strict /W4 /WX)
  target_link_libraries(FirstMissionCompletionTests PRIVATE SimpsonsRuntime)
  target_link_options(FirstMissionCompletionTests PRIVATE /STACK:67108864,1048576)
  add_test(NAME OriginalFirstMissionCompletion COMMAND FirstMissionCompletionTests "${CMAKE_SOURCE_DIR}/analysis/simpsons.pe")
  set_tests_properties(OriginalFirstMissionCompletion PROPERTIES TIMEOUT 30)
  foreach(STAGE spr_hub loc brt eighty_bites tree_hugger mob_rules cheater dayofthedolphins colossaldonut bargainbin bigsuperhappy dayspringfieldstoodstill gamehub grand_theft_scratchy medal_of_homer meetthyplayer neverquest rhymes)
    add_test(NAME OriginalAuditStage_${STAGE} COMMAND FirstMissionCompletionTests "${CMAKE_SOURCE_DIR}/analysis/simpsons.pe" "${STAGE}")
    set_tests_properties(OriginalAuditStage_${STAGE} PROPERTIES TIMEOUT 30)
  endforeach()
  add_executable(MovieStateTests tests/test_movie_state.cpp)
  target_compile_options(MovieStateTests PRIVATE /fp:strict /W4 /WX)
  target_link_libraries(MovieStateTests PRIVATE SimpsonsGraphics)
  add_test(NAME OriginalMovieState COMMAND MovieStateTests)
  add_executable(MovieGeometryTests tests/test_movie_geometry.cpp)
  target_compile_options(MovieGeometryTests PRIVATE /fp:strict /W4 /WX)
  target_link_libraries(MovieGeometryTests PRIVATE SimpsonsGraphics)
  add_test(NAME OriginalMovieGeometry COMMAND MovieGeometryTests "${CMAKE_SOURCE_DIR}/analysis/simpsons.pe")
  add_test(NAME OriginalMovieShaderEvidence COMMAND "${Python3_EXECUTABLE}" -B -m unittest discover
    -s "${CMAKE_SOURCE_DIR}/tests" -p test_movie_shader_evidence.py -v)
  set_tests_properties(OriginalMovieState OriginalMovieGeometry OriginalMovieShaderEvidence PROPERTIES TIMEOUT 45)
  add_executable(MovieBackendTests tests/test_movie_backend.cpp)
  target_compile_options(MovieBackendTests PRIVATE /fp:strict /W4 /WX)
  target_link_libraries(MovieBackendTests PRIVATE SimpsonsGraphics)
  add_test(NAME NativeMovieBackendWARP COMMAND MovieBackendTests)
  add_test(NAME NativeMovieBackendHardware COMMAND MovieBackendTests --hardware)
  set_tests_properties(NativeMovieBackendWARP NativeMovieBackendHardware PROPERTIES TIMEOUT 60)
  add_executable(ContentEnumerationTests tests/test_content_enumeration.cpp)
  target_compile_options(ContentEnumerationTests PRIVATE /fp:strict /W4 /WX)
  target_link_libraries(ContentEnumerationTests PRIVATE SimpsonsRuntime)
  target_link_options(ContentEnumerationTests PRIVATE /STACK:67108864,1048576)
  add_test(NAME OriginalContentEnumeration COMMAND ContentEnumerationTests "${CMAKE_SOURCE_DIR}/analysis/simpsons.pe")
  set_tests_properties(OriginalContentEnumeration PROPERTIES TIMEOUT 60)
  add_executable(NativeSaveStoreTests tests/test_native_saves.cpp)
  target_compile_options(NativeSaveStoreTests PRIVATE /fp:strict /W4 /WX)
  target_link_libraries(NativeSaveStoreTests PRIVATE SimpsonsRuntime)
  add_test(NAME NativeSaveStore COMMAND NativeSaveStoreTests)
  set_tests_properties(NativeSaveStore PROPERTIES TIMEOUT 30)
  add_executable(NativeSaveBridgeTests tests/test_save_bridge.cpp)
  target_compile_options(NativeSaveBridgeTests PRIVATE /fp:strict /W4 /WX)
  target_link_libraries(NativeSaveBridgeTests PRIVATE SimpsonsRuntime)
  target_link_options(NativeSaveBridgeTests PRIVATE /STACK:67108864,1048576)
  add_test(NAME OriginalNativeSaveBridge COMMAND NativeSaveBridgeTests "${CMAKE_SOURCE_DIR}/analysis/simpsons.pe")
  set_tests_properties(OriginalNativeSaveBridge PROPERTIES TIMEOUT 30)
  add_executable(NativeControllerTests tests/test_native_controllers.cpp)
  add_executable(NativeInputPromptTests tests/test_native_input_prompts.cpp)
  target_compile_options(NativeInputPromptTests PRIVATE /fp:strict /W4 /WX)
  target_link_libraries(NativeInputPromptTests PRIVATE SimpsonsGraphics)
  add_test(NAME NativeInputPromptsWARP COMMAND NativeInputPromptTests)
  add_test(NAME NativeInputPromptsHardware COMMAND NativeInputPromptTests --hardware)
  add_test(NAME NativeInputPromptAssets COMMAND "${Python3_EXECUTABLE}" -B -m unittest discover -s "${CMAKE_SOURCE_DIR}/tests" -p test_prompt_icons.py)
  set_tests_properties(NativeInputPromptAssets PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
  target_compile_options(NativeControllerTests PRIVATE /fp:strict /W4 /WX)
  target_link_libraries(NativeControllerTests PRIVATE SimpsonsRuntime)
  target_link_options(NativeControllerTests PRIVATE /STACK:67108864,1048576)
  add_test(NAME OriginalNativeControllers COMMAND NativeControllerTests "${CMAKE_SOURCE_DIR}/analysis/simpsons.pe")
  add_test(NAME NativeCheckpointControllers COMMAND NativeControllerTests --checkpoint-only)
  add_test(NAME OriginalControllerMovieAction COMMAND NativeControllerTests --movie-action-only "${CMAKE_SOURCE_DIR}/analysis/simpsons.pe")
  set_tests_properties(OriginalNativeControllers OriginalControllerMovieAction PROPERTIES TIMEOUT 60)
  add_executable(NativeMenuNavigationTests tests/test_native_menu_navigation.cpp)
  target_compile_options(NativeMenuNavigationTests PRIVATE /fp:strict /W4 /WX)
  target_link_libraries(NativeMenuNavigationTests PRIVATE SimpsonsRuntime)
  target_link_options(NativeMenuNavigationTests PRIVATE /STACK:67108864,1048576)
  add_test(NAME OriginalNativeMenuNavigation COMMAND NativeMenuNavigationTests "${CMAKE_SOURCE_DIR}/analysis/simpsons.pe")
  set_tests_properties(OriginalNativeMenuNavigation PROPERTIES TIMEOUT 60)
  add_executable(NativeMenuMouseTests tests/test_native_menu_mouse.cpp)
  target_compile_options(NativeMenuMouseTests PRIVATE /fp:strict /W4 /WX)
  target_include_directories(NativeMenuMouseTests PRIVATE "${CMAKE_SOURCE_DIR}")
  add_test(NAME NativeMenuMouseMapping COMMAND NativeMenuMouseTests)
  add_test(NAME NativeMouseMenuActions COMMAND "${Python3_EXECUTABLE}" -B "${CMAKE_SOURCE_DIR}/tools/test_native_mouse_menu.py")
  add_executable(NativeAptHitTestTests tests/test_native_apt_hit_test.cpp)
  target_compile_options(NativeAptHitTestTests PRIVATE /fp:strict /W4 /WX)
  target_link_libraries(NativeAptHitTestTests PRIVATE SimpsonsRuntime)
  target_link_options(NativeAptHitTestTests PRIVATE /STACK:67108864,1048576)
  add_test(NAME OriginalAptPointHitTest COMMAND NativeAptHitTestTests "${CMAKE_SOURCE_DIR}/analysis/simpsons.pe")
  set_tests_properties(OriginalAptPointHitTest PROPERTIES TIMEOUT 60)
  add_executable(WorldTelemetryTests tests/test_world_telemetry.cpp)
  target_compile_options(WorldTelemetryTests PRIVATE /fp:strict /W4 /WX)
  target_link_libraries(WorldTelemetryTests PRIVATE SimpsonsRuntime)
  add_test(NAME OriginalWorldCameraTelemetry COMMAND WorldTelemetryTests)
  set_tests_properties(OriginalWorldCameraTelemetry PROPERTIES TIMEOUT 30)
  add_executable(SceneDepthTelemetryTests tests/test_scene_depth_telemetry.cpp runtime/scene_depth_telemetry.cpp)
  target_include_directories(SceneDepthTelemetryTests PRIVATE "${CMAKE_SOURCE_DIR}")
  target_compile_options(SceneDepthTelemetryTests PRIVATE /fp:strict /W4 /WX)
  add_test(NAME NativeSceneDepthTelemetry COMMAND SceneDepthTelemetryTests)
  add_executable(PlayerTelemetryTests tests/test_player_telemetry.cpp runtime/player_telemetry.cpp)
  target_include_directories(PlayerTelemetryTests PRIVATE "${CMAKE_SOURCE_DIR}")
  target_compile_options(PlayerTelemetryTests PRIVATE /fp:strict /W4 /WX)
  add_test(NAME OriginalPlayerFrameTelemetry COMMAND PlayerTelemetryTests)
  add_executable(NativeFrameRateTests tests/test_native_frame_rate.cpp)
  add_executable(NativeVideoSettingsTests tests/test_native_video_settings.cpp)
  add_executable(InternalResolutionTests tests/test_internal_resolution.cpp)
  target_compile_options(InternalResolutionTests PRIVATE /fp:strict /W4 /WX)
  target_link_libraries(InternalResolutionTests PRIVATE SimpsonsGraphics)
  add_test(NAME NativeInternalResolution COMMAND InternalResolutionTests)
  add_executable(AntialiasingTests tests/test_antialiasing.cpp)
  target_compile_options(AntialiasingTests PRIVATE /fp:strict /W4 /WX)
  target_link_libraries(AntialiasingTests PRIVATE SimpsonsGraphics)
  add_test(NAME NativeAntialiasing COMMAND AntialiasingTests)
  set_tests_properties(NativeAntialiasing PROPERTIES TIMEOUT 60)
  add_executable(UltrawideCameraTests tests/test_ultrawide_camera.cpp)
  target_compile_options(UltrawideCameraTests PRIVATE /fp:strict /W4 /WX)
  target_link_libraries(UltrawideCameraTests PRIVATE SimpsonsRuntime)
  target_link_options(UltrawideCameraTests PRIVATE /STACK:67108864,1048576)
  add_test(NAME OriginalUltrawideCamera COMMAND UltrawideCameraTests "${CMAKE_SOURCE_DIR}/analysis/simpsons.pe")
  set_tests_properties(OriginalUltrawideCamera PROPERTIES TIMEOUT 60)
  add_executable(UltrawideTests tests/test_ultrawide.cpp)
  target_compile_options(UltrawideTests PRIVATE /fp:strict /W4 /WX)
  target_link_libraries(UltrawideTests PRIVATE SimpsonsGraphics user32)
  add_test(NAME NativeUltrawide COMMAND UltrawideTests)
  set_tests_properties(NativeUltrawide PROPERTIES TIMEOUT 120)
  target_compile_options(NativeVideoSettingsTests PRIVATE /fp:strict /W4 /WX)
  target_link_libraries(NativeVideoSettingsTests PRIVATE SimpsonsRuntime)
  target_link_options(NativeVideoSettingsTests PRIVATE /STACK:67108864,1048576)
  add_test(NAME NativeVideoSettings COMMAND NativeVideoSettingsTests)
  set_tests_properties(NativeVideoSettings PROPERTIES TIMEOUT 30 RUN_SERIAL TRUE)
  add_test(NAME NativeVideoMenuAssets COMMAND "${Python3_EXECUTABLE}" -B "${CMAKE_SOURCE_DIR}/tools/test_native_video_menu.py")
  add_executable(NativeChainTraversalTests tests/test_guest_chain.cpp)
  target_include_directories(NativeChainTraversalTests PRIVATE "${CMAKE_SOURCE_DIR}")
  target_compile_options(NativeChainTraversalTests PRIVATE /W4 /WX)
  add_test(NAME NativeBoundedGuestChains COMMAND NativeChainTraversalTests)
  target_compile_options(NativeFrameRateTests PRIVATE /fp:strict /W4 /WX)
  target_link_libraries(NativeFrameRateTests PRIVATE SimpsonsRuntime)
  target_link_options(NativeFrameRateTests PRIVATE /STACK:67108864,1048576)
  add_test(NAME OriginalNativeFrameRate COMMAND NativeFrameRateTests "${CMAKE_SOURCE_DIR}/analysis/simpsons.pe")
  # Real-time pacing bounds: keep parallel test load from delaying its wakeups.
  set_tests_properties(OriginalNativeFrameRate PROPERTIES TIMEOUT 30 RUN_SERIAL TRUE)
  add_executable(NativeStorageSelectorTests tests/test_storage_selector.cpp)
  target_compile_options(NativeStorageSelectorTests PRIVATE /fp:strict /W4 /WX)
  target_link_libraries(NativeStorageSelectorTests PRIVATE SimpsonsRuntime)
  target_link_options(NativeStorageSelectorTests PRIVATE /STACK:67108864,1048576)
  add_test(NAME OriginalNativeStorageSelector COMMAND NativeStorageSelectorTests "${CMAKE_SOURCE_DIR}/analysis/simpsons.pe")
  set_tests_properties(OriginalNativeStorageSelector PROPERTIES TIMEOUT 60)
  foreach(storage_flag_pair IN ITEMS 0 1 2 3)
    add_test(NAME OriginalNativeStorageSelectorFlags${storage_flag_pair} COMMAND NativeStorageSelectorTests "${CMAKE_SOURCE_DIR}/analysis/simpsons.pe" --flags ${storage_flag_pair})
    set_tests_properties(OriginalNativeStorageSelectorFlags${storage_flag_pair} PROPERTIES TIMEOUT 60 RUN_SERIAL TRUE)
  endforeach()
  add_executable(ViewportCameraPassTests tests/test_viewport_camera_pass.cpp)
  target_compile_options(ViewportCameraPassTests PRIVATE /fp:strict /W4 /WX)
  target_link_libraries(ViewportCameraPassTests PRIVATE SimpsonsRuntime)
  target_link_options(ViewportCameraPassTests PRIVATE /STACK:67108864,1048576)
  add_test(NAME OriginalViewportCameraPasses COMMAND ViewportCameraPassTests "${CMAKE_SOURCE_DIR}/analysis/simpsons.pe")
  set_tests_properties(OriginalViewportCameraPasses PROPERTIES TIMEOUT 60)
  add_executable(MipTextureTests tests/test_mip_texture.cpp)
  add_executable(CrossfadeTextureTests tests/test_crossfade_texture.cpp)
  target_compile_options(CrossfadeTextureTests PRIVATE /fp:strict /W4 /WX)
  target_link_libraries(CrossfadeTextureTests PRIVATE SimpsonsRuntime)
  target_link_options(CrossfadeTextureTests PRIVATE /STACK:67108864,1048576)
  add_test(NAME OriginalCrossfadeTextureLifecycle COMMAND CrossfadeTextureTests "${CMAKE_SOURCE_DIR}/analysis/simpsons.pe")
  set_tests_properties(OriginalCrossfadeTextureLifecycle PROPERTIES TIMEOUT 60)
  add_executable(MoviePlaneTests tests/test_movie_planes.cpp)
  target_compile_options(MoviePlaneTests PRIVATE /fp:strict /W4 /WX)
  target_link_libraries(MoviePlaneTests PRIVATE SimpsonsRuntime)
  target_link_options(MoviePlaneTests PRIVATE /STACK:67108864,1048576)
  add_test(NAME OriginalMoviePlaneLifecycle COMMAND MoviePlaneTests "${CMAKE_SOURCE_DIR}/analysis/simpsons.pe")
  set_tests_properties(OriginalMoviePlaneLifecycle PROPERTIES TIMEOUT 60)
  add_test(NAME OriginalMovieDrawIntegration COMMAND MoviePlaneTests "${CMAKE_SOURCE_DIR}/analysis/simpsons.pe" --integration)
  set_tests_properties(OriginalMovieDrawIntegration PROPERTIES TIMEOUT 60)
  target_compile_options(MipTextureTests PRIVATE /fp:strict /W4 /WX)
  target_link_libraries(MipTextureTests PRIVATE SimpsonsGraphics)
  add_test(NAME NativeMipTexturesWARP COMMAND MipTextureTests)
  add_test(NAME NativeMipTexturesHardware COMMAND MipTextureTests --hardware)
  add_executable(BuiltinImageFilterTests tests/test_builtin_image_filter.cpp)
  target_compile_options(BuiltinImageFilterTests PRIVATE /fp:strict /W4 /WX)
  target_link_libraries(BuiltinImageFilterTests PRIVATE SimpsonsRuntime)
  target_link_options(BuiltinImageFilterTests PRIVATE /STACK:67108864,1048576)
  add_test(NAME OriginalBuiltinImageFilter COMMAND BuiltinImageFilterTests "${CMAKE_SOURCE_DIR}/analysis/simpsons.pe")
  set_tests_properties(OriginalBuiltinImageFilter PROPERTIES TIMEOUT 60)
  add_executable(BuiltinTextureLifecycleTests tests/test_builtin_texture_lifecycle.cpp)
  add_executable(ViewportColorTextureTests tests/test_viewport_color_textures.cpp)
  add_executable(MeshDeclarationLifecycleTests tests/test_mesh_declaration_lifecycle.cpp)
  target_compile_options(MeshDeclarationLifecycleTests PRIVATE /fp:strict /W4 /WX)
  target_link_libraries(MeshDeclarationLifecycleTests PRIVATE SimpsonsRuntime)
  target_link_options(MeshDeclarationLifecycleTests PRIVATE /STACK:67108864,1048576)
  add_test(NAME OriginalMeshDeclarationLifecycle COMMAND MeshDeclarationLifecycleTests "${CMAKE_SOURCE_DIR}/analysis/simpsons.pe")
  set_tests_properties(OriginalMeshDeclarationLifecycle PROPERTIES TIMEOUT 60)
  target_compile_options(ViewportColorTextureTests PRIVATE /fp:strict /W4 /WX)
  target_link_libraries(ViewportColorTextureTests PRIVATE SimpsonsRuntime)
  target_link_options(ViewportColorTextureTests PRIVATE /STACK:67108864,1048576)
  add_test(NAME OriginalViewportColorTextures COMMAND ViewportColorTextureTests "${CMAKE_SOURCE_DIR}/analysis/simpsons.pe")
  set_tests_properties(OriginalViewportColorTextures PROPERTIES TIMEOUT 60)
  add_executable(ViewportSurfaceTests tests/test_viewport_surfaces.cpp)
  target_compile_options(ViewportSurfaceTests PRIVATE /fp:strict /W4 /WX)
  target_link_libraries(ViewportSurfaceTests PRIVATE SimpsonsRuntime)
  target_link_options(ViewportSurfaceTests PRIVATE /STACK:67108864,1048576)
  add_test(NAME OriginalViewportSurfaces COMMAND ViewportSurfaceTests "${CMAKE_SOURCE_DIR}/analysis/simpsons.pe")
  set_tests_properties(OriginalViewportSurfaces PROPERTIES TIMEOUT 60)

  add_executable(SceneCopyTests tests/test_scene_copies.cpp)
  target_compile_options(SceneCopyTests PRIVATE /fp:strict /W4 /WX)
  target_link_libraries(SceneCopyTests PRIVATE SimpsonsRuntime)
  target_link_options(SceneCopyTests PRIVATE /STACK:67108864,1048576)
  add_test(NAME OriginalSceneCopies COMMAND SceneCopyTests "${CMAKE_SOURCE_DIR}/analysis/simpsons.pe")
  set_tests_properties(OriginalSceneCopies PROPERTIES TIMEOUT 60)
  target_compile_options(BuiltinTextureLifecycleTests PRIVATE /fp:strict /W4 /WX)
  target_link_libraries(BuiltinTextureLifecycleTests PRIVATE SimpsonsRuntime)
  target_link_options(BuiltinTextureLifecycleTests PRIVATE /STACK:67108864,1048576)
  add_test(NAME OriginalBuiltinTextureLifecycle COMMAND BuiltinTextureLifecycleTests "${CMAKE_SOURCE_DIR}/analysis/simpsons.pe")
  set_tests_properties(OriginalBuiltinTextureLifecycle PROPERTIES TIMEOUT 60)
  add_executable(NonlocalJumpTests tests/test_nonlocal_jump.cpp)
  target_compile_options(NonlocalJumpTests PRIVATE /fp:strict /W4 /WX)
  target_link_libraries(NonlocalJumpTests PRIVATE SimpsonsRuntime)
  target_link_options(NonlocalJumpTests PRIVATE /STACK:67108864,1048576)
  add_test(NAME OriginalNonlocalJump COMMAND NonlocalJumpTests "${CMAKE_SOURCE_DIR}/analysis/simpsons.pe")
  add_executable(BuiltinImageDecodeTests tests/test_builtin_image_decode.cpp)
  add_executable(ITXDSmallRGBATests tests/test_itxd_small_rgba.cpp)
  target_compile_options(ITXDSmallRGBATests PRIVATE /fp:strict /W4 /WX)
  target_link_libraries(ITXDSmallRGBATests PRIVATE SimpsonsGraphics)
  add_test(NAME OriginalITXDSmallRGBAWARP COMMAND ITXDSmallRGBATests
    "${CMAKE_SOURCE_DIR}/build/itxd-small-rgba/small.bin")
  add_test(NAME OriginalITXDSmallRGBAHardware COMMAND ITXDSmallRGBATests
    "${CMAKE_SOURCE_DIR}/build/itxd-small-rgba/small.bin" --hardware)
  set_tests_properties(OriginalITXDSmallRGBAWARP OriginalITXDSmallRGBAHardware PROPERTIES TIMEOUT 60)
  target_compile_options(BuiltinImageDecodeTests PRIVATE /fp:strict /W4 /WX)
  target_link_libraries(BuiltinImageDecodeTests PRIVATE SimpsonsRuntime)
  target_link_options(BuiltinImageDecodeTests PRIVATE /STACK:67108864,1048576)
  add_test(NAME OriginalBuiltinImageDecode COMMAND BuiltinImageDecodeTests "${CMAKE_SOURCE_DIR}/analysis/simpsons.pe")
  set_tests_properties(OriginalBuiltinImageDecode PROPERTIES TIMEOUT 60)
  add_executable(ReflectionTextureLifecycleTests tests/test_reflection_texture_lifecycle.cpp)
  target_compile_options(ReflectionTextureLifecycleTests PRIVATE /fp:strict /W4 /WX)
  target_link_libraries(ReflectionTextureLifecycleTests PRIVATE SimpsonsRuntime)
  target_link_options(ReflectionTextureLifecycleTests PRIVATE /STACK:67108864,1048576)
  add_test(NAME OriginalReflectionTextureLifecycle COMMAND ReflectionTextureLifecycleTests "${CMAKE_SOURCE_DIR}/analysis/simpsons.pe")
  set_tests_properties(OriginalReflectionTextureLifecycle PROPERTIES TIMEOUT 60)
  add_executable(CubeTextureTests tests/test_cube_texture.cpp)
  target_compile_options(CubeTextureTests PRIVATE /fp:strict /W4 /WX)
  target_link_libraries(CubeTextureTests PRIVATE SimpsonsGraphics)
  add_test(NAME NativeCubeTextureWARP COMMAND CubeTextureTests)
  add_test(NAME NativeCubeTextureHardware COMMAND CubeTextureTests --hardware)
  add_executable(WritableTextureTests tests/test_writable_texture.cpp)
  target_compile_options(WritableTextureTests PRIVATE /fp:strict /W4 /WX)
  target_link_libraries(WritableTextureTests PRIVATE SimpsonsGraphics)
  add_test(NAME NativeWritableTextureWARP COMMAND WritableTextureTests)
  add_test(NAME NativeWritableTextureHardware COMMAND WritableTextureTests --hardware)
  add_executable(ShadowCameraLifecycleTests tests/test_shadow_camera_lifecycle.cpp)
  add_executable(ShadowCameraPassTests tests/test_shadow_camera_pass.cpp)
  target_compile_options(ShadowCameraPassTests PRIVATE /fp:strict /W4 /WX)
  target_link_libraries(ShadowCameraPassTests PRIVATE SimpsonsRuntime)
  target_link_options(ShadowCameraPassTests PRIVATE /STACK:67108864,1048576)
  add_test(NAME OriginalShadowCameraPass COMMAND ShadowCameraPassTests "${CMAKE_SOURCE_DIR}/analysis/simpsons.pe")
  set_tests_properties(OriginalShadowCameraPass PROPERTIES TIMEOUT 60)
  add_test(NAME OriginalShadowTangentPass COMMAND ShadowCameraPassTests "${CMAKE_SOURCE_DIR}/analysis/simpsons.pe" tangent)
  set_tests_properties(OriginalShadowTangentPass PROPERTIES TIMEOUT 60 RUN_SERIAL TRUE)
  add_test(NAME OriginalShadowTangentContract COMMAND "${Python3_EXECUTABLE}" -B "${CMAKE_SOURCE_DIR}/tools/analyze_shadow_tangent_contract.py")
  target_compile_options(ShadowCameraLifecycleTests PRIVATE /fp:strict /W4 /WX)
  target_link_libraries(ShadowCameraLifecycleTests PRIVATE SimpsonsRuntime)
  target_link_options(ShadowCameraLifecycleTests PRIVATE /STACK:67108864,1048576)
  add_test(NAME OriginalShadowCameraLifecycle COMMAND ShadowCameraLifecycleTests "${CMAKE_SOURCE_DIR}/analysis/simpsons.pe")
  add_test(NAME OriginalReflectionCameraLifecycle COMMAND ShadowCameraLifecycleTests "${CMAKE_SOURCE_DIR}/analysis/simpsons.pe" --cube)
  add_test(NAME OriginalViewportCameraLifecycle COMMAND ShadowCameraLifecycleTests "${CMAKE_SOURCE_DIR}/analysis/simpsons.pe" --viewport)
  set_tests_properties(OriginalShadowCameraLifecycle PROPERTIES TIMEOUT 60)
  set_tests_properties(OriginalReflectionCameraLifecycle PROPERTIES TIMEOUT 60)
  set_tests_properties(OriginalViewportCameraLifecycle PROPERTIES TIMEOUT 60)
  add_executable(EffectResourceTests tests/test_effect_resources.cpp)
  target_compile_options(EffectResourceTests PRIVATE /fp:strict /W4 /WX)
  target_link_libraries(EffectResourceTests PRIVATE SimpsonsGraphics bcrypt)
  add_test(NAME NativeEffectMetadataOwnership COMMAND EffectResourceTests "${CMAKE_SOURCE_DIR}/analysis/simpsons.pe")
  set_tests_properties(NativeEffectMetadataOwnership PROPERTIES TIMEOUT 45)
  add_executable(EffectCatalogLifecycleTests tests/test_effect_catalog_lifecycle.cpp)
  target_compile_options(EffectCatalogLifecycleTests PRIVATE /fp:strict /W4 /WX)
  target_link_libraries(EffectCatalogLifecycleTests PRIVATE SimpsonsRuntime)
  target_link_options(EffectCatalogLifecycleTests PRIVATE /STACK:67108864,1048576)
  add_test(NAME OriginalEffectCatalogLifecycle COMMAND EffectCatalogLifecycleTests "${CMAKE_SOURCE_DIR}/analysis/simpsons.pe")
  set_tests_properties(OriginalEffectCatalogLifecycle PROPERTIES TIMEOUT 60)
  add_executable(PostEffectCatalogLifecycleTests tests/test_post_effect_catalog_lifecycle.cpp)
  add_executable(EdgeParameterTests tests/test_edge_parameters.cpp)
  target_compile_options(EdgeParameterTests PRIVATE /fp:strict /W4 /WX)
  target_link_libraries(EdgeParameterTests PRIVATE SimpsonsRuntime)
  target_link_options(EdgeParameterTests PRIVATE /STACK:67108864,1048576)
  add_test(NAME OriginalEdgeParameters COMMAND EdgeParameterTests "${CMAKE_SOURCE_DIR}/analysis/simpsons.pe")
  set_tests_properties(OriginalEdgeParameters PROPERTIES TIMEOUT 60)
  add_executable(EdgeAAParameterTests tests/test_edgeaa_parameters.cpp)
  target_compile_options(EdgeAAParameterTests PRIVATE /fp:strict /W4 /WX)
  target_link_libraries(EdgeAAParameterTests PRIVATE SimpsonsRuntime)
  target_link_options(EdgeAAParameterTests PRIVATE /STACK:67108864,1048576)
  add_dependencies(EdgeAAParameterTests VerifyITXDPaletteFixture)
  add_test(NAME OriginalEdgeAAParameters COMMAND EdgeAAParameterTests "${CMAKE_SOURCE_DIR}/analysis/simpsons.pe" "${CMAKE_SOURCE_DIR}/build/itxd-palette")
  set_tests_properties(OriginalEdgeAAParameters PROPERTIES TIMEOUT 60)
  add_test(NAME OriginalEdgeShaderEvidence COMMAND "${Python3_EXECUTABLE}" -B
    "${CMAKE_SOURCE_DIR}/tools/analyze_edge_shaders.py" --verify --self-test)
  target_compile_options(PostEffectCatalogLifecycleTests PRIVATE /fp:strict /W4 /WX)
  target_link_libraries(PostEffectCatalogLifecycleTests PRIVATE SimpsonsRuntime)
  target_link_options(PostEffectCatalogLifecycleTests PRIVATE /STACK:67108864,1048576)
  add_test(NAME OriginalCombinedEffectCatalogLifecycle COMMAND PostEffectCatalogLifecycleTests "${CMAKE_SOURCE_DIR}/analysis/simpsons.pe")
  set_tests_properties(OriginalCombinedEffectCatalogLifecycle PROPERTIES TIMEOUT 60)
  add_executable(EffectBindingConsumerTests tests/test_effect_binding_consumer.cpp)
  target_compile_options(EffectBindingConsumerTests PRIVATE /fp:strict /W4 /WX)
  target_link_libraries(EffectBindingConsumerTests PRIVATE SimpsonsRuntime)
  add_test(NAME OriginalEffectBindingConsumer COMMAND EffectBindingConsumerTests "${CMAKE_SOURCE_DIR}/analysis/simpsons.pe")
  set_tests_properties(OriginalEffectBindingConsumer PROPERTIES TIMEOUT 30)

  add_custom_target(EffectReflectionFixtureCheck
    COMMAND "${Python3_EXECUTABLE}" -B "${CMAKE_SOURCE_DIR}/tools/generate_effect_reflection_fixture.py"
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    VERBATIM)
  add_executable(EffectReflectionTests tests/test_effect_reflection.cpp)
  add_executable(EffectTextureUsageTests tests/test_effect_texture_usage.cpp)
  target_compile_options(EffectTextureUsageTests PRIVATE /fp:strict /W4 /WX)
  target_link_libraries(EffectTextureUsageTests PRIVATE SimpsonsGraphics bcrypt)
  add_test(NAME OriginalEffectTextureUsage COMMAND EffectTextureUsageTests "${CMAKE_SOURCE_DIR}/analysis/simpsons.pe")
  add_test(NAME OriginalEffectAdmissionAudit COMMAND "${Python3_EXECUTABLE}" -B
    "${CMAKE_SOURCE_DIR}/tests/test_effect_admission_audit.py" -v)
  add_dependencies(EffectReflectionTests EffectReflectionFixtureCheck)
  target_compile_options(EffectReflectionTests PRIVATE /fp:strict /W4 /WX)
  target_link_libraries(EffectReflectionTests PRIVATE SimpsonsGraphics bcrypt)
  add_test(NAME NativeEffectReflectionMetadata COMMAND EffectReflectionTests
    "${CMAKE_SOURCE_DIR}/analysis/simpsons.pe" "${CMAKE_SOURCE_DIR}/build/effect-reflection-all49/fixture.bin")
  set_tests_properties(NativeEffectReflectionMetadata PROPERTIES TIMEOUT 30)

  add_executable(EffectReflectionGatewayTests tests/test_effect_reflection_gateway.cpp)
  add_dependencies(EffectReflectionGatewayTests EffectReflectionFixtureCheck)
  target_compile_options(EffectReflectionGatewayTests PRIVATE /fp:strict /W4 /WX)
  target_link_libraries(EffectReflectionGatewayTests PRIVATE SimpsonsRuntime)
  add_test(NAME OriginalEffectReflectionGateway COMMAND EffectReflectionGatewayTests
    "${CMAKE_SOURCE_DIR}/analysis/simpsons.pe" "${CMAKE_SOURCE_DIR}/build/effect-reflection-all49/fixture.bin")
  set_tests_properties(OriginalEffectReflectionGateway PROPERTIES TIMEOUT 60)
  add_custom_target(EffectFinalizerFixtureCheck
    COMMAND "${Python3_EXECUTABLE}" -B "${CMAKE_SOURCE_DIR}/tools/generate_effect_finalizer_fixture.py"
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}" VERBATIM)
  add_executable(EffectFinalizerTests tests/test_effect_finalizers.cpp)
  add_dependencies(EffectFinalizerTests EffectFinalizerFixtureCheck)
  target_compile_options(EffectFinalizerTests PRIVATE /fp:strict /W4 /WX)
  target_link_libraries(EffectFinalizerTests PRIVATE SimpsonsRuntime)
  add_test(NAME OriginalEffectFinalizers COMMAND EffectFinalizerTests
    "${CMAKE_SOURCE_DIR}/analysis/simpsons.pe" "${CMAKE_SOURCE_DIR}/build/effect-finalizer-integration/fixture.txt")
  set_tests_properties(OriginalEffectFinalizers PROPERTIES TIMEOUT 60)
  add_executable(CubeLayoutTests tests/test_cube_layout.cpp)
  target_compile_options(CubeLayoutTests PRIVATE /fp:strict /W4 /WX)
  target_link_libraries(CubeLayoutTests PRIVATE SimpsonsRuntime)
  add_test(NAME OriginalCubeCpuLayout COMMAND CubeLayoutTests "${CMAKE_SOURCE_DIR}/analysis/simpsons.pe")
  set_tests_properties(OriginalCubeCpuLayout PROPERTIES TIMEOUT 30)
  add_executable(EffectPoolCpuTests tests/test_effect_pool_cpu.cpp)
  target_compile_options(EffectPoolCpuTests PRIVATE /fp:strict /W4 /WX)
  target_link_libraries(EffectPoolCpuTests PRIVATE SimpsonsRuntime)
  target_link_options(EffectPoolCpuTests PRIVATE /STACK:67108864,1048576)
  add_test(NAME OriginalEffectPoolCpuMetadata COMMAND EffectPoolCpuTests "${CMAKE_SOURCE_DIR}/analysis/simpsons.pe")
  set_tests_properties(OriginalEffectPoolCpuMetadata PROPERTIES TIMEOUT 60)
  add_executable(FourTapShaderTests tests/test_fourtap_shaders.cpp ${SIMPSONS_FOURTAP_HEADERS})
  target_include_directories(FourTapShaderTests PRIVATE "${SIMPSONS_SHADER_DIR}")
  target_compile_definitions(FourTapShaderTests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
  target_compile_options(FourTapShaderTests PRIVATE /fp:strict /W4 /WX)
  target_link_libraries(FourTapShaderTests PRIVATE d3d11 dxgi)
  add_test(NAME NativeFourTapShaderWARP COMMAND FourTapShaderTests)
  add_test(NAME NativeFourTapShaderHardware COMMAND FourTapShaderTests --hardware)
  set_tests_properties(NativeFourTapShaderWARP NativeFourTapShaderHardware PROPERTIES TIMEOUT 45)
  add_executable(ShadowDepthShaderTests tests/test_shadow_depth_shader.cpp ${SIMPSONS_SHADOW_HEADERS})
  add_executable(ShadowAlphaShaderTests tests/test_shadow_alpha_shader.cpp ${SIMPSONS_SHADOW_ALPHA_HEADERS})
  target_include_directories(ShadowAlphaShaderTests PRIVATE "${SIMPSONS_SHADER_DIR}")
  target_compile_definitions(ShadowAlphaShaderTests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
  target_compile_options(ShadowAlphaShaderTests PRIVATE /fp:strict /W4 /WX)
  target_link_libraries(ShadowAlphaShaderTests PRIVATE d3d11 dxgi)
  add_test(NAME NativeShadowAlphaShaderWARP COMMAND ShadowAlphaShaderTests)
  add_test(NAME NativeShadowAlphaShaderHardware COMMAND ShadowAlphaShaderTests --hardware)
  add_test(NAME OriginalShadowAlphaShaderEvidence COMMAND "${Python3_EXECUTABLE}" -B "${CMAKE_SOURCE_DIR}/tools/analyze_shadow_alpha_shader.py" --verify --self-test)
  add_test(NAME OriginalShadowDepthCopyEvidence COMMAND "${Python3_EXECUTABLE}" -B "${CMAKE_SOURCE_DIR}/tools/analyze_shadow_depth_copy.py" --verify --self-test)
  set_tests_properties(NativeShadowAlphaShaderWARP NativeShadowAlphaShaderHardware PROPERTIES TIMEOUT 45)
  target_include_directories(ShadowDepthShaderTests PRIVATE "${SIMPSONS_SHADER_DIR}")
  target_compile_definitions(ShadowDepthShaderTests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
  target_compile_options(ShadowDepthShaderTests PRIVATE /fp:strict /W4 /WX)
  target_link_libraries(ShadowDepthShaderTests PRIVATE d3d11 dxgi)
  add_test(NAME NativeShadowDepthShaderWARP COMMAND ShadowDepthShaderTests)
  add_test(NAME NativeShadowDepthShaderHardware COMMAND ShadowDepthShaderTests --hardware)
  add_test(NAME OriginalShadowDepthShaderEvidence COMMAND "${Python3_EXECUTABLE}" -B "${CMAKE_SOURCE_DIR}/tools/analyze_shadow_depth_shader.py" --verify --self-test)
  set_tests_properties(NativeShadowDepthShaderWARP NativeShadowDepthShaderHardware PROPERTIES TIMEOUT 45)
  add_executable(EdgeShaderTests tests/test_edge_shaders.cpp ${SIMPSONS_EDGE_HEADERS})
  target_include_directories(EdgeShaderTests PRIVATE "${SIMPSONS_SHADER_DIR}")
  target_compile_definitions(EdgeShaderTests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
  target_compile_options(EdgeShaderTests PRIVATE /fp:strict /W4 /WX)
  target_link_libraries(EdgeShaderTests PRIVATE d3d11 dxgi)
  add_test(NAME NativeEdgeShaderWARP COMMAND EdgeShaderTests)
  add_test(NAME NativeEdgeShaderHardware COMMAND EdgeShaderTests --hardware)
  set_tests_properties(NativeEdgeShaderWARP NativeEdgeShaderHardware PROPERTIES TIMEOUT 45)
  add_executable(AAShaderTests tests/test_edge_shaders.cpp ${SIMPSONS_AA_HEADERS})
  target_include_directories(AAShaderTests PRIVATE "${SIMPSONS_SHADER_DIR}")
  target_compile_definitions(AAShaderTests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN SIMPSONS_AA_SHADER_TESTS)
  target_compile_options(AAShaderTests PRIVATE /fp:strict /W4 /WX)
  target_link_libraries(AAShaderTests PRIVATE d3d11 dxgi)
  add_test(NAME NativeAAShaderWARP COMMAND AAShaderTests)
  add_test(NAME NativeAAShaderHardware COMMAND AAShaderTests --hardware)
  add_test(NAME OriginalAAShaderEvidence COMMAND "${Python3_EXECUTABLE}" -B "${CMAKE_SOURCE_DIR}/tools/analyze_aa_shaders.py" --verify --self-test)
  set_tests_properties(NativeAAShaderWARP NativeAAShaderHardware OriginalAAShaderEvidence PROPERTIES TIMEOUT 45)
  set(EDGEAA_PROBE_HEADER "${SIMPSONS_SHADER_DIR}/PSEdgeAASourceProbe.h")
  add_custom_command(OUTPUT "${EDGEAA_PROBE_HEADER}"
    COMMAND "${SIMPSONS_FXC}" /nologo /Ges /Gis /O3 /T ps_5_0 /E PSEdgeAASourceProbe
      /Fh "${EDGEAA_PROBE_HEADER}" /Vn kPSEdgeAASourceProbe "${CMAKE_SOURCE_DIR}/tests/edgeaa_source_probe.hlsl"
    DEPENDS tests/edgeaa_source_probe.hlsl VERBATIM)
  add_executable(EdgeAAShaderTests tests/test_edgeaa_shaders.cpp ${SIMPSONS_EDGEAA_HEADERS} "${EDGEAA_PROBE_HEADER}")
  target_include_directories(EdgeAAShaderTests PRIVATE "${SIMPSONS_SHADER_DIR}")
  target_compile_definitions(EdgeAAShaderTests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
  target_compile_options(EdgeAAShaderTests PRIVATE /fp:strict /W4 /WX)
  target_link_libraries(EdgeAAShaderTests PRIVATE d3d11 dxgi)
  add_test(NAME NativeEdgeAAShaderWARP COMMAND EdgeAAShaderTests)
  add_test(NAME NativeEdgeAAShaderHardware COMMAND EdgeAAShaderTests --hardware)
  add_test(NAME OriginalEdgeAAShaderEvidence COMMAND "${Python3_EXECUTABLE}" -B "${CMAKE_SOURCE_DIR}/tools/analyze_edgeaa_shaders.py" --verify --self-test)
  set_tests_properties(NativeEdgeAAShaderWARP NativeEdgeAAShaderHardware OriginalEdgeAAShaderEvidence PROPERTIES TIMEOUT 45)
  add_executable(EdgeBackendTests tests/test_edge_backend.cpp)
  target_compile_options(EdgeBackendTests PRIVATE /fp:strict /W4 /WX)
  target_link_libraries(EdgeBackendTests PRIVATE SimpsonsGraphics)
  add_test(NAME NativeEdgeBackendWARP COMMAND EdgeBackendTests "${CMAKE_SOURCE_DIR}/analysis/simpsons.pe")
  add_test(NAME NativeEdgeBackendHardware COMMAND EdgeBackendTests "${CMAKE_SOURCE_DIR}/analysis/simpsons.pe" --hardware)
  set_tests_properties(NativeEdgeBackendWARP NativeEdgeBackendHardware PROPERTIES TIMEOUT 45)
  add_test(NAME OriginalFourTapShaderSemantics COMMAND "${Python3_EXECUTABLE}" -B
    "${CMAKE_SOURCE_DIR}/tools/analyze_fourtap_shaders.py" --verify --self-test)
  set_tests_properties(OriginalFourTapShaderSemantics PROPERTIES TIMEOUT 30)
  # Compile the same backend with explicit test access in a separate executable;
  # the production library above contains no inspection/error-injection API.
  add_executable(NativeAudioOutputTests tests/test_native_audio_output.cpp audio/native_audio_output.cpp)
  target_include_directories(NativeAudioOutputTests PRIVATE "${CMAKE_SOURCE_DIR}")
  target_compile_definitions(NativeAudioOutputTests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN SIMPSONS_AUDIO_OUTPUT_TESTS)
  target_compile_options(NativeAudioOutputTests PRIVATE /fp:strict /W4 /WX)
  target_link_libraries(NativeAudioOutputTests PRIVATE xaudio2 ole32 ksuser uuid)
  add_test(NAME NativeAudioOutputOwnership COMMAND NativeAudioOutputTests)
  set_tests_properties(NativeAudioOutputOwnership PROPERTIES TIMEOUT 45)
  add_executable(NativeXmaCodecTests tests/test_native_xma_codec.cpp)
  target_compile_options(NativeXmaCodecTests PRIVATE /fp:strict)
  target_link_libraries(NativeXmaCodecTests PRIVATE SimpsonsAudio bcrypt)
  add_test(NAME NativeXmaCodecOwnership COMMAND "${Python3_EXECUTABLE}" -B
    "${CMAKE_SOURCE_DIR}/tools/prepare_audio_fixtures.py" --test "$<TARGET_FILE:NativeXmaCodecTests>")
  set_tests_properties(NativeXmaCodecOwnership PROPERTIES TIMEOUT 60)
  add_executable(NativeXmaSourceTests tests/test_xma_source.cpp)
  target_compile_options(NativeXmaSourceTests PRIVATE /fp:strict /W4 /WX)
  target_link_libraries(NativeXmaSourceTests PRIVATE SimpsonsAudio bcrypt)
  add_test(NAME NativeXmaSourceOwnership COMMAND "${Python3_EXECUTABLE}" -B
    "${CMAKE_SOURCE_DIR}/tools/prepare_xma_source_fixtures.py" --test "$<TARGET_FILE:NativeXmaSourceTests>")
  set_tests_properties(NativeXmaSourceOwnership PROPERTIES TIMEOUT 60)
  add_executable(MenuXmaTests tests/test_menu_xma.cpp)
  target_compile_options(MenuXmaTests PRIVATE /fp:strict /W4 /WX)
  target_link_libraries(MenuXmaTests PRIVATE SimpsonsAudio bcrypt)
  add_executable(AudioCatalogExtentTests tests/test_audio_catalog_extent.cpp)
  target_compile_options(AudioCatalogExtentTests PRIVATE /fp:strict /W4 /WX)
  target_link_libraries(AudioCatalogExtentTests PRIVATE SimpsonsAudio bcrypt)
  add_test(NAME NativeAudioCatalogSourceExtent COMMAND "${Python3_EXECUTABLE}" -B
    "${CMAKE_SOURCE_DIR}/tools/prepare_audio_catalog_extent_fixture.py" --test "$<TARGET_FILE:AudioCatalogExtentTests>")
  set_tests_properties(NativeAudioCatalogSourceExtent PROPERTIES TIMEOUT 120)
  add_executable(AudioCatalogTests tests/test_audio_catalog.cpp)
  target_compile_options(AudioCatalogTests PRIVATE /fp:strict /W4 /WX)
  target_link_libraries(AudioCatalogTests PRIVATE SimpsonsAudio bcrypt)
  add_test(NAME NativeAudioCatalogAdmission COMMAND AudioCatalogTests
    "${CMAKE_SOURCE_DIR}/analysis/audio_catalog.bin" "${CMAKE_SOURCE_DIR}/Simpsons Game, The (USA)")
  add_executable(ResidentAudioCatalogTests tests/test_resident_audio_catalog.cpp)
  target_compile_options(ResidentAudioCatalogTests PRIVATE /fp:strict /W4 /WX)
  target_link_libraries(ResidentAudioCatalogTests PRIVATE SimpsonsAudio bcrypt)
  add_test(NAME NativeResidentAudioCatalogAdmission COMMAND "${Python3_EXECUTABLE}" -B
    "${CMAKE_SOURCE_DIR}/tools/prepare_resident_audio_catalog_fixture.py"
    --test "$<TARGET_FILE:ResidentAudioCatalogTests>"
    --catalog "${CMAKE_SOURCE_DIR}/analysis/resident_audio_catalog.bin")
  add_executable(AmxAudioCatalogTests tests/test_amx_audio_catalog_native.cpp)
  target_compile_options(AmxAudioCatalogTests PRIVATE /fp:strict /W4 /WX)
  target_link_libraries(AmxAudioCatalogTests PRIVATE SimpsonsAudio bcrypt)
  add_test(NAME NativeAmxAudioCatalogAdmission COMMAND "${Python3_EXECUTABLE}" -B
    "${CMAKE_SOURCE_DIR}/tools/prepare_amx_audio_catalog_fixture.py"
    --test "$<TARGET_FILE:AmxAudioCatalogTests>"
    --catalog "${CMAKE_SOURCE_DIR}/analysis/amx_audio_catalog.bin")
  add_test(NAME MenuXmaSources COMMAND "${Python3_EXECUTABLE}" -B
    "${CMAKE_SOURCE_DIR}/tools/prepare_menu_xma_fixtures.py" --test "$<TARGET_FILE:MenuXmaTests>")
  set_tests_properties(MenuXmaSources PROPERTIES TIMEOUT 180)
  add_test(NAME LocMusicXmaSources COMMAND "${Python3_EXECUTABLE}" -B
    "${CMAKE_SOURCE_DIR}/tools/prepare_menu_xma_fixtures.py" --bank loc --test "$<TARGET_FILE:MenuXmaTests>")
  set_tests_properties(LocMusicXmaSources PROPERTIES TIMEOUT 180)
  add_test(NAME MonoDialogueXmaSources COMMAND "${Python3_EXECUTABLE}" -B
    "${CMAKE_SOURCE_DIR}/tools/prepare_menu_xma_fixtures.py" --bank dialogue --test "$<TARGET_FILE:MenuXmaTests>")
  set_tests_properties(MonoDialogueXmaSources PROPERTIES TIMEOUT 60)
  add_executable(ResidentXmaTests tests/test_resident_xma.cpp)
  target_compile_options(ResidentXmaTests PRIVATE /fp:strict /W4 /WX)
  target_link_libraries(ResidentXmaTests PRIVATE SimpsonsAudio bcrypt)
  add_test(NAME ResidentXmaSource COMMAND "${Python3_EXECUTABLE}" -B
    "${CMAKE_SOURCE_DIR}/tools/prepare_resident_xma_fixture.py" --test "$<TARGET_FILE:ResidentXmaTests>")
  set_tests_properties(ResidentXmaSource PROPERTIES TIMEOUT 60)
  add_test(NAME StoryResidentXmaSource COMMAND "${Python3_EXECUTABLE}" -B
    "${CMAKE_SOURCE_DIR}/tools/prepare_story_xma_fixture.py" --test "$<TARGET_FILE:ResidentXmaTests>")
  set_tests_properties(StoryResidentXmaSource PROPERTIES TIMEOUT 180)
  add_test(NAME LocResidentXmaSource COMMAND "${Python3_EXECUTABLE}" -B
    "${CMAKE_SOURCE_DIR}/tools/prepare_loc_xma_fixture.py" --test "$<TARGET_FILE:ResidentXmaTests>")
  set_tests_properties(LocResidentXmaSource PROPERTIES TIMEOUT 180)
  add_test(NAME CharactersResidentXmaSource COMMAND "${Python3_EXECUTABLE}" -B
    "${CMAKE_SOURCE_DIR}/tools/prepare_characters_xma_fixture.py" --test "$<TARGET_FILE:ResidentXmaTests>")
  set_tests_properties(CharactersResidentXmaSource PROPERTIES TIMEOUT 180)
  add_test(NAME HomerResidentXmaSource COMMAND "${Python3_EXECUTABLE}" -B
    "${CMAKE_SOURCE_DIR}/tools/prepare_homer_xma_fixture.py" --test "$<TARGET_FILE:ResidentXmaTests>")
  set_tests_properties(HomerResidentXmaSource PROPERTIES TIMEOUT 360)
  add_test(NAME LocGlobalResidentXmaSource COMMAND "${Python3_EXECUTABLE}" -B
    "${CMAKE_SOURCE_DIR}/tools/prepare_loc_global_xma_fixture.py" --test "$<TARGET_FILE:ResidentXmaTests>")
  set_tests_properties(LocGlobalResidentXmaSource PROPERTIES TIMEOUT 180)
  add_executable(EngineAudioTests tests/test_engine_audio.cpp)
  target_link_libraries(EngineAudioTests PRIVATE SimpsonsRuntime)
  target_link_options(EngineAudioTests PRIVATE /STACK:67108864,1048576)
  add_test(NAME OriginalExm0Lifecycle COMMAND EngineAudioTests "${CMAKE_SOURCE_DIR}/analysis/simpsons.pe")
  set_tests_properties(OriginalExm0Lifecycle PROPERTIES TIMEOUT 45)
  add_executable(ResidentBankLifecycleTests tests/test_resident_bank_lifecycle.cpp)
  target_link_libraries(ResidentBankLifecycleTests PRIVATE SimpsonsRuntime)
  target_link_options(ResidentBankLifecycleTests PRIVATE /STACK:67108864,1048576)
  add_test(NAME OriginalResidentBankLifecycle COMMAND ResidentBankLifecycleTests "${CMAKE_SOURCE_DIR}/analysis/simpsons.pe")
  set_tests_properties(OriginalResidentBankLifecycle PROPERTIES TIMEOUT 90 WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
  add_executable(AmxStartupProvenanceTests tests/test_amx_startup_provenance.cpp)
  target_link_libraries(AmxStartupProvenanceTests PRIVATE SimpsonsRuntime)
  target_link_options(AmxStartupProvenanceTests PRIVATE /STACK:67108864,1048576)
  add_test(NAME OriginalAmxStartupProvenance COMMAND AmxStartupProvenanceTests "${CMAKE_SOURCE_DIR}/analysis/simpsons.pe")
  set_tests_properties(OriginalAmxStartupProvenance PROPERTIES TIMEOUT 90 WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
  add_executable(DacPcmTests tests/test_dac_pcm.cpp)
  target_compile_options(DacPcmTests PRIVATE /fp:strict)
  target_link_libraries(DacPcmTests PRIVATE SimpsonsRuntime bcrypt)
  add_test(NAME OriginalDacPcm COMMAND DacPcmTests "${CMAKE_SOURCE_DIR}/analysis/simpsons.pe")
  set_tests_properties(OriginalDacPcm PROPERTIES TIMEOUT 30)
  add_executable(DacGainTests tests/test_dac_gain.cpp)
  target_compile_options(DacGainTests PRIVATE /fp:strict)
  target_link_libraries(DacGainTests PRIVATE SimpsonsRuntime bcrypt)
  add_test(NAME OriginalDacGain COMMAND DacGainTests "${CMAKE_SOURCE_DIR}/analysis/simpsons.pe")
  set_tests_properties(OriginalDacGain PROPERTIES TIMEOUT 30)
  add_executable(DacLifecycleTests tests/test_dac_lifecycle.cpp)
  target_compile_options(DacLifecycleTests PRIVATE /fp:strict)
  target_link_libraries(DacLifecycleTests PRIVATE SimpsonsRuntime bcrypt)
  add_test(NAME OriginalDacLifecycle COMMAND DacLifecycleTests "${CMAKE_SOURCE_DIR}/analysis/simpsons.pe")
  set_tests_properties(OriginalDacLifecycle PROPERTIES TIMEOUT 45)
  add_executable(AudioReaderLifecycleTests tests/test_audio_reader_lifecycle.cpp)
  target_compile_options(AudioReaderLifecycleTests PRIVATE /fp:strict /W4 /WX)
  target_link_libraries(AudioReaderLifecycleTests PRIVATE SimpsonsRuntime bcrypt)
  add_test(NAME OriginalAudioReaderLifecycle COMMAND AudioReaderLifecycleTests "${CMAKE_SOURCE_DIR}/analysis/simpsons.pe")
  set_tests_properties(OriginalAudioReaderLifecycle PROPERTIES TIMEOUT 60)
  add_executable(AudioReaderProducerTests tests/test_audio_reader_producer.cpp)
  target_compile_options(AudioReaderProducerTests PRIVATE /fp:strict /W4 /WX)
  target_link_libraries(AudioReaderProducerTests PRIVATE SimpsonsRuntime bcrypt)
  add_test(NAME OriginalAudioReaderProducer COMMAND AudioReaderProducerTests "${CMAKE_SOURCE_DIR}/analysis/simpsons.pe")
  set_tests_properties(OriginalAudioReaderProducer PROPERTIES TIMEOUT 60)
  foreach(INDEX RANGE 0 13)
    add_test(NAME OriginalAudioReaderProducerCase${INDEX} COMMAND AudioReaderProducerTests "${CMAKE_SOURCE_DIR}/analysis/simpsons.pe" "${INDEX}")
    set_tests_properties(OriginalAudioReaderProducerCase${INDEX} PROPERTIES TIMEOUT 60)
  endforeach()
  add_executable(ResourceAuditTests tests/test_resource_audit.cpp)
  target_compile_options(ResourceAuditTests PRIVATE /W4 /WX)
  target_include_directories(ResourceAuditTests PRIVATE "${CMAKE_SOURCE_DIR}")
  add_test(NAME ResourceAuditReceipts COMMAND ResourceAuditTests "${CMAKE_CURRENT_BINARY_DIR}")
  add_executable(HeapPageIndexTests tests/test_heap_page_index.cpp)
  target_compile_options(HeapPageIndexTests PRIVATE /W4 /WX)
  target_include_directories(HeapPageIndexTests PRIVATE "${CMAKE_SOURCE_DIR}")
  add_test(NAME OriginalHeapPageIndex COMMAND HeapPageIndexTests)
  add_executable(AudioSourceFileReceiptTests tests/test_audio_source_file_receipt.cpp)
  target_compile_options(AudioSourceFileReceiptTests PRIVATE /W4 /WX)
  target_include_directories(AudioSourceFileReceiptTests PRIVATE "${CMAKE_SOURCE_DIR}")
  add_test(NAME OriginalAudioSourceFileReceipt COMMAND AudioSourceFileReceiptTests)
  add_executable(IndependentEffectSetupTests tests/test_independent_effect_setup.cpp)
  target_compile_options(IndependentEffectSetupTests PRIVATE /fp:strict /W4 /WX)
  target_link_libraries(IndependentEffectSetupTests PRIVATE SimpsonsRuntime)
  target_link_options(IndependentEffectSetupTests PRIVATE /STACK:67108864,1048576)
  foreach(INDEX RANGE 0 48)
    if(INDEX LESS 10)
      set(ROW_NAME "0${INDEX}")
    else()
      set(ROW_NAME "${INDEX}")
    endif()
    add_test(NAME OriginalIndependentEffectSetup${ROW_NAME} COMMAND IndependentEffectSetupTests "${CMAKE_SOURCE_DIR}/analysis/simpsons.pe" "${INDEX}")
    set_tests_properties(OriginalIndependentEffectSetup${ROW_NAME} PROPERTIES TIMEOUT 60 RUN_SERIAL TRUE)
  endforeach()
  add_executable(AudioReaderClaimTests tests/test_audio_reader_claim.cpp)
  target_compile_options(AudioReaderClaimTests PRIVATE /fp:strict /W4 /WX)
  target_link_libraries(AudioReaderClaimTests PRIVATE SimpsonsRuntime bcrypt)
  add_test(NAME OriginalAudioReaderClaim COMMAND AudioReaderClaimTests "${CMAKE_SOURCE_DIR}/analysis/simpsons.pe")
  set_tests_properties(OriginalAudioReaderClaim PROPERTIES TIMEOUT 60)
  add_executable(DacProcessorTests tests/test_dac_processor.cpp audio/dac_processor.cpp audio/dac_gain.cpp audio/native_audio_output.cpp)
  target_include_directories(DacProcessorTests PRIVATE "${CMAKE_SOURCE_DIR}")
  target_compile_definitions(DacProcessorTests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN SIMPSONS_DAC_PROCESSOR_TESTS SIMPSONS_AUDIO_OUTPUT_TESTS)
  target_compile_options(DacProcessorTests PRIVATE /fp:strict /W4 /WX)
  target_link_libraries(DacProcessorTests PRIVATE xaudio2 ole32 ksuser uuid)
  add_test(NAME NativeDacProcessor COMMAND DacProcessorTests)
  set_tests_properties(NativeDacProcessor PROPERTIES TIMEOUT 45)
  add_executable(RuntimeTests tests/runtime_tests.cpp)
  target_link_libraries(RuntimeTests PRIVATE SimpsonsRuntime)
  add_test(NAME NativeMemoryContract COMMAND RuntimeTests)
  add_executable(GuestReadMemoTests tests/test_guest_read_memo.cpp)
  target_link_libraries(GuestReadMemoTests PRIVATE SimpsonsRuntime)
  add_test(NAME NativeGuestReadMemo COMMAND GuestReadMemoTests)
  set(INLINE_MEMORY_REFERENCE "${SIMPSONS_GENERATED_DIR}/inline_memory_reference.h")
  add_custom_command(OUTPUT "${INLINE_MEMORY_REFERENCE}"
    COMMAND "${Python3_EXECUTABLE}" -B "${CMAKE_SOURCE_DIR}/tools/generate_inline_memory_reference.py"
      --source "${SIMPSONS_GENERATED_DIR}/ppc_recomp.70.cpp"
      --matrix-source "${SIMPSONS_GENERATED_DIR}/ppc_recomp.71.cpp"
      --runtime-source "${SIMPSONS_GENERATED_DIR}/ppc_recomp.239.cpp"
      --vector-source "${SIMPSONS_GENERATED_DIR}/ppc_recomp.271.cpp" --output "${INLINE_MEMORY_REFERENCE}"
    DEPENDS tools/generate_inline_memory_reference.py "${SIMPSONS_GENERATED_DIR}/ppc_recomp.70.cpp"
      "${SIMPSONS_GENERATED_DIR}/ppc_recomp.71.cpp"
      "${SIMPSONS_GENERATED_DIR}/ppc_recomp.239.cpp" "${SIMPSONS_GENERATED_DIR}/ppc_recomp.271.cpp"
    VERBATIM)
  add_executable(InlineMemoryTests tests/test_inline_memory.cpp "${INLINE_MEMORY_REFERENCE}")
  set_source_files_properties(tests/test_inline_memory.cpp PROPERTIES OBJECT_DEPENDS "${INLINE_MEMORY_REFERENCE}")
  add_dependencies(InlineMemoryTests VerifyAOT)
  target_compile_options(InlineMemoryTests PRIVATE /fp:strict /W4 /WX /Ob2)
  target_link_libraries(InlineMemoryTests PRIVATE SimpsonsRuntime)
  add_test(NAME NativeInlineMemoryContract COMMAND InlineMemoryTests)
  set_tests_properties(NativeInlineMemoryContract PROPERTIES TIMEOUT 30)
  add_executable(NativeTickTests tests/test_native_tick.cpp)
  target_compile_options(NativeTickTests PRIVATE /fp:strict)
  target_link_libraries(NativeTickTests PRIVATE SimpsonsRuntime)
  add_test(NAME NativeMillisecondClock COMMAND NativeTickTests "${CMAKE_SOURCE_DIR}/analysis/simpsons.pe")
  set_tests_properties(NativeMillisecondClock PROPERTIES TIMEOUT 15)
  add_executable(NativeAnsiStringTests tests/test_native_ansi_string.cpp)
  target_link_libraries(NativeAnsiStringTests PRIVATE SimpsonsRuntime)
  add_test(NAME NativeAnsiString COMMAND NativeAnsiStringTests)
  set_tests_properties(NativeAnsiString PROPERTIES TIMEOUT 15)
  add_executable(NativeConfigurationTests tests/test_native_configuration.cpp)
  target_compile_options(NativeConfigurationTests PRIVATE /fp:strict /W4 /WX)
  target_link_libraries(NativeConfigurationTests PRIVATE SimpsonsRuntime)
  add_test(NAME NativeConfiguration COMMAND NativeConfigurationTests "${CMAKE_SOURCE_DIR}/analysis/simpsons.pe")
  set_tests_properties(NativeConfiguration PROPERTIES TIMEOUT 15)
  add_executable(ExecutionIdentityTests tests/test_execution_identity.cpp)
  target_compile_options(ExecutionIdentityTests PRIVATE /fp:strict /W4 /WX)
  target_link_libraries(ExecutionIdentityTests PRIVATE SimpsonsRuntime)
  target_link_options(ExecutionIdentityTests PRIVATE /STACK:67108864,1048576)
  add_test(NAME OriginalExecutionIdentity COMMAND ExecutionIdentityTests "${CMAKE_SOURCE_DIR}/analysis/simpsons.pe")
  set_tests_properties(OriginalExecutionIdentity PROPERTIES TIMEOUT 15)
  add_executable(ProfilePreferencesTests tests/test_profile_preferences.cpp)
  target_compile_options(ProfilePreferencesTests PRIVATE /fp:strict /W4 /WX)
  target_link_libraries(ProfilePreferencesTests PRIVATE SimpsonsRuntime)
  target_link_options(ProfilePreferencesTests PRIVATE /STACK:67108864,1048576)
  add_test(NAME OriginalProfilePreferences COMMAND ProfilePreferencesTests "${CMAKE_SOURCE_DIR}/analysis/simpsons.pe")
  set_tests_properties(OriginalProfilePreferences PROPERTIES TIMEOUT 45)
  add_executable(NativeLocalPlayersTests tests/test_native_local_players.cpp)
  target_compile_options(NativeLocalPlayersTests PRIVATE /fp:strict /W4 /WX)
  target_link_libraries(NativeLocalPlayersTests PRIVATE SimpsonsRuntime)
  add_test(NAME NativeLocalPlayerOwnership COMMAND NativeLocalPlayersTests)
  set_tests_properties(NativeLocalPlayerOwnership PROPERTIES TIMEOUT 30)
  add_executable(LocalPlayerBridgeTests tests/test_local_player_bridge.cpp)
  target_compile_options(LocalPlayerBridgeTests PRIVATE /fp:strict /W4 /WX)
  target_link_libraries(LocalPlayerBridgeTests PRIVATE SimpsonsRuntime)
  add_test(NAME OriginalLocalPlayerQuery COMMAND LocalPlayerBridgeTests "${CMAKE_SOURCE_DIR}/analysis/simpsons.pe")
  set_tests_properties(OriginalLocalPlayerQuery PROPERTIES TIMEOUT 30)
  add_test(NAME NativeLocalProfileCommandLine COMMAND "${Python3_EXECUTABLE}" -B
    "${CMAKE_SOURCE_DIR}/tests/test_local_profile_cli.py" "$<TARGET_FILE:SimpsonsNative>")
  set_tests_properties(NativeLocalProfileCommandLine PROPERTIES TIMEOUT 45)
  add_executable(NativeAchievementBridgeTests tests/test_achievement_bridge.cpp)
  target_compile_options(NativeAchievementBridgeTests PRIVATE /fp:strict /W4 /WX)
  target_link_libraries(NativeAchievementBridgeTests PRIVATE SimpsonsRuntime)
  add_test(NAME OriginalNativeAchievementRequest COMMAND NativeAchievementBridgeTests "${CMAKE_SOURCE_DIR}/analysis/simpsons.pe")
  set_tests_properties(OriginalNativeAchievementRequest PROPERTIES TIMEOUT 30)
  add_executable(NativeWordSwitchTests tests/test_native_word_switch.cpp)
  target_compile_options(NativeWordSwitchTests PRIVATE /fp:strict /W4 /WX)
  target_link_libraries(NativeWordSwitchTests PRIVATE SimpsonsRuntime)
  add_test(NAME OriginalWordSwitch COMMAND NativeWordSwitchTests "${CMAKE_SOURCE_DIR}/analysis/simpsons.pe")
  set_tests_properties(OriginalWordSwitch PROPERTIES TIMEOUT 15)
  add_executable(NativeStatusTests tests/test_native_status.cpp)
  target_compile_options(NativeStatusTests PRIVATE /fp:strict /W4 /WX)
  target_link_libraries(NativeStatusTests PRIVATE SimpsonsRuntime)
  add_test(NAME NativeStatusMapping COMMAND NativeStatusTests "${CMAKE_SOURCE_DIR}/analysis/simpsons.pe")
  set_tests_properties(NativeStatusMapping PROPERTIES TIMEOUT 15)
  add_executable(GraphicsCpuStartupTests tests/test_graphics_cpu_startup.cpp)
  target_compile_options(GraphicsCpuStartupTests PRIVATE /fp:strict /W4 /WX)
  target_link_libraries(GraphicsCpuStartupTests PRIVATE SimpsonsRuntime)
  add_test(NAME OriginalGraphicsCpuStartup COMMAND GraphicsCpuStartupTests "${CMAKE_SOURCE_DIR}/analysis/simpsons.pe")
  set_tests_properties(OriginalGraphicsCpuStartup PROPERTIES TIMEOUT 45)
  add_executable(FirstEffectLifecycleTests tests/test_first_effect_lifecycle.cpp)
  target_compile_options(FirstEffectLifecycleTests PRIVATE /fp:strict /W4 /WX)
  target_link_libraries(FirstEffectLifecycleTests PRIVATE SimpsonsRuntime)
  target_link_options(FirstEffectLifecycleTests PRIVATE /STACK:67108864,1048576)
  add_test(NAME OriginalFirstEffectLifecycle COMMAND FirstEffectLifecycleTests "${CMAKE_SOURCE_DIR}/analysis/simpsons.pe")
  set_tests_properties(OriginalFirstEffectLifecycle PROPERTIES TIMEOUT 60)
  add_executable(NativeNotificationTests tests/test_notifications.cpp)
  target_link_libraries(NativeNotificationTests PRIVATE SimpsonsRuntime)
  add_test(NAME NativeSystemNotifications COMMAND NativeNotificationTests)
  set_tests_properties(NativeSystemNotifications PROPERTIES TIMEOUT 30)
  add_executable(NativeMutantTests tests/test_mutants.cpp)
  target_link_libraries(NativeMutantTests PRIVATE SimpsonsRuntime)
  add_test(NAME NativeMutantOwnership COMMAND NativeMutantTests)
  set_tests_properties(NativeMutantOwnership PROPERTIES TIMEOUT 30)
  add_executable(NativeEventTests tests/test_events.cpp)
  target_compile_options(NativeEventTests PRIVATE /fp:strict /W4 /WX)
  target_link_libraries(NativeEventTests PRIVATE SimpsonsRuntime)
  add_test(NAME NativeEventOwnership COMMAND NativeEventTests "${CMAKE_SOURCE_DIR}/analysis/simpsons.pe")
  set_tests_properties(NativeEventOwnership PROPERTIES TIMEOUT 30)
  add_executable(AudioPcmTests tests/test_audio_pcm.cpp)
  target_link_libraries(AudioPcmTests PRIVATE SimpsonsRuntime)
  add_test(NAME OriginalAudioPcmConversion COMMAND AudioPcmTests "${CMAKE_SOURCE_DIR}/analysis/simpsons.pe")
  set_tests_properties(OriginalAudioPcmConversion PROPERTIES TIMEOUT 30)
  add_test(NAME GeneratorSemantics COMMAND "${Python3_EXECUTABLE}" -B "${CMAKE_SOURCE_DIR}/tests/test_generator_memory.py")
  set_tests_properties(GeneratorSemantics PROPERTIES TIMEOUT 90)
  add_test(NAME NativeHostFloatingPoint COMMAND "${Python3_EXECUTABLE}" -B "${CMAKE_SOURCE_DIR}/tests/test_host_fp.py")
  set_tests_properties(NativeHostFloatingPoint PROPERTIES TIMEOUT 120)
  add_executable(FilesystemTests tests/test_filesystem.cpp runtime/filesystem.cpp runtime/native_saves.cpp runtime/native_content.cpp)
  target_link_libraries(FilesystemTests PRIVATE bcrypt)
  target_compile_definitions(FilesystemTests PRIVATE SIMPSONS_FILESYSTEM_STANDALONE NOMINMAX WIN32_LEAN_AND_MEAN)
  target_include_directories(FilesystemTests PRIVATE "${CMAKE_SOURCE_DIR}" "${SIMPSONS_GENERATED_DIR}" "${CMAKE_SOURCE_DIR}/third_party/XenonRecomp/thirdparty/simde")
  add_test(NAME OriginalAssetFilesystem COMMAND FilesystemTests)
  add_executable(ThreadLifecycleTests tests/test_thread_lifecycle.cpp runtime/threads.cpp runtime/thread_objects.cpp runtime/kernel_objects.cpp runtime/guest_write_watch.cpp)
  target_compile_definitions(ThreadLifecycleTests PRIVATE SIMPSONS_THREAD_LIFECYCLE_STANDALONE NOMINMAX WIN32_LEAN_AND_MEAN)
  target_include_directories(ThreadLifecycleTests PRIVATE "${CMAKE_SOURCE_DIR}" "${SIMPSONS_GENERATED_DIR}" "${CMAKE_SOURCE_DIR}/third_party/XenonRecomp/thirdparty/simde")
  add_test(NAME NativeThreadLifecycle COMMAND ThreadLifecycleTests)
  set_tests_properties(NativeThreadLifecycle PROPERTIES TIMEOUT 60)
  add_executable(ThreadExitStatusTests tests/test_thread_exit_status.cpp)
  target_compile_options(ThreadExitStatusTests PRIVATE /fp:strict /W4 /WX)
  target_link_libraries(ThreadExitStatusTests PRIVATE SimpsonsRuntime)
  add_test(NAME OriginalThreadExitStatus COMMAND ThreadExitStatusTests "${CMAKE_SOURCE_DIR}/analysis/simpsons.pe")
  set_tests_properties(OriginalThreadExitStatus PROPERTIES TIMEOUT 60)
  add_executable(NativeGraphicsTests tests/test_native_graphics.cpp)
  target_link_libraries(NativeGraphicsTests PRIVATE SimpsonsGraphics)
  add_test(NAME NativeGraphicsResources COMMAND NativeGraphicsTests)
  add_test(NAME NativeGraphicsResourcesHardware COMMAND NativeGraphicsTests --hardware)
  add_executable(ViewportResetTests tests/test_viewport_reset.cpp)
  target_compile_options(ViewportResetTests PRIVATE /fp:strict /W4 /WX)
  target_link_libraries(ViewportResetTests PRIVATE SimpsonsRuntime)
  add_test(NAME OriginalViewportReset COMMAND ViewportResetTests "${CMAKE_SOURCE_DIR}/analysis/simpsons.pe")
  set_tests_properties(OriginalViewportReset PROPERTIES TIMEOUT 60)
  add_executable(ViewportCopyTests tests/test_viewport_copy.cpp)
  target_compile_options(ViewportCopyTests PRIVATE /fp:strict /W4 /WX)
  target_link_libraries(ViewportCopyTests PRIVATE SimpsonsRuntime)
  add_test(NAME OriginalViewportCopies COMMAND ViewportCopyTests "${CMAKE_SOURCE_DIR}/analysis/simpsons.pe")
  set_tests_properties(OriginalViewportCopies PROPERTIES TIMEOUT 60)
  add_executable(NativePresentationTests tests/test_native_presentation.cpp)
  target_link_libraries(NativePresentationTests PRIVATE SimpsonsGraphics user32)
  add_test(NAME NativePresentationContract COMMAND NativePresentationTests)
  set_tests_properties(NativePresentationContract PROPERTIES TIMEOUT 30)
  add_executable(NativeRecordingTests tests/test_native_recording.cpp)
  set(RECORDING_DEPTH_BINDING_HEADER "${SIMPSONS_SHADER_DIR}/PSRecordingDepthBinding.h")
  add_custom_command(OUTPUT "${RECORDING_DEPTH_BINDING_HEADER}"
    COMMAND "${SIMPSONS_FXC}" /nologo /Ges /Gis /O3 /T ps_5_0 /E PSRecordingDepthBinding
      /Fh "${RECORDING_DEPTH_BINDING_HEADER}" /Vn kPSRecordingDepthBinding
      "${CMAKE_SOURCE_DIR}/tests/recording_depth_binding.hlsl"
    DEPENDS tests/recording_depth_binding.hlsl VERBATIM)
  add_executable(RecordingDepthBindingTests tests/test_recording_depth_binding.cpp
    "${SIMPSONS_SHADER_DIR}/VSFlat.h" "${RECORDING_DEPTH_BINDING_HEADER}")
  target_include_directories(RecordingDepthBindingTests PRIVATE "${SIMPSONS_SHADER_DIR}")
  target_compile_options(RecordingDepthBindingTests PRIVATE /fp:strict /W4 /WX)
  target_link_libraries(RecordingDepthBindingTests PRIVATE SimpsonsGraphics)
  add_test(NAME NativeRecordingDepthBindingWARP COMMAND RecordingDepthBindingTests)
  add_test(NAME NativeRecordingDepthBindingHardware COMMAND RecordingDepthBindingTests --hardware)
  set_tests_properties(NativeRecordingDepthBindingWARP NativeRecordingDepthBindingHardware PROPERTIES TIMEOUT 60)
  add_executable(RecordingPayloadTests tests/test_recording_payload.cpp)
  target_include_directories(RecordingPayloadTests PRIVATE "${SIMPSONS_SHADER_DIR}")
  target_compile_options(RecordingPayloadTests PRIVATE /fp:strict /W4 /WX)
  target_link_libraries(RecordingPayloadTests PRIVATE SimpsonsGraphics)
  add_test(NAME NativeRecordingPayloadWARP COMMAND RecordingPayloadTests)
  add_test(NAME NativeRecordingPayloadHardware COMMAND RecordingPayloadTests --hardware)
  set_tests_properties(NativeRecordingPayloadWARP NativeRecordingPayloadHardware PROPERTIES TIMEOUT 60)
  target_link_libraries(NativeRecordingTests PRIVATE SimpsonsGraphics)
  add_test(NAME NativeRecordingOwnership COMMAND NativeRecordingTests)
  set_tests_properties(NativeRecordingOwnership PROPERTIES TIMEOUT 30)
  add_executable(ScreenShaderTests tests/test_screen_shaders.cpp)
  add_executable(OriginalScreenBackendTests tests/test_original_screen_backend.cpp)
  add_executable(Im2DShaderTests tests/test_im2d_shaders.cpp ${SIMPSONS_IM2D_HEADERS})
  target_include_directories(Im2DShaderTests PRIVATE "${SIMPSONS_SHADER_DIR}")
  target_compile_definitions(Im2DShaderTests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
  target_compile_options(Im2DShaderTests PRIVATE /fp:strict /W4 /WX)
  target_link_libraries(Im2DShaderTests PRIVATE SimpsonsGraphics)
  add_test(NAME OriginalIm2DShaderArithmetic COMMAND Im2DShaderTests)
  add_executable(Im2DVertexTests tests/test_im2d_vertices.cpp)
  target_compile_options(Im2DVertexTests PRIVATE /fp:strict /W4 /WX)
  target_link_libraries(Im2DVertexTests PRIVATE SimpsonsGraphics)
  add_test(NAME OriginalIm2DPackedVertices COMMAND Im2DVertexTests)
  add_executable(Im2DBackendTests tests/test_im2d_backend.cpp)
  target_compile_options(Im2DBackendTests PRIVATE /fp:strict /W4 /WX)
  target_link_libraries(Im2DBackendTests PRIVATE SimpsonsGraphics)
  add_test(NAME OriginalIm2DNativeDraw COMMAND Im2DBackendTests)
  add_custom_target(VerifyITXDFontFixture
    COMMAND "${Python3_EXECUTABLE}" -B "${CMAKE_SOURCE_DIR}/tools/prepare_itxd_font_fixture.py"
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}" VERBATIM)
  add_executable(ITXDBlocksTests tests/test_itxd_blocks.cpp)
  target_compile_options(ITXDBlocksTests PRIVATE /fp:strict /W4 /WX)
  target_link_libraries(ITXDBlocksTests PRIVATE SimpsonsGraphics)
  add_dependencies(ITXDBlocksTests VerifyITXDFontFixture)
  add_test(NAME OriginalITXDFontBlocks COMMAND ITXDBlocksTests
    "${CMAKE_SOURCE_DIR}/build/itxd-font/HighlanderStdBold6060b.tiled"
    "${CMAKE_SOURCE_DIR}/build/itxd-font/HighlanderStdBold6060b.bc2")
  set_tests_properties(OriginalITXDFontBlocks PROPERTIES TIMEOUT 30)
  add_custom_target(VerifyITXDBC3Fixture
    COMMAND "${Python3_EXECUTABLE}" -B "${CMAKE_SOURCE_DIR}/tools/prepare_itxd_bc3_fixture.py"
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}" VERBATIM)
  add_executable(ITXDBC3BlocksTests tests/test_itxd_bc3_blocks.cpp)
  target_compile_options(ITXDBC3BlocksTests PRIVATE /fp:strict /W4 /WX)
  target_link_libraries(ITXDBC3BlocksTests PRIVATE SimpsonsGraphics)
  add_dependencies(ITXDBC3BlocksTests VerifyITXDBC3Fixture)
  add_test(NAME OriginalITXDSharedLibraryBlocks COMMAND ITXDBC3BlocksTests
    "${CMAKE_SOURCE_DIR}/build/itxd-bc3/8_SharedLibrary.tiled"
    "${CMAKE_SOURCE_DIR}/build/itxd-bc3/8_SharedLibrary.bc3")
  set_tests_properties(OriginalITXDSharedLibraryBlocks PROPERTIES TIMEOUT 30)
  add_custom_target(VerifyITXDMipFixture
    COMMAND "${Python3_EXECUTABLE}" -B "${CMAKE_SOURCE_DIR}/tools/prepare_itxd_mip_fixture.py"
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
  add_executable(ITXDMipTests tests/test_itxd_mips.cpp)
  target_compile_options(ITXDMipTests PRIVATE /fp:strict /W4 /WX)
  target_link_libraries(ITXDMipTests PRIVATE SimpsonsGraphics)
  add_dependencies(ITXDMipTests VerifyITXDMipFixture)
  add_test(NAME OriginalITXDMipBlocks COMMAND ITXDMipTests "${CMAKE_SOURCE_DIR}/build/itxd-mips/cases.bin")
  set_tests_properties(OriginalITXDMipBlocks PROPERTIES TIMEOUT 30)
  add_custom_target(VerifyITXDRGBAFixture
    COMMAND "${Python3_EXECUTABLE}" -B "${CMAKE_SOURCE_DIR}/tools/prepare_itxd_rgba_fixture.py"
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}" VERBATIM)
  add_executable(ITXDRGBATests tests/test_itxd_rgba.cpp)
  target_compile_options(ITXDRGBATests PRIVATE /fp:strict /W4 /WX)
  target_link_libraries(ITXDRGBATests PRIVATE SimpsonsGraphics)
  add_custom_target(VerifyITXDTonalFixture
    COMMAND "${Python3_EXECUTABLE}" -B "${CMAKE_SOURCE_DIR}/tools/prepare_itxd_tonal_fixture.py"
    VERBATIM)
  add_custom_target(VerifyITXDBuildingsFixture
    COMMAND "${Python3_EXECUTABLE}" -B "${CMAKE_SOURCE_DIR}/tools/prepare_itxd_buildings_fixture.py"
    VERBATIM)
  add_dependencies(ITXDRGBATests VerifyITXDRGBAFixture VerifyITXDTonalFixture VerifyITXDBuildingsFixture)
  add_test(NAME OriginalITXDBuildingsRGBA COMMAND ITXDRGBATests "${CMAKE_SOURCE_DIR}/build/itxd-rgba/buildings.bin")
  add_test(NAME OriginalITXDTonalRGBA COMMAND ITXDRGBATests "${CMAKE_SOURCE_DIR}/build/itxd-rgba/tonal.bin")
  add_test(NAME OriginalITXDCandyRGBA COMMAND ITXDRGBATests "${CMAKE_SOURCE_DIR}/build/itxd-rgba/candy.bin")
  add_executable(ITXDLifecycleTests tests/test_itxd_lifecycle.cpp)
  add_custom_target(VerifyInputPromptFixture
    COMMAND "${Python3_EXECUTABLE}" -B "${CMAKE_SOURCE_DIR}/tools/prepare_input_prompt_fixture.py"
    VERBATIM)
  add_dependencies(ITXDLifecycleTests VerifyInputPromptFixture)
  add_test(NAME OriginalITXDInputPrompts COMMAND ITXDLifecycleTests
    "${CMAKE_SOURCE_DIR}/analysis/simpsons.pe" "${CMAKE_SOURCE_DIR}/build/native-input-prompts" buttons)
  set_tests_properties(OriginalITXDInputPrompts PROPERTIES TIMEOUT 60)
  add_custom_target(VerifyITXDPaletteFixture
    COMMAND "${Python3_EXECUTABLE}" -B "${CMAKE_SOURCE_DIR}/tools/prepare_itxd_palette_fixture.py"
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}" VERBATIM)
  add_custom_target(VerifyITXDDualPaletteFixture
    COMMAND "${Python3_EXECUTABLE}" -B "${CMAKE_SOURCE_DIR}/tools/prepare_itxd_dual_palette_fixture.py"
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}" VERBATIM)
  add_executable(ITXDPaletteTests tests/test_itxd_palette.cpp)
  add_custom_target(VerifyITXDLuminanceFixture
    COMMAND "${Python3_EXECUTABLE}" -B "${CMAKE_SOURCE_DIR}/tools/prepare_itxd_luminance_fixture.py"
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}" VERBATIM)
  add_custom_target(VerifyITXDLuminanceMipFixture
    COMMAND "${Python3_EXECUTABLE}" -B "${CMAKE_SOURCE_DIR}/tools/prepare_itxd_luminance_mip_fixture.py"
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}" VERBATIM)
  add_executable(ITXDLuminanceTests tests/test_itxd_luminance.cpp)
  target_compile_options(ITXDLuminanceTests PRIVATE /fp:strict /W4 /WX)
  target_link_libraries(ITXDLuminanceTests PRIVATE SimpsonsGraphics)
  add_dependencies(ITXDLuminanceTests VerifyITXDLuminanceFixture VerifyITXDLuminanceMipFixture)
  add_test(NAME OriginalITXDLuminancePixels COMMAND ITXDLuminanceTests
    "${CMAKE_SOURCE_DIR}/build/itxd-luminance/flare4pt.tiled"
    "${CMAKE_SOURCE_DIR}/build/itxd-luminance/flare4pt.rgba")
  add_test(NAME OriginalITXDLuminanceFamilyWARP COMMAND ITXDLuminanceTests --family
    "${CMAKE_SOURCE_DIR}/build/itxd-luminance/family.bin")
  add_test(NAME OriginalITXDLuminanceFamilyHardware COMMAND ITXDLuminanceTests --family
    "${CMAKE_SOURCE_DIR}/build/itxd-luminance/family.bin" --hardware)
  set_tests_properties(OriginalITXDLuminanceFamilyWARP OriginalITXDLuminanceFamilyHardware PROPERTIES TIMEOUT 30)
  add_test(NAME OriginalITXDLuminanceMipsWARP COMMAND ITXDLuminanceTests --mips
    "${CMAKE_SOURCE_DIR}/build/itxd-luminance-mip/family.bin")
  add_test(NAME OriginalITXDLuminanceMipsHardware COMMAND ITXDLuminanceTests --mips
    "${CMAKE_SOURCE_DIR}/build/itxd-luminance-mip/family.bin" --hardware)
  set_tests_properties(OriginalITXDLuminanceMipsWARP OriginalITXDLuminanceMipsHardware PROPERTIES TIMEOUT 30)
  add_test(NAME OriginalITXDLuminanceMipFixturePython
    COMMAND "${Python3_EXECUTABLE}" -B "${CMAKE_SOURCE_DIR}/tests/test_itxd_luminance_mip_fixture.py")
  set_tests_properties(OriginalITXDLuminanceMipFixturePython PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}" TIMEOUT 30)
  add_dependencies(ITXDPaletteTests VerifyITXDDualPaletteFixture)
  add_test(NAME OriginalITXDDualPalettePixels COMMAND ITXDPaletteTests
    "${CMAKE_SOURCE_DIR}/build/itxd-dual-palette/dual_simpsons_palette.tiled"
    "${CMAKE_SOURCE_DIR}/build/itxd-dual-palette/dual_simpsons_palette.rgba")
  target_compile_options(ITXDPaletteTests PRIVATE /fp:strict /W4 /WX)
  target_link_libraries(ITXDPaletteTests PRIVATE SimpsonsGraphics)
  add_dependencies(ITXDPaletteTests VerifyITXDPaletteFixture)
  add_test(NAME OriginalITXDPalettePixels COMMAND ITXDPaletteTests
    "${CMAKE_SOURCE_DIR}/build/itxd-palette/simpsons_palette.tiled"
    "${CMAKE_SOURCE_DIR}/build/itxd-palette/simpsons_palette.rgba")
  add_custom_target(VerifyITXDCacheFixture
    COMMAND "${Python3_EXECUTABLE}" -B "${CMAKE_SOURCE_DIR}/tools/prepare_itxd_cache_fixture.py"
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
  target_compile_options(ITXDLifecycleTests PRIVATE /fp:strict /W4 /WX)
  target_link_libraries(ITXDLifecycleTests PRIVATE SimpsonsRuntime)
  target_link_options(ITXDLifecycleTests PRIVATE /STACK:67108864,1048576)
  add_dependencies(ITXDLifecycleTests VerifyITXDFontFixture VerifyITXDBC3Fixture)
  add_dependencies(ITXDLifecycleTests VerifyITXDCacheFixture)
  add_dependencies(ITXDLifecycleTests VerifyITXDPaletteFixture)
  add_test(NAME OriginalITXDPaletteLifecycle COMMAND ITXDLifecycleTests
    "${CMAKE_SOURCE_DIR}/analysis/simpsons.pe" "${CMAKE_SOURCE_DIR}/build/itxd-palette" simpsons_palette)
  set_tests_properties(OriginalITXDPaletteLifecycle PROPERTIES TIMEOUT 180)
  add_test(NAME OriginalITXDCacheVariants COMMAND ITXDLifecycleTests
    "${CMAKE_SOURCE_DIR}/analysis/simpsons.pe" "${CMAKE_SOURCE_DIR}/build/itxd-cache" hud_target_center)
  set_tests_properties(OriginalITXDCacheVariants PROPERTIES TIMEOUT 180)
  add_test(NAME OriginalITXDLifecycle COMMAND ITXDLifecycleTests
    "${CMAKE_SOURCE_DIR}/analysis/simpsons.pe" "${CMAKE_SOURCE_DIR}/build/itxd-font")
  set_tests_properties(OriginalITXDLifecycle PROPERTIES TIMEOUT 180)
  add_test(NAME OriginalITXDBC3Lifecycle COMMAND ITXDLifecycleTests
    "${CMAKE_SOURCE_DIR}/analysis/simpsons.pe" "${CMAKE_SOURCE_DIR}/build/itxd-bc3" "8_SharedLibrary")
  set_tests_properties(OriginalITXDBC3Lifecycle PROPERTIES TIMEOUT 180)
  add_executable(OriginalScreenBridgeTests tests/test_original_screen_bridge.cpp)
  target_compile_options(OriginalScreenBridgeTests PRIVATE /fp:strict /W4 /WX)
  target_link_libraries(OriginalScreenBridgeTests PRIVATE SimpsonsRuntime)
  add_test(NAME OriginalScreenBridge COMMAND OriginalScreenBridgeTests "${CMAKE_SOURCE_DIR}/analysis/simpsons.pe")
  add_executable(OriginalScreenEffectTests tests/test_original_screen_effects.cpp)
  target_compile_options(OriginalScreenEffectTests PRIVATE /fp:strict /W4 /WX)
  target_link_libraries(OriginalScreenEffectTests PRIVATE SimpsonsRuntime)
  add_test(NAME OriginalScreenEffects COMMAND OriginalScreenEffectTests "${CMAKE_SOURCE_DIR}/analysis/simpsons.pe")
  add_test(NAME OriginalScreenEffectSites COMMAND "${Python3_EXECUTABLE}" -B "${CMAKE_SOURCE_DIR}/tools/generate_screen_effect_sites.py" --check)
  set_tests_properties(OriginalScreenBridge PROPERTIES TIMEOUT 60)
  target_compile_options(OriginalScreenBackendTests PRIVATE /fp:strict /W4 /WX)
  target_link_libraries(OriginalScreenBackendTests PRIVATE SimpsonsGraphics)
  add_test(NAME OriginalScreenBackend COMMAND OriginalScreenBackendTests)
  target_link_libraries(ScreenShaderTests PRIVATE SimpsonsGraphics)
  add_test(NAME NativeScreenShaders COMMAND ScreenShaderTests)
  add_executable(NativeTextureStreamTests tests/test_native_texture_stream.cpp)
  target_link_libraries(NativeTextureStreamTests PRIVATE SimpsonsGraphics)
  add_test(NAME OriginalNativeTextureStream COMMAND NativeTextureStreamTests "${CMAKE_SOURCE_DIR}/analysis/simpsons.pe")
  add_executable(DriverResourcesTests tests/test_driver_resources.cpp)
  target_link_libraries(DriverResourcesTests PRIVATE SimpsonsGraphics)
  add_test(NAME NativeDriverResources COMMAND DriverResourcesTests)
  add_executable(MaterialResourcesTests tests/test_material_resources.cpp)
  target_link_libraries(MaterialResourcesTests PRIVATE SimpsonsGraphics)
  add_test(NAME NativeMaterialOwnership COMMAND MaterialResourcesTests "${CMAKE_SOURCE_DIR}/analysis/simpsons.pe")
  add_test(NAME OriginalScreenShaderEvidence COMMAND "${Python3_EXECUTABLE}" -B -m unittest discover -s "${CMAKE_SOURCE_DIR}/tests" -p test_screen_shaders.py -v)
  add_executable(NativeMaterialCompilerTests tests/test_native_material_compiler.cpp)
  target_link_libraries(NativeMaterialCompilerTests PRIVATE SimpsonsGraphics)
  add_test(NAME NativeOriginalMaterialArtifacts COMMAND NativeMaterialCompilerTests "${CMAKE_SOURCE_DIR}/analysis/simpsons.pe")
  add_executable(DeclarationResourcesTests tests/test_declaration_resources.cpp)
  target_link_libraries(DeclarationResourcesTests PRIVATE SimpsonsGraphics)
  add_test(NAME NativeDeclarationOwnership COMMAND DeclarationResourcesTests "${CMAKE_SOURCE_DIR}/analysis/simpsons.pe")
  add_executable(EngineStateTests tests/test_engine_state.cpp)
  target_link_libraries(EngineStateTests PRIVATE SimpsonsGraphics)
  add_test(NAME NativeEngineState COMMAND EngineStateTests "${CMAKE_SOURCE_DIR}/analysis/simpsons.pe")
  add_executable(EngineStateBridgeTests tests/test_engine_state_bridge.cpp)
  target_link_libraries(EngineStateBridgeTests PRIVATE SimpsonsRuntime)
  add_test(NAME OriginalEngineStateBridge COMMAND EngineStateBridgeTests "${CMAKE_SOURCE_DIR}/analysis/simpsons.pe")
  add_executable(EngineResourceBridgeTests tests/test_engine_resource_bridge.cpp)
  target_link_libraries(EngineResourceBridgeTests PRIVATE SimpsonsRuntime)
  add_test(NAME OriginalEngineResourceBridge COMMAND EngineResourceBridgeTests "${CMAKE_SOURCE_DIR}/analysis/simpsons.pe")
  add_executable(EngineDynamicBuffersTests tests/test_engine_dynamic_buffers.cpp runtime/engine_dynamic_buffers.cpp)
  target_compile_definitions(EngineDynamicBuffersTests PRIVATE SIMPSONS_DYNAMIC_BUFFERS_STANDALONE NOMINMAX WIN32_LEAN_AND_MEAN)
  target_include_directories(EngineDynamicBuffersTests PRIVATE "${CMAKE_SOURCE_DIR}" "${SIMPSONS_GENERATED_DIR}" "${CMAKE_SOURCE_DIR}/third_party/XenonRecomp/thirdparty/simde")
  target_compile_options(EngineDynamicBuffersTests PRIVATE /clang:-mssse3)
  target_link_libraries(EngineDynamicBuffersTests PRIVATE SimpsonsGraphics user32 bcrypt)
  add_test(NAME NativeEngineDynamicBuffers COMMAND EngineDynamicBuffersTests "${CMAKE_SOURCE_DIR}/analysis/simpsons.pe")
  add_executable(EnginePipelineResourcesTests tests/test_engine_pipeline_resources.cpp)
  target_compile_options(EnginePipelineResourcesTests PRIVATE /fp:strict /W4 /WX)
  target_link_libraries(EnginePipelineResourcesTests PRIVATE SimpsonsRuntime)
  add_test(NAME OriginalPipelineResources COMMAND EnginePipelineResourcesTests "${CMAKE_SOURCE_DIR}/analysis/simpsons.pe")
  foreach(PIPELINE_CAPABILITY_CASE RANGE 0 7)
    add_test(NAME OriginalPipelineCapabilityWARP_${PIPELINE_CAPABILITY_CASE}
      COMMAND EnginePipelineResourcesTests "${CMAKE_SOURCE_DIR}/analysis/simpsons.pe" ${PIPELINE_CAPABILITY_CASE})
    add_test(NAME OriginalPipelineCapabilityHardware_${PIPELINE_CAPABILITY_CASE}
      COMMAND EnginePipelineResourcesTests "${CMAKE_SOURCE_DIR}/analysis/simpsons.pe" ${PIPELINE_CAPABILITY_CASE} --hardware)
    set_tests_properties(OriginalPipelineCapabilityWARP_${PIPELINE_CAPABILITY_CASE}
      OriginalPipelineCapabilityHardware_${PIPELINE_CAPABILITY_CASE} PROPERTIES TIMEOUT 60 RUN_SERIAL TRUE)
  endforeach()
  add_test(NAME OriginalPipelineCapabilityContract COMMAND "${Python3_EXECUTABLE}" -B
    "${CMAKE_SOURCE_DIR}/tools/analyze_pipeline_capability_contract.py")
  add_executable(EngineDriverTests tests/test_engine_driver.cpp)
  target_link_libraries(EngineDriverTests PRIVATE SimpsonsRuntime)
  target_link_options(EngineDriverTests PRIVATE /STACK:67108864,1048576)
  add_test(NAME OriginalDriverLifecycle COMMAND EngineDriverTests "${CMAKE_SOURCE_DIR}/analysis/simpsons.pe")
  set_tests_properties(OriginalDriverLifecycle PROPERTIES TIMEOUT 30)
  add_test(NAME OriginalExpandedBlendEvidence COMMAND "${Python3_EXECUTABLE}" -B -m unittest discover -s "${CMAKE_SOURCE_DIR}/tests" -p test_expanded_blend.py -v)
  add_executable(NativeViewportTests tests/test_native_viewport.cpp)
  target_link_libraries(NativeViewportTests PRIVATE SimpsonsGraphics bcrypt)
  add_test(NAME NativeViewportContract COMMAND NativeViewportTests "${CMAKE_SOURCE_DIR}/analysis/simpsons.pe")
  add_test(NAME OriginalPostStartEvidence COMMAND "${Python3_EXECUTABLE}" -B "${CMAKE_SOURCE_DIR}/tools/analyze_poststart_integration.py" --self-test)
  add_test(NAME OriginalCameraRasterEvidence COMMAND "${Python3_EXECUTABLE}" -B "${CMAKE_SOURCE_DIR}/tools/analyze_camera_rasters.py" --self-test)
  add_test(NAME OriginalLoadingTextureEvidence COMMAND "${Python3_EXECUTABLE}" -B "${CMAKE_SOURCE_DIR}/tools/analyze_texture_bridge.py" --self-test)
  add_test(NAME OriginalAudioCatalogPython COMMAND "${Python3_EXECUTABLE}" -B -m unittest discover -s "${CMAKE_SOURCE_DIR}/tests" -p test_audio_catalog.py -v)
  add_test(NAME OriginalStreamedLoopCatalogPython COMMAND "${Python3_EXECUTABLE}" -B -m unittest discover -s "${CMAKE_SOURCE_DIR}/tests" -p test_streamed_loop_catalog.py -v)
  add_test(NAME OriginalResidentAudioCatalogPython COMMAND "${Python3_EXECUTABLE}" -B -m unittest discover -s "${CMAKE_SOURCE_DIR}/tests" -p test_resident_audio_catalog.py -v)
  add_test(NAME OriginalAmxAudioCatalogPython COMMAND "${Python3_EXECUTABLE}" -B -m unittest discover -s "${CMAKE_SOURCE_DIR}/tests" -p test_amx_audio_catalog.py -v)
endif()
