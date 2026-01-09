video_fft_sources = files('gstvideo_fft.c')

cc = meson.get_compiler('c')
cpp = meson.get_compiler('cpp')
math_dep = cc.find_library('m', required: false)

# Try to find OpenCV for optimized FFT
opencv_dep = dependency('opencv4', required: false)
if not opencv_dep.found()
  opencv_dep = dependency('opencv', required: false)
endif

have_opencv = opencv_dep.found()

# Add C++ source file if OpenCV is available
if have_opencv
  video_fft_sources += files('gstvideo_fft_opencv.cpp')
endif

video_fft_deps = [
  gstreamer_dep,
  gstreamer_video_dep,
  math_dep,
]

if have_opencv
  video_fft_deps += [opencv_dep]
endif

# Get GStreamer plugin installation directory
gstreamer_plugin_dir = gstreamer_dep.get_variable('pluginsdir',
  pkgconfig_define: ['libdir', cheese_prefix / get_option('libdir')],
  default_value: cheese_prefix / get_option('libdir') / 'gstreamer-1.0')

video_fft_c_args = ['-DGST_USE_UNSTABLE_API']
video_fft_cpp_args = []
if have_opencv
  video_fft_c_args += ['-DHAVE_OPENCV']
  video_fft_cpp_args += ['-DHAVE_OPENCV']
endif

video_fft_plugin = shared_library('gstvideo_fft',
  video_fft_sources,
  dependencies: video_fft_deps,
  include_directories: top_inc,
  c_args: video_fft_c_args,
  cpp_args: video_fft_cpp_args,
  install: true,
  install_dir: gstreamer_plugin_dir,
)

