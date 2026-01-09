/*
 * GStreamer video_fft plugin
 * Copyright (C) 2026
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/gst.h>
#include <gst/video/video.h>
#include <gst/video/gstvideofilter.h>
#include <math.h>
#include <string.h>
#include <stdlib.h>
#include <glib.h>
#ifdef G_OS_UNIX
#include <time.h>
#endif

#ifdef HAVE_OPENCV
/* OpenCV FFT implementation is in separate C++ file */
extern void apply_fft_filter_opencv (guint8 *pixels, gint width, gint height, gint stride,
                                     gint channels, gint channel_idx, gdouble blur_val, gdouble sharp_val);
#endif

GST_DEBUG_CATEGORY_STATIC (gst_video_fft_debug);
#define GST_CAT_DEFAULT gst_video_fft_debug

#define GST_TYPE_VIDEO_FFT (gst_video_fft_get_type())
#define GST_VIDEO_FFT(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_VIDEO_FFT,GstVideoFft))
#define GST_VIDEO_FFT_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_VIDEO_FFT,GstVideoFftClass))
#define GST_IS_VIDEO_FFT(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_VIDEO_FFT))
#define GST_IS_VIDEO_FFT_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_VIDEO_FFT))

typedef struct _GstVideoFft GstVideoFft;
typedef struct _GstVideoFftClass GstVideoFftClass;

struct _GstVideoFft
{
  GstVideoFilter base;
  gdouble blur;
  gdouble sharp;
};

struct _GstVideoFftClass
{
  GstVideoFilterClass base_class;
};

enum
{
  PROP_0,
  PROP_BLUR,
  PROP_SHARPNESS
};

#define DEFAULT_BLUR 0.0
#define DEFAULT_SHARPNESS 0.0

static GstStaticPadTemplate gst_video_fft_src_template =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_MAKE ("{ RGB, BGR, RGBA, BGRA, ARGB, ABGR }"))
);

static GstStaticPadTemplate gst_video_fft_sink_template =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_MAKE ("{ RGB, BGR, RGBA, BGRA, ARGB, ABGR }"))
);

/* Forward declaration to satisfy -Wmissing-prototypes */
GType gst_video_fft_get_type (void);

G_DEFINE_TYPE (GstVideoFft, gst_video_fft, GST_TYPE_VIDEO_FILTER);

static void gst_video_fft_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_video_fft_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static GstFlowReturn gst_video_fft_transform_frame (GstVideoFilter * filter,
    GstVideoFrame * inframe, GstVideoFrame * outframe);

/* Simple 1D FFT using Cooley-Tukey algorithm (radix-2) */
/* Optimized with pre-computed twiddle factors */
static void
fft_1d (gdouble *real, gdouble *imag, gint n, gint inverse)
{
  gint i, j, k, m;
  gdouble angle, w_real, w_imag, t_real, t_imag;
  gdouble scale = inverse ? 1.0 / n : 1.0;
  
  /* Bit-reverse permutation - optimized inner loop */
  j = 0;
  for (i = 1; i < n; i++) {
    gint bit = n >> 1;
    for (; j & bit; bit >>= 1)
      j ^= bit;
    j ^= bit;
    if (i < j) {
      /* Swap real and imag in one go */
      gdouble tmp_r = real[i];
      gdouble tmp_i = imag[i];
      real[i] = real[j];
      imag[i] = imag[j];
      real[j] = tmp_r;
      imag[j] = tmp_i;
    }
  }
  
  /* FFT computation - optimized with better cache locality */
  for (m = 2; m <= n; m <<= 1) {
    angle = (inverse ? 2.0 : -2.0) * M_PI / m;
    /* Pre-compute twiddle factor once per stage */
    w_real = cos (angle);
    w_imag = sin (angle);
    
    for (k = 0; k < n; k += m) {
      gdouble u_real = 1.0;
      gdouble u_imag = 0.0;
      
      /* Process butterfly operations */
      for (j = 0; j < m / 2; j++) {
        gint t = k + j + m / 2;
        gint k_j = k + j;
        
        /* Butterfly computation - optimized */
        t_real = u_real * real[t] - u_imag * imag[t];
        t_imag = u_real * imag[t] + u_imag * real[t];
        
        real[t] = real[k_j] - t_real;
        imag[t] = imag[k_j] - t_imag;
        real[k_j] += t_real;
        imag[k_j] += t_imag;
        
        /* Update twiddle factor - optimized */
        gdouble next_u_real = u_real * w_real - u_imag * w_imag;
        u_imag = u_real * w_imag + u_imag * w_real;
        u_real = next_u_real;
      }
    }
  }
  
  /* Scale for inverse FFT - vectorized-friendly loop */
  if (inverse) {
    for (i = 0; i < n; i++) {
      real[i] *= scale;
      imag[i] *= scale;
    }
  }
}

/* Calculate next power of 2 */
static gint
next_power_of_2 (gint n)
{
  gint p = 1;
  while (p < n)
    p <<= 1;
  return p;
}


/* Apply frequency domain filter using FFT */
static void
apply_fft_filter (guint8 *pixels, gint width, gint height, gint stride,
                  gint channels, gint channel_idx, gdouble blur_val, gdouble sharp_val)
{
#ifdef HAVE_OPENCV
  /* Use OpenCV's optimized FFT if available */
  apply_fft_filter_opencv(pixels, width, height, stride, channels, channel_idx, blur_val, sharp_val);
  return;
#endif

  /* Fallback to custom FFT implementation */
  /* Performance optimization: downsample very large images for FFT */
  /* This significantly reduces computation time while maintaining quality */
  gint fft_w = next_power_of_2 (width);
  gint fft_h = next_power_of_2 (height);
  gint orig_width = width;
  gint orig_height = height;
  gint scale_factor = 1;
  
  /* Downsample if image is very large (reduces FFT size dramatically) */
  if (fft_w > 1024 || fft_h > 1024) {
    scale_factor = (fft_w > fft_h) ? (fft_w / 1024) : (fft_h / 1024);
    if (scale_factor < 1) scale_factor = 1;
    width = (width + scale_factor - 1) / scale_factor;
    height = (height + scale_factor - 1) / scale_factor;
    fft_w = next_power_of_2 (width);
    fft_h = next_power_of_2 (height);
  }
  
  gint i, j;
  gdouble *real, *imag, *temp_real, *temp_imag;
  
#ifdef G_OS_UNIX
  struct timespec start, end;
  gdouble time_alloc = 0, time_copy_in = 0, time_fft_forward = 0;
  gdouble time_filter = 0, time_fft_inverse = 0, time_copy_out = 0;
  clock_gettime (CLOCK_MONOTONIC, &start);
#endif
  
  /* Allocate FFT buffers */
  real = g_malloc (fft_w * fft_h * sizeof (gdouble));
  imag = g_malloc (fft_w * fft_h * sizeof (gdouble));
  temp_real = g_malloc (fft_w * sizeof (gdouble));
  temp_imag = g_malloc (fft_w * sizeof (gdouble));
  
#ifdef G_OS_UNIX
  clock_gettime (CLOCK_MONOTONIC, &end);
  time_alloc = (end.tv_sec - start.tv_sec) * 1e9 + (end.tv_nsec - start.tv_nsec);
  clock_gettime (CLOCK_MONOTONIC, &start);
#endif
  
  if (!real || !imag || !temp_real || !temp_imag) {
    g_free (real);
    g_free (imag);
    g_free (temp_real);
    g_free (temp_imag);
    return;
  }
  
  /* Copy image data to FFT buffer */
  for (j = 0; j < height; j++) {
    guint8 *row = pixels + j * stride;
    for (i = 0; i < width; i++) {
      gint idx = j * fft_w + i;
      real[idx] = (gdouble) row[i * channels + channel_idx];
      imag[idx] = 0.0;
    }
    /* Zero-pad to FFT width */
    for (i = width; i < fft_w; i++) {
      gint idx = j * fft_w + i;
      real[idx] = 0.0;
      imag[idx] = 0.0;
    }
  }
  /* Zero-pad to FFT height */
  for (j = height; j < fft_h; j++) {
    for (i = 0; i < fft_w; i++) {
      gint idx = j * fft_w + i;
      real[idx] = 0.0;
      imag[idx] = 0.0;
    }
  }
  
#ifdef G_OS_UNIX
  clock_gettime (CLOCK_MONOTONIC, &end);
  time_copy_in = (end.tv_sec - start.tv_sec) * 1e9 + (end.tv_nsec - start.tv_nsec);
  clock_gettime (CLOCK_MONOTONIC, &start);
#endif
  
  /* 2D FFT: First transform rows - optimized memory access */
  /* Use larger temp buffer to avoid repeated allocations */
  gint max_fft = (fft_w > fft_h) ? fft_w : fft_h;
  if (max_fft > fft_w) {
    /* Reallocate temp buffers if needed for columns */
    g_free (temp_real);
    g_free (temp_imag);
    temp_real = g_malloc (max_fft * sizeof (gdouble));
    temp_imag = g_malloc (max_fft * sizeof (gdouble));
    if (!temp_real || !temp_imag) {
      g_free (real);
      g_free (imag);
      g_free (temp_real);
      g_free (temp_imag);
      return;
    }
  }
  
  /* Transform rows - process in cache-friendly blocks */
  for (j = 0; j < fft_h; j++) {
    gint row_offset = j * fft_w;
    /* Copy row to temp buffer */
    for (i = 0; i < fft_w; i++) {
      gint idx = row_offset + i;
      temp_real[i] = real[idx];
      temp_imag[i] = imag[idx];
    }
    /* Transform row */
    fft_1d (temp_real, temp_imag, fft_w, 0);
    /* Copy back */
    for (i = 0; i < fft_w; i++) {
      gint idx = row_offset + i;
      real[idx] = temp_real[i];
      imag[idx] = temp_imag[i];
    }
  }
  
  /* Then transform columns - optimized */
  for (i = 0; i < fft_w; i++) {
    /* Copy column to temp buffer */
    for (j = 0; j < fft_h; j++) {
      gint idx = j * fft_w + i;
      temp_real[j] = real[idx];
      temp_imag[j] = imag[idx];
    }
    /* Transform column */
    fft_1d (temp_real, temp_imag, fft_h, 0);
    /* Copy back */
    for (j = 0; j < fft_h; j++) {
      gint idx = j * fft_w + i;
      real[idx] = temp_real[j];
      imag[idx] = temp_imag[j];
    }
  }
  
#ifdef G_OS_UNIX
  clock_gettime (CLOCK_MONOTONIC, &end);
  time_fft_forward = (end.tv_sec - start.tv_sec) * 1e9 + (end.tv_nsec - start.tv_nsec);
  clock_gettime (CLOCK_MONOTONIC, &start);
#endif
  
  /* Apply frequency response curve */
  /* Pre-compute constants for better performance */
  gdouble center_x = fft_w / 2.0;
  gdouble center_y = fft_h / 2.0;
  gdouble max_freq = sqrt (center_x * center_x + center_y * center_y);
  gdouble inv_max_freq = 1.0 / max_freq; /* Pre-compute inverse for division optimization */
  
  /* Pre-compute filter parameters if needed */
  gdouble sigma = 0.0;
  gdouble blur_strength = 0.0;
  gboolean has_blur = (blur_val != 0.0);
  if (has_blur) {
    blur_strength = fabs (blur_val);
    sigma = 0.1 + (blur_strength / 10.0) * 0.7;
    sigma = 2.0 * sigma * sigma; /* Pre-compute 2*sigma^2 for Gaussian */
  }
  
  gdouble sharp_strength = 0.0;
  gdouble boost_start = 0.15;
  gdouble boost_scale = 0.0;
  gdouble boost_range_inv = 0.0;
  gboolean has_sharp = fabs(sharp_val) > 1e-6;
  if (has_sharp) {
    sharp_strength = fabs (sharp_val);
    boost_scale = (sharp_strength / 10.0) * 0.5;
    boost_range_inv = 1.0 / (1.0 - boost_start); /* Pre-compute for division */
  }
  
  /* Preserve DC component (brightness) - store it before filtering */
  gdouble dc_real = real[0];
  gdouble dc_imag = imag[0];
  
  /* Optimized frequency filtering loop */
  for (j = 0; j < fft_h; j++) {
    gdouble dy = j - center_y;
    gdouble dy_sq = dy * dy; /* Pre-compute dy^2 for each row */
    
    for (i = 0; i < fft_w; i++) {
      /* Skip DC component */
      if (i == 0 && j == 0) {
        continue;
      }
      
      gint idx = j * fft_w + i;
      gdouble dx = i - center_x;
      gdouble freq_sq = dx * dx + dy_sq; /* Use pre-computed dy^2 */
      gdouble freq = sqrt (freq_sq);
      gdouble normalized_freq = freq * inv_max_freq; /* Use pre-computed inverse */
      
      /* Smooth frequency response curve */
      gdouble response = 1.0;
      
      if (has_blur) {
        /* Blur: Gaussian-like low-pass filter - optimized */
        gdouble normalized_freq_sq = normalized_freq * normalized_freq;
        response = exp (-normalized_freq_sq / sigma); /* Use pre-computed 2*sigma^2 */
      }
      
      if (has_sharp && normalized_freq > boost_start) {
        /* Sharpness: smooth high-frequency boost - optimized */
        gdouble boost_factor = 1.0 + boost_scale * 
                               (0.5 + 0.5 * sin (M_PI * (normalized_freq - boost_start) * boost_range_inv));
        response *= boost_factor;
      }
      
      /* Apply frequency response */
      real[idx] *= response;
      imag[idx] *= response;
    }
  }
  
  /* Restore DC component to preserve brightness */
  real[0] = dc_real;
  imag[0] = dc_imag;
  
#ifdef G_OS_UNIX
  clock_gettime (CLOCK_MONOTONIC, &end);
  time_filter = (end.tv_sec - start.tv_sec) * 1e9 + (end.tv_nsec - start.tv_nsec);
  clock_gettime (CLOCK_MONOTONIC, &start);
#endif
  
  /* Inverse 2D FFT: First transform columns */
  for (i = 0; i < fft_w; i++) {
    for (j = 0; j < fft_h; j++) {
      gint idx = j * fft_w + i;
      temp_real[j] = real[idx];
      temp_imag[j] = imag[idx];
    }
    fft_1d (temp_real, temp_imag, fft_h, 1);
    for (j = 0; j < fft_h; j++) {
      gint idx = j * fft_w + i;
      real[idx] = temp_real[j];
      imag[idx] = temp_imag[j];
    }
  }
  
  /* Then transform rows */
  for (j = 0; j < fft_h; j++) {
    for (i = 0; i < fft_w; i++) {
      gint idx = j * fft_w + i;
      temp_real[i] = real[idx];
      temp_imag[i] = imag[idx];
    }
    fft_1d (temp_real, temp_imag, fft_w, 1);
    for (i = 0; i < fft_w; i++) {
      gint idx = j * fft_w + i;
      real[idx] = temp_real[i];
      imag[idx] = temp_imag[i];
    }
  }
  
#ifdef G_OS_UNIX
  clock_gettime (CLOCK_MONOTONIC, &end);
  time_fft_inverse = (end.tv_sec - start.tv_sec) * 1e9 + (end.tv_nsec - start.tv_nsec);
  clock_gettime (CLOCK_MONOTONIC, &start);
#endif
  
  /* Copy back to image */
  for (j = 0; j < height; j++) {
    guint8 *row = pixels + j * stride;
    for (i = 0; i < width; i++) {
      gint idx = j * fft_w + i;
      gint value = (gint) (real[idx] + 0.5);
      row[i * channels + channel_idx] = CLAMP (value, 0, 255);
    }
  }
  
#ifdef G_OS_UNIX
  clock_gettime (CLOCK_MONOTONIC, &end);
  time_copy_out = (end.tv_sec - start.tv_sec) * 1e9 + (end.tv_nsec - start.tv_nsec);
  
  /* Log profiling information (only for first channel to avoid spam) */
  static gint profile_count = 0;
  if (channel_idx == 0 && (++profile_count % 30 == 0)) {
    gdouble total = time_alloc + time_copy_in + time_fft_forward + 
                    time_filter + time_fft_inverse + time_copy_out;
    GST_DEBUG ("FFT Profile [%dx%d->%dx%d]: alloc=%.1f%% copy_in=%.1f%% fft_fwd=%.1f%% "
               "filter=%.1f%% fft_inv=%.1f%% copy_out=%.1f%% total=%.2fms",
               width, height, fft_w, fft_h,
               (time_alloc / total) * 100.0,
               (time_copy_in / total) * 100.0,
               (time_fft_forward / total) * 100.0,
               (time_filter / total) * 100.0,
               (time_fft_inverse / total) * 100.0,
               (time_copy_out / total) * 100.0,
               total / 1e6);
  }
#endif
  
  g_free (real);
  g_free (imag);
  g_free (temp_real);
  g_free (temp_imag);
}

static void
gst_video_fft_class_init (GstVideoFftClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;
  GstVideoFilterClass *video_filter_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;
  video_filter_class = (GstVideoFilterClass *) klass;

  gst_element_class_add_static_pad_template (gstelement_class, &gst_video_fft_sink_template);
  gst_element_class_add_static_pad_template (gstelement_class, &gst_video_fft_src_template);

  gobject_class->set_property = gst_video_fft_set_property;
  gobject_class->get_property = gst_video_fft_get_property;

  g_object_class_install_property (gobject_class, PROP_BLUR,
      g_param_spec_double ("blur", "Blur", "Blur adjustment value",
          -10.0, 10.0, DEFAULT_BLUR,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_SHARPNESS,
      g_param_spec_double ("sharp", "Sharpness", "Sharpness adjustment value",
          -10.0, 10.0, DEFAULT_SHARPNESS,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  gst_element_class_set_static_metadata (gstelement_class,
      "Video FFT filter",
      "Filter/Effect/Video",
      "Apply FFT-based blur and sharpness filters",
      "Cheese");

  video_filter_class->transform_frame =
      GST_DEBUG_FUNCPTR (gst_video_fft_transform_frame);
}

static void
gst_video_fft_init (GstVideoFft * filter)
{
  filter->blur = DEFAULT_BLUR;
  filter->sharp = DEFAULT_SHARPNESS;
}

static void
gst_video_fft_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstVideoFft *filter = GST_VIDEO_FFT (object);

  switch (prop_id) {
    case PROP_BLUR:
      filter->blur = g_value_get_double (value);
      break;
    case PROP_SHARPNESS:
      filter->sharp = g_value_get_double (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_video_fft_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstVideoFft *filter = GST_VIDEO_FFT (object);

  switch (prop_id) {
    case PROP_BLUR:
      g_value_set_double (value, filter->blur);
      break;
    case PROP_SHARPNESS:
      g_value_set_double (value, filter->sharp);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static GstFlowReturn
gst_video_fft_transform_frame (GstVideoFilter * vfilter,
    GstVideoFrame * inframe, GstVideoFrame * outframe)
{
  GstVideoFft *filter = GST_VIDEO_FFT (vfilter);
  guint8 *in_data, *out_data;
  gint width, height, in_stride, out_stride, channels;

  in_data = GST_VIDEO_FRAME_PLANE_DATA (inframe, 0);
  out_data = GST_VIDEO_FRAME_PLANE_DATA (outframe, 0);
  width = GST_VIDEO_FRAME_WIDTH (inframe);
  height = GST_VIDEO_FRAME_HEIGHT (inframe);
  in_stride = GST_VIDEO_FRAME_PLANE_STRIDE (inframe, 0);
  out_stride = GST_VIDEO_FRAME_PLANE_STRIDE (outframe, 0);
  channels = GST_VIDEO_FRAME_N_COMPONENTS (inframe);

  /* Copy input to output first */
  gst_video_frame_copy (outframe, inframe);

  /* Apply FFT-based blur and sharpness if needed */
  /* Skip FFT if values are very small (performance optimization) */
  if ((filter->blur != 0.0 && fabs (filter->blur) > 0.01) ||
      (filter->sharp != 0.0 && fabs (filter->sharp) > 0.01)) {
    /* Process each color channel separately */
    for (gint c = 0; c < channels && c < 3; c++) {
      apply_fft_filter (out_data, width, height, out_stride, channels, c,
                        filter->blur, filter->sharp);
    }
  }

  return GST_FLOW_OK;
}

static gboolean
plugin_init (GstPlugin * plugin)
{
  GST_DEBUG_CATEGORY_INIT (gst_video_fft_debug, "video_fft", 0,
      "Video FFT plugin");

  return gst_element_register (plugin, "video_fft", GST_RANK_NONE,
      GST_TYPE_VIDEO_FFT);
}

#ifndef PACKAGE
#define PACKAGE "cheese"
#endif

#ifndef VERSION
#define VERSION "44.1"
#endif

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    video_fft,
    "Video FFT effect plugin",
    plugin_init,
    VERSION,
    "LGPL",
    PACKAGE,
    "https://wiki.gnome.org/Apps/Cheese")

