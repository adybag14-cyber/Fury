add_executable(fury_render_tests render_tests.cpp)
target_link_libraries(fury_render_tests PRIVATE Fury::Engine)
fury_stage_runtime(fury_render_tests)
add_test(NAME rendering_math COMMAND fury_render_tests)
add_executable(fury_gltf_tests gltf_tests.cpp)
target_link_libraries(fury_gltf_tests PRIVATE Fury::Engine)
fury_stage_runtime(fury_gltf_tests)
add_test(NAME gltf_import COMMAND fury_gltf_tests)
add_executable(fury_settings_tests settings_tests.cpp)
target_link_libraries(fury_settings_tests PRIVATE Fury::Engine)
fury_stage_runtime(fury_settings_tests)
add_test(NAME rendering_settings COMMAND fury_settings_tests)

add_executable(fury_audio_tests audio_tests.cpp)
target_link_libraries(fury_audio_tests PRIVATE Fury::Engine)
target_compile_definitions(fury_audio_tests PRIVATE FURY_TEST_AUDIO_ASSETS="${PROJECT_SOURCE_DIR}/assets/audio/meridian")
fury_stage_runtime(fury_audio_tests)
add_test(NAME audio_cpu COMMAND fury_audio_tests)

add_executable(fury_software_raster_tests software_raster_tests.cpp)
target_link_libraries(fury_software_raster_tests PRIVATE Fury::Engine)
fury_stage_runtime(fury_software_raster_tests)
add_test(NAME software_rasterization COMMAND fury_software_raster_tests)

add_executable(fury_asset_pipeline_tests asset_pipeline_tests.cpp ../apps/vaultline/harbor_assets.cpp)
target_link_libraries(fury_asset_pipeline_tests PRIVATE Fury::Engine)
fury_stage_runtime(fury_asset_pipeline_tests)
add_test(NAME asset_pipeline COMMAND fury_asset_pipeline_tests)
set_tests_properties(asset_pipeline PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")

add_executable(fury_cpu_ray_tests cpu_ray_tests.cpp)
target_link_libraries(fury_cpu_ray_tests PRIVATE Fury::Engine)
fury_stage_runtime(fury_cpu_ray_tests)
add_test(NAME cpu_ray_tracing COMMAND fury_cpu_ray_tests)

if(TARGET fury_renderlab)
  add_test(NAME cpu_renderlab_runtime COMMAND fury_renderlab --backend cpu-ray
    --width 160 --height 90 --frames 2 --warmup 0 --hidden)
  set_tests_properties(cpu_renderlab_runtime PROPERTIES ENVIRONMENT "SDL_VIDEODRIVER=dummy;FURY_CPU_THREADS=2" TIMEOUT 60)
endif()
if(TARGET vaultline)
  add_test(NAME cpu_vaultline_runtime COMMAND vaultline --cpu-ray --smoke
    --width 160 --height 90 --frames 2)
  set_tests_properties(cpu_vaultline_runtime PROPERTIES
    ENVIRONMENT "SDL_VIDEODRIVER=dummy;SDL_AUDIODRIVER=dummy;FURY_CPU_THREADS=2;FURY_AUDIO_BACKEND=cpu"
    WORKING_DIRECTORY "$<TARGET_FILE_DIR:vaultline>" TIMEOUT 90)
endif()

add_executable(fury_surface_detail_tests surface_detail_tests.cpp)
target_link_libraries(fury_surface_detail_tests PRIVATE Fury::Engine)
fury_stage_runtime(fury_surface_detail_tests)
add_test(NAME surface_detail COMMAND fury_surface_detail_tests)

add_executable(fury_surface_sampling_tests surface_sampling_tests.cpp)
target_link_libraries(fury_surface_sampling_tests PRIVATE Fury::Engine)
fury_stage_runtime(fury_surface_sampling_tests)
add_test(NAME surface_sampling COMMAND fury_surface_sampling_tests)

add_executable(fury_gl_material_tests gl_material_tests.cpp)
target_link_libraries(fury_gl_material_tests PRIVATE Fury::Engine)
fury_stage_runtime(fury_gl_material_tests)
find_program(FURY_XVFB_RUN xvfb-run)
if(UNIX AND FURY_XVFB_RUN)
  add_test(NAME gl_materials COMMAND ${FURY_XVFB_RUN} -a $<TARGET_FILE:fury_gl_material_tests>)
else()
  add_test(NAME gl_materials COMMAND fury_gl_material_tests)
endif()
set_tests_properties(gl_materials PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 90)

add_executable(fury_hero_surface_tests hero_surface_tests.cpp
  ../apps/vaultline/harbor_assets.cpp ../apps/renderlab/coastal_scene.cpp)
target_link_libraries(fury_hero_surface_tests PRIVATE Fury::Engine)
fury_stage_runtime(fury_hero_surface_tests)
add_test(NAME hero_surfaces COMMAND fury_hero_surface_tests)
set_tests_properties(hero_surfaces PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")

find_package(Python3 QUIET COMPONENTS Interpreter)
if(Python3_Interpreter_FOUND)
  add_test(NAME dxr_planar_footprint COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/dxr_planar_footprint_tests.py)
endif()

add_executable(fury_surface_attachment_tests surface_attachment_tests.cpp)
target_link_libraries(fury_surface_attachment_tests PRIVATE Fury::Engine)
fury_stage_runtime(fury_surface_attachment_tests)
add_test(NAME surface_attachment COMMAND fury_surface_attachment_tests)

add_executable(fury_world_architecture_tests world_architecture_tests.cpp
  ../apps/vaultline/world_architecture.cpp)
target_link_libraries(fury_world_architecture_tests PRIVATE Fury::Engine)
fury_stage_runtime(fury_world_architecture_tests)
add_test(NAME world_architecture COMMAND fury_world_architecture_tests)

add_executable(fury_world_ground_tests world_ground_tests.cpp
  ../apps/vaultline/world_ground.cpp)
target_link_libraries(fury_world_ground_tests PRIVATE Fury::Engine)
fury_stage_runtime(fury_world_ground_tests)
add_test(NAME world_ground COMMAND fury_world_ground_tests)

add_executable(fury_world_life_tests world_life_tests.cpp
  ../apps/vaultline/world_life.cpp)
target_link_libraries(fury_world_life_tests PRIVATE Fury::Engine)
fury_stage_runtime(fury_world_life_tests)
add_test(NAME world_life COMMAND fury_world_life_tests)
set_tests_properties(world_life PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")

add_executable(fury_world_visibility_tests world_visibility_tests.cpp)
target_link_libraries(fury_world_visibility_tests PRIVATE Fury::Engine)
fury_stage_runtime(fury_world_visibility_tests)
add_test(NAME world_visibility COMMAND fury_world_visibility_tests)

if(Python3_Interpreter_FOUND AND TARGET vaultline)
  add_test(NAME whole_world_runtime COMMAND ${Python3_EXECUTABLE}
    ${CMAKE_CURRENT_SOURCE_DIR}/world_runtime_tests.py
    --vaultline $<TARGET_FILE:vaultline> --source-root ${CMAKE_SOURCE_DIR})
  set_tests_properties(whole_world_runtime PROPERTIES TIMEOUT 180 PROCESSORS 2)
endif()

add_executable(fury_world_water_tests world_water_tests.cpp
  ../apps/vaultline/world_water.cpp)
target_link_libraries(fury_world_water_tests PRIVATE Fury::Engine)
fury_stage_runtime(fury_world_water_tests)
add_test(NAME world_water COMMAND fury_world_water_tests)

add_executable(fury_world_audit_tests world_audit_tests.cpp
  ../apps/vaultline/world_audit.cpp)
target_link_libraries(fury_world_audit_tests PRIVATE Fury::Engine)
fury_stage_runtime(fury_world_audit_tests)
add_test(NAME world_audit COMMAND fury_world_audit_tests)

if(UNIX AND FURY_XVFB_RUN)
  add_test(NAME world_water_gl COMMAND ${FURY_XVFB_RUN} -a $<TARGET_FILE:fury_world_water_tests> --gl)
else()
  add_test(NAME world_water_gl COMMAND fury_world_water_tests --gl)
endif()
set_tests_properties(world_water_gl PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 90)
