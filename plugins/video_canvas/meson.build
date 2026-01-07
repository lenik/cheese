canvas_sources = files('gstcanvas.c')

cc = meson.get_compiler('c')
math_dep = cc.find_library('m', required: false)

canvas_deps = [
  gstreamer_dep,
  gstreamer_video_dep,
  math_dep,
]

# Get GStreamer plugin installation directory
gstreamer_plugin_dir = gstreamer_dep.get_variable('pluginsdir',
  pkgconfig_define: ['libdir', cheese_prefix / get_option('libdir')],
  default_value: cheese_prefix / get_option('libdir') / 'gstreamer-1.0')

canvas_plugin = shared_library('gstcanvas',
  canvas_sources,
  dependencies: canvas_deps,
  include_directories: top_inc,
  c_args: ['-DGST_USE_UNSTABLE_API'],
  install: true,
  install_dir: gstreamer_plugin_dir,
)

