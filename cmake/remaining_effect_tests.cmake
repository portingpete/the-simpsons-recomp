set(SIMPSONS_REMAINING_RIGID_FIXTURES "${SIMPSONS_SHADER_DIR}/rigid_remaining_fixtures.h")
add_custom_command(OUTPUT "${SIMPSONS_REMAINING_RIGID_FIXTURES}"
  COMMAND "${Python3_EXECUTABLE}" -B "${CMAKE_SOURCE_DIR}/tests/test_rigid_remaining_shader.py"
    --emit-fixtures "${SIMPSONS_REMAINING_RIGID_FIXTURES}"
  DEPENDS tests/test_rigid_remaining_shader.py tools/analyze_rigid_remaining_shader.py
    ${SIMPSONS_REMAINING_ANALYZER_DEPS} VERBATIM)
set(SIMPSONS_SKIN_VARIANTS_FIXTURES "${SIMPSONS_SHADER_DIR}/skin_variants_fixtures.h")
add_custom_command(OUTPUT "${SIMPSONS_SKIN_VARIANTS_FIXTURES}"
  COMMAND "${Python3_EXECUTABLE}" -B "${CMAKE_SOURCE_DIR}/tests/test_skin_variants_shader.py"
    --emit-cpp "${SIMPSONS_SKIN_VARIANTS_FIXTURES}"
  DEPENDS tests/test_skin_variants_shader.py tests/test_skin_variant_reciprocal.py tools/analyze_skin_variants_shader.py
    tools/skin_variants_pins.py tests/test_skin_dualalpha_shader.py
    tools/analyze_skin_dualalpha_shader.py tools/analyze_skin_dualtextured_shader.py
    tools/analyze_skin_alpha_shader.py tools/analyze_skin_shader.py tools/skin_shader_emit.py tools/analyze_168f8alpha_shader.py
    ${SIMPSONS_REMAINING_ANALYZER_DEPS} VERBATIM)
set(SIMPSONS_SKIN_DUAL_UV_FIXTURES "${SIMPSONS_SHADER_DIR}/skin_dualtextured_uv_fixtures.h")
add_custom_command(OUTPUT "${SIMPSONS_SKIN_DUAL_UV_FIXTURES}"
  COMMAND "${Python3_EXECUTABLE}" -B "${CMAKE_SOURCE_DIR}/tests/test_skin_dualtextured_uv_shader.py"
    --emit-cpp "${SIMPSONS_SKIN_DUAL_UV_FIXTURES}"
  DEPENDS tests/test_skin_dualtextured_uv_shader.py tools/analyze_skin_dualtextured_uv_shader.py
    tests/test_skin_dualalpha_shader.py tools/analyze_skin_dualalpha_shader.py
    tools/analyze_skin_dualtextured_shader.py tools/analyze_skin_alpha_shader.py
    tools/analyze_skin_shader.py tools/skin_shader_emit.py tools/analyze_168f8alpha_shader.py
    ${SIMPSONS_REMAINING_ANALYZER_DEPS} VERBATIM)
add_executable(RigidRemainingShaderTests tests/test_rigid_remaining_gpu.cpp
  "${SIMPSONS_REMAINING_RIGID_FIXTURES}" ${SIMPSONS_REMAINING_RIGID_HEADERS}
  "${SIMPSONS_SHADER_DIR}/VSChocolate.h" "${SIMPSONS_SHADER_DIR}/PSChocolate.h"
  "${SIMPSONS_SHADER_DIR}/PSChocolateDraw.h")
add_executable(SkinVariantsShaderTests tests/test_skin_variants_gpu.cpp
  "${SIMPSONS_SKIN_VARIANTS_FIXTURES}" ${SIMPSONS_SKIN_VARIANTS_HEADERS})
add_executable(SkinDualUVShaderTests tests/test_skin_dualtextured_uv_gpu.cpp
  "${SIMPSONS_SKIN_DUAL_UV_FIXTURES}" ${SIMPSONS_SKIN_DUAL_UV_HEADERS})
# Generated files must be direct object dependencies as well as target inputs.
# Windows Clang depfiles may use a second spelling of the same shared path.
set_property(SOURCE tests/test_rigid_remaining_gpu.cpp APPEND PROPERTY OBJECT_DEPENDS
  "${SIMPSONS_REMAINING_RIGID_FIXTURES};${SIMPSONS_REMAINING_RIGID_HEADERS};${SIMPSONS_SHADER_DIR}/VSChocolate.h;${SIMPSONS_SHADER_DIR}/PSChocolate.h;${SIMPSONS_SHADER_DIR}/PSChocolateDraw.h")
set_property(SOURCE tests/test_skin_variants_gpu.cpp APPEND PROPERTY OBJECT_DEPENDS
  "${SIMPSONS_SKIN_VARIANTS_FIXTURES};${SIMPSONS_SKIN_VARIANTS_HEADERS}")
set_property(SOURCE tests/test_skin_dualtextured_uv_gpu.cpp APPEND PROPERTY OBJECT_DEPENDS
  "${SIMPSONS_SKIN_DUAL_UV_FIXTURES};${SIMPSONS_SKIN_DUAL_UV_HEADERS}")
set_property(SOURCE tests/test_rigid_mesh.cpp APPEND PROPERTY OBJECT_DEPENDS
  "${SIMPSONS_RIGID_HEADERS};${SIMPSONS_CHOCOLATE_UV_PROBE_HEADER}")
foreach(TARGET RigidRemainingShaderTests SkinVariantsShaderTests SkinDualUVShaderTests)
  target_include_directories(${TARGET} PRIVATE "${CMAKE_SOURCE_DIR}" "${SIMPSONS_SHADER_DIR}")
  target_compile_definitions(${TARGET} PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
  target_compile_options(${TARGET} PRIVATE /fp:strict /W4 /WX)
  target_link_libraries(${TARGET} PRIVATE d3d11 dxgi)
endforeach()
foreach(FAMILY RigidRemaining SkinVariants SkinDualUV)
  add_test(NAME Native${FAMILY}ShaderWARP COMMAND ${FAMILY}ShaderTests)
  add_test(NAME Native${FAMILY}ShaderHardware COMMAND ${FAMILY}ShaderTests --hardware)
  set_tests_properties(Native${FAMILY}ShaderWARP Native${FAMILY}ShaderHardware PROPERTIES TIMEOUT 60)
endforeach()
add_test(NAME OriginalRigidRemainingShader COMMAND "${Python3_EXECUTABLE}" -B
  "${CMAKE_SOURCE_DIR}/tests/test_rigid_remaining_shader.py" -v)
add_test(NAME OriginalSkinVariantsShader COMMAND "${Python3_EXECUTABLE}" -B
  "${CMAKE_SOURCE_DIR}/tests/test_skin_variants_shader.py" -v)
add_test(NAME OriginalSkinDualUVShader COMMAND "${Python3_EXECUTABLE}" -B
  "${CMAKE_SOURCE_DIR}/tests/test_skin_dualtextured_uv_shader.py" -v)
add_executable(RemainingRigidPassTests tests/test_rigid_remaining_pass.cpp)
add_executable(SkinVariantsPassTests tests/test_skin_variants_pass.cpp)
add_executable(OriginalSkinDualUVPassTests tests/test_skin_dualtextured_uv_pass.cpp)
add_executable(SkinDualUVMaterialTests tests/test_skin_dualtextured_uv_material.cpp)
add_executable(SkinVariantsMaterialTests tests/test_skin_variants_material.cpp)
target_compile_options(SkinVariantsMaterialTests PRIVATE /fp:strict /W4 /WX)
target_link_libraries(SkinVariantsMaterialTests PRIVATE SimpsonsRuntime)
add_test(NAME OriginalSkinVariantsMaterial COMMAND SkinVariantsMaterialTests "${CMAKE_SOURCE_DIR}/analysis/simpsons.pe")
target_compile_options(SkinDualUVMaterialTests PRIVATE /fp:strict /W4 /WX)
target_link_libraries(SkinDualUVMaterialTests PRIVATE SimpsonsRuntime)
add_test(NAME OriginalSkinDualUVMaterial COMMAND SkinDualUVMaterialTests "${CMAKE_SOURCE_DIR}/analysis/simpsons.pe")
foreach(TARGET RemainingRigidPassTests SkinVariantsPassTests OriginalSkinDualUVPassTests)
  target_compile_options(${TARGET} PRIVATE /fp:strict /W4 /WX)
  target_link_libraries(${TARGET} PRIVATE SimpsonsRuntime)
  target_link_options(${TARGET} PRIVATE /STACK:67108864,1048576)
endforeach()
foreach(FAMILY chocolate projtex)
  if(FAMILY STREQUAL "chocolate")
    set(LABEL Chocolate)
  else()
    set(LABEL Projtex)
  endif()
  add_test(NAME Original${LABEL}Pass COMMAND RemainingRigidPassTests
    "${CMAKE_SOURCE_DIR}/analysis/simpsons.pe" "${CMAKE_SOURCE_DIR}/build/itxd-palette/loc_split4.itxd" ${FAMILY})
  set_tests_properties(Original${LABEL}Pass PROPERTIES TIMEOUT 60 RUN_SERIAL TRUE)
endforeach()
foreach(FAMILY gloss flipbook)
  if(FAMILY STREQUAL "gloss")
    set(LABEL Gloss)
  else()
    set(LABEL Flipbook)
  endif()
  add_test(NAME OriginalSkin${LABEL}Pass COMMAND SkinVariantsPassTests
    "${CMAKE_SOURCE_DIR}/analysis/simpsons.pe" "${CMAKE_SOURCE_DIR}/build/itxd-palette/loc_split4.itxd" ${FAMILY})
  set_tests_properties(OriginalSkin${LABEL}Pass PROPERTIES TIMEOUT 60 RUN_SERIAL TRUE)
endforeach()
add_test(NAME OriginalSkinDualUVPass COMMAND OriginalSkinDualUVPassTests
  "${CMAKE_SOURCE_DIR}/analysis/simpsons.pe" "${CMAKE_SOURCE_DIR}/build/itxd-palette/loc_split4.itxd" dual-uv)
set_tests_properties(OriginalSkinDualUVPass PROPERTIES TIMEOUT 60 RUN_SERIAL TRUE)
add_executable(SkinVariantsMeshTests tests/test_skin_variants_mesh.cpp)
target_compile_options(SkinVariantsMeshTests PRIVATE /fp:strict /W4 /WX)
target_link_libraries(SkinVariantsMeshTests PRIVATE SimpsonsGraphics)
add_test(NAME NativeSkinVariantsMeshWARP COMMAND SkinVariantsMeshTests "${CMAKE_SOURCE_DIR}/analysis/simpsons.pe")
add_test(NAME NativeSkinVariantsMeshHardware COMMAND SkinVariantsMeshTests "${CMAKE_SOURCE_DIR}/analysis/simpsons.pe" --hardware)
set_tests_properties(NativeSkinVariantsMeshWARP NativeSkinVariantsMeshHardware PROPERTIES TIMEOUT 60)
