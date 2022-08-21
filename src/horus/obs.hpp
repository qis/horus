#pragma once
#include <obs-module.h>
#include <format>

namespace horus {

#define HORUS_OBS_IMPORT_ALL                        \
  HORUS_OBS_IMPORT(obs_register_source_s)           \
  HORUS_OBS_IMPORT(obs_properties_create)           \
  HORUS_OBS_IMPORT(obs_properties_add_float_slider) \
  HORUS_OBS_IMPORT(obs_data_set_default_double)     \
  HORUS_OBS_IMPORT(obs_data_get_double)             \
  HORUS_OBS_IMPORT(obs_enter_graphics)              \
  HORUS_OBS_IMPORT(obs_leave_graphics)              \
  HORUS_OBS_IMPORT(obs_source_skip_video_filter)    \
  HORUS_OBS_IMPORT(obs_filter_get_parent)           \
  HORUS_OBS_IMPORT(obs_filter_get_target)           \
  HORUS_OBS_IMPORT(obs_source_get_base_width)       \
  HORUS_OBS_IMPORT(obs_source_get_base_height)      \
  HORUS_OBS_IMPORT(obs_source_get_width)            \
  HORUS_OBS_IMPORT(obs_source_get_height)           \
  HORUS_OBS_IMPORT(obs_source_process_filter_begin) \
  HORUS_OBS_IMPORT(obs_source_process_filter_end)   \
  HORUS_OBS_IMPORT(obs_source_inc_showing)          \
  HORUS_OBS_IMPORT(obs_source_dec_showing)          \
  HORUS_OBS_IMPORT(obs_source_video_render)         \
  HORUS_OBS_IMPORT(obs_source_draw)                 \
  HORUS_OBS_IMPORT(gs_effect_create)                \
  HORUS_OBS_IMPORT(gs_effect_create_from_file)      \
  HORUS_OBS_IMPORT(gs_effect_destroy)               \
  HORUS_OBS_IMPORT(gs_stagesurface_create);         \
  HORUS_OBS_IMPORT(gs_stagesurface_destroy);        \
  HORUS_OBS_IMPORT(gs_stage_texture)                \
  HORUS_OBS_IMPORT(gs_stagesurface_map)             \
  HORUS_OBS_IMPORT(gs_stagesurface_unmap)           \
  HORUS_OBS_IMPORT(gs_texrender_create)             \
  HORUS_OBS_IMPORT(gs_texrender_destroy)            \
  HORUS_OBS_IMPORT(gs_blend_state_push)             \
  HORUS_OBS_IMPORT(gs_blend_function)               \
  HORUS_OBS_IMPORT(gs_blend_state_pop)              \
  HORUS_OBS_IMPORT(gs_texrender_begin)              \
  HORUS_OBS_IMPORT(gs_texrender_end)                \
  HORUS_OBS_IMPORT(gs_texrender_reset)              \
  HORUS_OBS_IMPORT(gs_texrender_get_texture)        \
  HORUS_OBS_IMPORT(gs_viewport_push)                \
  HORUS_OBS_IMPORT(gs_viewport_pop)                 \
  HORUS_OBS_IMPORT(gs_set_viewport)                 \
  HORUS_OBS_IMPORT(gs_matrix_push)                  \
  HORUS_OBS_IMPORT(gs_matrix_pop)                   \
  HORUS_OBS_IMPORT(gs_ortho)                        \
  HORUS_OBS_IMPORT(gs_projection_push)              \
  HORUS_OBS_IMPORT(gs_projection_pop)               \
  HORUS_OBS_IMPORT(gs_texture_create)               \
  HORUS_OBS_IMPORT(gs_texture_destroy)              \
  HORUS_OBS_IMPORT(gs_texture_set_image)            \
  HORUS_OBS_IMPORT(gs_effect_get_param_by_name)     \
  HORUS_OBS_IMPORT(gs_effect_set_texture_srgb)


#define HORUS_OBS_IMPORT(name) extern decltype(&::name) name;

HORUS_OBS_IMPORT_ALL

bool initialize() noexcept;

}  // namespace horus