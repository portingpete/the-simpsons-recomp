# Exact original shader identities for the remaining game material variants.
set(SIMPSONS_REMAINING_ANALYZER_DEPS
  tools/analyze_rigid_shader.py tools/analyze_screen_shaders.py tools/analyze_edge_shaders.py
  tools/analyze_fourtap_shaders.py tools/analyze_chocolate_shader.py
  tools/analyze_rigid_uv_shader.py tools/analyze_post_effect_catalog.py
  tools/analyze_effect_catalog.py tools/analyze_poststart_integration.py analysis/simpsons.pe)
function(simpsons_remaining_compile ENTRY SOURCE OUTPUT_LIST)
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
      /I "${SIMPSONS_SHADER_DIR}" /I "${CMAKE_SOURCE_DIR}/renderer"
      /Fh "${HEADER}" /Vn "k${ENTRY}" "${SOURCE}"
    DEPENDS "${SOURCE}" ${ARGN} VERBATIM)
  set(${OUTPUT_LIST} ${${OUTPUT_LIST}} "${HEADER}" PARENT_SCOPE)
endfunction()
set(SIMPSONS_REMAINING_RIGID_SOURCE "${SIMPSONS_SHADER_DIR}/rigid_remaining_shader.hlsl")
add_custom_command(OUTPUT "${SIMPSONS_REMAINING_RIGID_SOURCE}"
  COMMAND "${Python3_EXECUTABLE}" -B "${CMAKE_SOURCE_DIR}/tools/analyze_rigid_remaining_shader.py"
    --emit-hlsl "${SIMPSONS_REMAINING_RIGID_SOURCE}"
  DEPENDS tools/analyze_rigid_remaining_shader.py ${SIMPSONS_REMAINING_ANALYZER_DEPS} VERBATIM)
foreach(ENTRY VSChocolateOpaque PSChocolateOpaque VSProjtex PSProjtex VSProjtexAlpha PSProjtexAlpha)
  simpsons_remaining_compile(${ENTRY} "${SIMPSONS_REMAINING_RIGID_SOURCE}" SIMPSONS_REMAINING_RIGID_HEADERS)
endforeach()
foreach(ENTRY PSChocolateOpaqueDraw PSProjtexDraw)
  simpsons_remaining_compile(${ENTRY} "${CMAKE_SOURCE_DIR}/renderer/rigid_remaining_draw.hlsl"
    SIMPSONS_REMAINING_RIGID_HEADERS "${SIMPSONS_REMAINING_RIGID_SOURCE}"
    renderer/rigid_remaining_shader.hlsl renderer/shadow_mesh.hlsl)
endforeach()
simpsons_remaining_compile(PSProjtexAlphaDraw "${CMAKE_SOURCE_DIR}/renderer/projtex_alpha_draw.hlsl"
  SIMPSONS_REMAINING_RIGID_HEADERS "${SIMPSONS_REMAINING_RIGID_SOURCE}"
  renderer/rigid_remaining_shader.hlsl renderer/shadow_mesh.hlsl)
foreach(NAME ChocolateOpaque Projtex ProjtexAlpha)
  foreach(ENTRY GS${NAME}Probe VS${NAME}PixelProbe)
    simpsons_remaining_compile(${ENTRY} "${CMAKE_SOURCE_DIR}/renderer/rigid_remaining_probe.hlsl"
      SIMPSONS_REMAINING_RIGID_HEADERS "${SIMPSONS_REMAINING_RIGID_SOURCE}" renderer/rigid_remaining_shader.hlsl)
  endforeach()
endforeach()
foreach(ENTRY GSChocolateAlphaFullProbe VSChocolateAlphaPixelProbe)
  simpsons_remaining_compile(${ENTRY} "${CMAKE_SOURCE_DIR}/renderer/remaining_chocolate_alpha_probe.hlsl"
    SIMPSONS_REMAINING_RIGID_HEADERS "${SIMPSONS_CHOCOLATE_SOURCE}")
endforeach()
foreach(FAMILY gloss flipbook)
  if(FAMILY STREQUAL "gloss")
    set(NAME SkinGloss)
  else()
    set(NAME SkinFlipbook)
  endif()
  set(SOURCE "${SIMPSONS_SHADER_DIR}/skin_${FAMILY}_shader.hlsl")
  add_custom_command(OUTPUT "${SOURCE}"
    COMMAND "${Python3_EXECUTABLE}" -B "${CMAKE_SOURCE_DIR}/tools/analyze_skin_variants_shader.py"
      ${FAMILY} --emit-hlsl "${SOURCE}"
    DEPENDS tools/analyze_skin_variants_shader.py tools/skin_variants_pins.py
      ${SIMPSONS_REMAINING_ANALYZER_DEPS} VERBATIM)
  foreach(SUFFIX "" Alpha)
    foreach(ENTRY VS${NAME}${SUFFIX} PS${NAME}${SUFFIX} GS${NAME}${SUFFIX}Probe
        VS${NAME}${SUFFIX}PixelProbe PS${NAME}${SUFFIX}Draw)
      simpsons_remaining_compile(${ENTRY} "${SOURCE}" SIMPSONS_SKIN_VARIANTS_HEADERS renderer/shadow_mesh.hlsl)
    endforeach()
  endforeach()
endforeach()
set(SIMPSONS_SKIN_DUAL_UV_SOURCE "${SIMPSONS_SHADER_DIR}/skin_dualtextured_uv_shader.hlsl")
add_custom_command(OUTPUT "${SIMPSONS_SKIN_DUAL_UV_SOURCE}"
  COMMAND "${Python3_EXECUTABLE}" -B "${CMAKE_SOURCE_DIR}/tools/analyze_skin_dualtextured_uv_shader.py"
    --emit-hlsl "${SIMPSONS_SKIN_DUAL_UV_SOURCE}"
  DEPENDS tools/analyze_skin_dualtextured_uv_shader.py tools/skin_shader_emit.py
    ${SIMPSONS_REMAINING_ANALYZER_DEPS} VERBATIM)
foreach(ENTRY VSSkinDualUV PSSkinDualUV VSSkinDualUVAlpha PSSkinDualUVAlpha
    GSSkinDualUVProbe GSSkinDualUVAlphaProbe VSSkinDualUVPixelProbe VSSkinDualUVAlphaPixelProbe
    PSSkinDualUVDraw PSSkinDualUVAlphaDraw)
  set(SOURCE "${SIMPSONS_SKIN_DUAL_UV_SOURCE}")
  if(ENTRY STREQUAL "PSSkinDualUVDraw")
    set(SOURCE "${CMAKE_SOURCE_DIR}/renderer/skin_dualtextured_uv_draw.hlsl")
  elseif(ENTRY STREQUAL "PSSkinDualUVAlphaDraw")
    set(SOURCE "${CMAKE_SOURCE_DIR}/renderer/skin_dualtextured_uv_alpha_draw.hlsl")
  elseif(ENTRY MATCHES "Probe$")
    set(SOURCE "${CMAKE_SOURCE_DIR}/renderer/skin_dualtextured_uv_probe.hlsl")
  endif()
  simpsons_remaining_compile(${ENTRY} "${SOURCE}" SIMPSONS_SKIN_DUAL_UV_HEADERS
    "${SIMPSONS_SKIN_DUAL_UV_SOURCE}" renderer/skin_dualtextured_uv_shader.hlsl renderer/shadow_mesh.hlsl)
endforeach()
list(APPEND SIMPSONS_RIGID_HEADERS ${SIMPSONS_REMAINING_RIGID_HEADERS}
  ${SIMPSONS_SKIN_VARIANTS_HEADERS} ${SIMPSONS_SKIN_DUAL_UV_HEADERS})
set_property(SOURCE renderer/skin_mesh.cpp APPEND PROPERTY OBJECT_DEPENDS
  "${SIMPSONS_SKIN_VARIANTS_HEADERS};${SIMPSONS_SKIN_DUAL_UV_HEADERS}")
