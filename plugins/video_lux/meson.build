video_lux_sources = files('gstvideo_lux.c')

video_lux_deps = [
  gstreamer_dep,
  gstreamer_video_dep,
]

# Get GStreamer plugin installation directory
gstreamer_plugin_dir = gstreamer_dep.get_variable('pluginsdir',
  pkgconfig_define: ['libdir', cheese_prefix / get_option('libdir')],
  default_value: cheese_prefix / get_option('libdir') / 'gstreamer-1.0')

video_lux_plugin = shared_library('gstvideo_lux',
  video_lux_sources,
  dependencies: video_lux_deps,
  include_directories: top_inc,
  c_args: ['-DGST_USE_UNSTABLE_API'],
  install: true,
  install_dir: gstreamer_plugin_dir,
)

