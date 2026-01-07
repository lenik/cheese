/*
 * GStreamer video_lux plugin
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

GST_DEBUG_CATEGORY_STATIC (gst_video_lux_debug);
#define GST_CAT_DEFAULT gst_video_lux_debug

#define GST_TYPE_VIDEO_LUX (gst_video_lux_get_type())
#define GST_VIDEO_LUX(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_VIDEO_LUX,GstVideoLux))
#define GST_VIDEO_LUX_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_VIDEO_LUX,GstVideoLuxClass))
#define GST_IS_VIDEO_LUX(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_VIDEO_LUX))
#define GST_IS_VIDEO_LUX_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_VIDEO_LUX))

typedef struct _GstVideoLux GstVideoLux;
typedef struct _GstVideoLuxClass GstVideoLuxClass;

struct _GstVideoLux
{
  GstVideoFilter base;
  gdouble lux;
  gdouble black;
  gdouble shadow;
  gdouble midtone;
  gdouble highlight;
  gdouble white;
  gdouble orange;
  gdouble red;
  gdouble yellow;
  gdouble green;
  gdouble cyan;
  gdouble blue;
  gdouble magenta;
  gdouble color_breadth;
  gdouble blur;
  gdouble sharpness;
  
  /* FFT buffers for frequency domain processing */
  gint fft_width;
  gint fft_height;
  gdouble *fft_real;
  gdouble *fft_imag;
  gdouble *fft_temp;
};

struct _GstVideoLuxClass
{
  GstVideoFilterClass base_class;
};

enum
{
  PROP_0,
  PROP_LUX,
  PROP_BLACK,
  PROP_SHADOW,
  PROP_MIDTONE,
  PROP_HIGHLIGHT,
  PROP_WHITE,
  PROP_ORANGE,
  PROP_RED,
  PROP_YELLOW,
  PROP_GREEN,
  PROP_CYAN,
  PROP_BLUE,
  PROP_MAGENTA,
  PROP_COLOR_BREADTH,
  PROP_BLUR,
  PROP_SHARPNESS
};

#define DEFAULT_LUX 0.0
#define DEFAULT_BLACK 0.0
#define DEFAULT_SHADOW 0.0
#define DEFAULT_MIDTONE 0.0
#define DEFAULT_HIGHLIGHT 0.0
#define DEFAULT_WHITE 0.0
#define DEFAULT_ORANGE 0.0
#define DEFAULT_RED 0.0
#define DEFAULT_YELLOW 0.0
#define DEFAULT_GREEN 0.0
#define DEFAULT_CYAN 0.0
#define DEFAULT_BLUE 0.0
#define DEFAULT_MAGENTA 0.0
#define DEFAULT_COLOR_BREADTH 0.3
#define DEFAULT_BLUR 0.0
#define DEFAULT_SHARPNESS 0.0

static GstStaticPadTemplate gst_video_lux_src_template =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_MAKE ("{ RGB, BGR, RGBA, BGRA, ARGB, ABGR }"))
);

static GstStaticPadTemplate gst_video_lux_sink_template =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_MAKE ("{ RGB, BGR, RGBA, BGRA, ARGB, ABGR }"))
);

/* Forward declaration to satisfy -Wmissing-prototypes */
GType gst_video_lux_get_type (void);

G_DEFINE_TYPE (GstVideoLux, gst_video_lux, GST_TYPE_VIDEO_FILTER);

static void gst_video_lux_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_video_lux_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static GstFlowReturn gst_video_lux_transform_frame (GstVideoFilter * filter,
    GstVideoFrame * inframe, GstVideoFrame * outframe);

/* Calculate luminance from RGB using integer arithmetic */
static inline gint
calculate_luminance_int (guint8 r, guint8 g, guint8 b)
{
  /* 0.299 * 256 = 76.544, 0.587 * 256 = 150.272, 0.114 * 256 = 29.184 */
  return (76 * r + 150 * g + 29 * b) >> 8;
}

/* Apply tone curve adjustment based on luminance with smooth transitions (returns adjustment * 256 for integer math) */
static inline gint
apply_tone_curve_int (gint luminance, gint black_int, gint shadow_int, gint midtone_int, gint highlight_int, gint white_int)
{
  /* Use smooth blending weights for each region to avoid discontinuities */
  /* Each region has overlapping coverage to ensure smooth transitions */
  gint black_weight = 0, shadow_weight = 0, midtone_weight = 0, highlight_weight = 0, white_weight = 0;
  gint adjustment = 0;
  
  /* Black: peak at 0, smooth falloff extending to ~0.2 (51) */
  if (luminance < 51) {
    /* Linear falloff: 256 at 0, 0 at 51 */
    black_weight = (51 - luminance) * 256 / 51;
  }
  
  /* Shadow: peak around 0.2 (51), smooth falloff from 0 to 0.4 (0 to 102) */
  if (luminance < 102) {
    if (luminance < 51) {
      /* Rising edge: 0 to 51 */
      shadow_weight = luminance * 256 / 51;
    } else {
      /* Falling edge: 51 to 102 */
      shadow_weight = (102 - luminance) * 256 / 51;
    }
  }
  
  /* Midtone: peak at 0.5 (128), smooth falloff from 0.2 to 0.8 (51 to 204) */
  if (luminance >= 51 && luminance < 204) {
    if (luminance < 128) {
      /* Rising edge: 51 to 128 */
      midtone_weight = (luminance - 51) * 256 / 77;
    } else {
      /* Falling edge: 128 to 204 */
      midtone_weight = (204 - luminance) * 256 / 76;
    }
  }
  
  /* Highlight: peak around 0.75 (191), smooth falloff from 0.6 to 0.9 (153 to 230) */
  if (luminance >= 153 && luminance < 230) {
    if (luminance < 191) {
      /* Rising edge: 153 to 191 */
      highlight_weight = (luminance - 153) * 256 / 38;
    } else {
      /* Falling edge: 191 to 230 */
      highlight_weight = (230 - luminance) * 256 / 39;
    }
  }
  
  /* White: peak at 255, smooth rise from 0.8 (204) */
  if (luminance >= 204) {
    /* Linear rise: 0 at 204, 256 at 255 */
    white_weight = (luminance - 204) * 256 / 51;
    if (white_weight > 256) white_weight = 256;
  }
  
  /* Apply weighted adjustments directly - weights are already normalized 0-256 */
  /* Each adjustment value is already multiplied by 256, so we multiply by weight and divide by 256*256 */
  adjustment = (black_int * black_weight + 
                shadow_int * shadow_weight + 
                midtone_int * midtone_weight - 
                highlight_int * highlight_weight - 
                white_int * white_weight);
  
  /* Divide by 256 to get final adjustment (since black_int etc are already * 256) */
  adjustment = adjustment >> 8;
  
  return adjustment;
}

/* Calculate selective color component with breadth control */
static inline gint
calculate_selective_color_int (guint8 r, guint8 g, guint8 b, 
                                gint r_coeff, gint g_coeff, gint b_coeff,
                                gdouble breadth)
{
  /* Calculate color component using weighted RGB values */
  /* Coefficients are in 0-256 range, can be negative */
  gint color_raw = (r_coeff * r + g_coeff * g + b_coeff * b);
  
  /* Normalize: shift to positive range, then scale to 0-255 */
  /* Assuming max positive value is around 256*255 = 65280, max negative is around -256*255 = -65280 */
  /* Shift by 65280 to make it positive, then divide by 512 to get 0-255 range */
  gint color = CLAMP ((color_raw + 65280) / 512, 0, 255);
  
  /* Apply breadth: higher breadth = broader selection, lower = narrower */
  /* Use a threshold based on breadth: 0.0 = very narrow (high threshold), 1.0 = very broad (low threshold) */
  /* Narrow selection by requiring higher color component values */
  gint min_threshold = 80;   /* Minimum threshold for very broad selection */
  gint max_threshold = 200;  /* Maximum threshold for very narrow selection */
  gint threshold = (gint)(min_threshold + (1.0 - breadth) * (max_threshold - min_threshold));
  
  if (color < threshold) {
    /* Scale down colors below threshold - narrower breadth means more aggressive scaling */
    gint diff = threshold - color;
    gint max_diff = threshold - min_threshold;
    if (max_diff > 0) {
      /* Linear falloff with breadth-dependent scaling */
      /* For narrow breadth (low value), scale more aggressively */
      gint breadth_int = (gint)(breadth * 256); /* 0-256 range */
      gint scale_numerator = (256 - breadth_int) * diff + breadth_int * max_diff;
      color = (color * scale_numerator) / (max_diff * 256);
    } else {
      color = 0;
    }
  } else {
    /* Full strength for colors above threshold */
    color = 255;
  }
  
  return CLAMP (color, 0, 255);
}

/* Calculate orange component (warm tones) using integer arithmetic */
static inline gint
calculate_orange_component_int (guint8 r, guint8 g, guint8 b, gdouble breadth)
{
  /* Orange is high in red, medium in green, low in blue */
  /* 0.6*256=153.6, 0.3*256=76.8, 0.1*256=25.6 */
  return calculate_selective_color_int (r, g, b, 153, 77, -26, breadth);
}

/* Calculate red component */
static inline gint
calculate_red_component_int (guint8 r, guint8 g, guint8 b, gdouble breadth)
{
  /* Red is high in red, low in green and blue */
  return calculate_selective_color_int (r, g, b, 256, -128, -128, breadth);
}

/* Calculate yellow component */
static inline gint
calculate_yellow_component_int (guint8 r, guint8 g, guint8 b, gdouble breadth)
{
  /* Yellow is high in red and green, low in blue */
  return calculate_selective_color_int (r, g, b, 128, 128, -256, breadth);
}

/* Calculate green component */
static inline gint
calculate_green_component_int (guint8 r, guint8 g, guint8 b, gdouble breadth)
{
  /* Green is high in green, low in red and blue */
  return calculate_selective_color_int (r, g, b, -128, 256, -128, breadth);
}

/* Calculate cyan component */
static inline gint
calculate_cyan_component_int (guint8 r, guint8 g, guint8 b, gdouble breadth)
{
  /* Cyan is high in green and blue, low in red */
  return calculate_selective_color_int (r, g, b, -256, 128, 128, breadth);
}

/* Calculate blue component */
static inline gint
calculate_blue_component_int (guint8 r, guint8 g, guint8 b, gdouble breadth)
{
  /* Blue is high in blue, low in red and green */
  return calculate_selective_color_int (r, g, b, -128, -128, 256, breadth);
}

/* Calculate magenta component */
static inline gint
calculate_magenta_component_int (guint8 r, guint8 g, guint8 b, gdouble breadth)
{
  /* Magenta is high in red and blue, low in green */
  return calculate_selective_color_int (r, g, b, 128, -256, 128, breadth);
}

/* Simple 1D FFT using Cooley-Tukey algorithm (radix-2) */
static void
fft_1d (gdouble *real, gdouble *imag, gint n, gint inverse)
{
  gint i, j, k, m;
  gdouble angle, w_real, w_imag, t_real, t_imag;
  gdouble scale = inverse ? 1.0 / n : 1.0;
  
  /* Bit-reverse permutation */
  j = 0;
  for (i = 1; i < n; i++) {
    gint bit = n >> 1;
    for (; j & bit; bit >>= 1)
      j ^= bit;
    j ^= bit;
    if (i < j) {
      gdouble tmp = real[i];
      real[i] = real[j];
      real[j] = tmp;
      tmp = imag[i];
      imag[i] = imag[j];
      imag[j] = tmp;
    }
  }
  
  /* FFT computation */
  for (m = 2; m <= n; m <<= 1) {
    angle = (inverse ? 2.0 : -2.0) * M_PI / m;
    w_real = cos (angle);
    w_imag = sin (angle);
    
    for (k = 0; k < n; k += m) {
      gdouble u_real = 1.0;
      gdouble u_imag = 0.0;
      
      for (j = 0; j < m / 2; j++) {
        gint t = k + j + m / 2;
        t_real = u_real * real[t] - u_imag * imag[t];
        t_imag = u_real * imag[t] + u_imag * real[t];
        
        real[t] = real[k + j] - t_real;
        imag[t] = imag[k + j] - t_imag;
        real[k + j] += t_real;
        imag[k + j] += t_imag;
        
        gdouble next_u_real = u_real * w_real - u_imag * w_imag;
        u_imag = u_real * w_imag + u_imag * w_real;
        u_real = next_u_real;
      }
    }
  }
  
  /* Scale for inverse FFT */
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
                  gint channels, gint channel_idx, gdouble blur_val, gdouble sharpness_val)
{
  gint fft_w = next_power_of_2 (width);
  gint fft_h = next_power_of_2 (height);
  gint i, j;
  gdouble *real, *imag, *temp_real, *temp_imag;
  
  /* Allocate FFT buffers */
  real = g_malloc (fft_w * fft_h * sizeof (gdouble));
  imag = g_malloc (fft_w * fft_h * sizeof (gdouble));
  temp_real = g_malloc (fft_w * sizeof (gdouble));
  temp_imag = g_malloc (fft_w * sizeof (gdouble));
  
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
  
  /* 2D FFT: First transform rows */
  for (j = 0; j < fft_h; j++) {
    for (i = 0; i < fft_w; i++) {
      gint idx = j * fft_w + i;
      temp_real[i] = real[idx];
      temp_imag[i] = imag[idx];
    }
    fft_1d (temp_real, temp_imag, fft_w, 0);
    for (i = 0; i < fft_w; i++) {
      gint idx = j * fft_w + i;
      real[idx] = temp_real[i];
      imag[idx] = temp_imag[i];
    }
  }
  
  /* Then transform columns */
  for (i = 0; i < fft_w; i++) {
    for (j = 0; j < fft_h; j++) {
      gint idx = j * fft_w + i;
      temp_real[j] = real[idx];
      temp_imag[j] = imag[idx];
    }
    fft_1d (temp_real, temp_imag, fft_h, 0);
    for (j = 0; j < fft_h; j++) {
      gint idx = j * fft_w + i;
      real[idx] = temp_real[j];
      imag[idx] = temp_imag[j];
    }
  }
  
  /* Apply frequency response curve */
  gdouble center_x = fft_w / 2.0;
  gdouble center_y = fft_h / 2.0;
  gdouble max_freq = sqrt (center_x * center_x + center_y * center_y);
  
  for (j = 0; j < fft_h; j++) {
    for (i = 0; i < fft_w; i++) {
      gint idx = j * fft_w + i;
      gdouble dx = i - center_x;
      gdouble dy = j - center_y;
      gdouble freq = sqrt (dx * dx + dy * dy);
      gdouble normalized_freq = freq / max_freq; /* 0.0 to 1.0 */
      
      /* Smooth frequency response curve */
      /* For blur: reduce high frequencies (smooth rolloff) */
      /* For sharpness: enhance high frequencies (smooth boost) */
      gdouble response = 1.0;
      
      if (blur_val != 0.0) {
        /* Blur: smooth low-pass filter */
        /* Use smooth curve: 1.0 at DC, decreasing for higher frequencies */
        gdouble blur_strength = fabs (blur_val) * 0.1;
        gdouble cutoff = 0.3 + blur_strength * 0.5; /* Adjustable cutoff */
        /* Smooth rolloff using cosine curve */
        if (normalized_freq > cutoff) {
          gdouble rolloff = (normalized_freq - cutoff) / (1.0 - cutoff);
          response *= (1.0 - blur_strength) * (0.5 + 0.5 * cos (M_PI * rolloff));
        } else {
          /* Preserve low frequencies */
          gdouble preserve = 1.0 - blur_strength * (normalized_freq / cutoff);
          response *= preserve;
        }
      }
      
      if (sharpness_val != 0.0) {
        /* Sharpness: smooth high-frequency boost */
        gdouble sharp_strength = fabs (sharpness_val) * 0.15;
        gdouble boost_start = 0.1; /* Start boosting from this frequency */
        if (normalized_freq > boost_start) {
          gdouble boost_factor = 1.0 + sharp_strength * 
                                 (0.5 + 0.5 * sin (M_PI * (normalized_freq - boost_start) / (1.0 - boost_start)));
          response *= boost_factor;
        }
      }
      
      /* Preserve DC component (brightness) */
      if (i == 0 && j == 0) {
        response = 1.0;
      }
      
      /* Apply frequency response */
      real[idx] *= response;
      imag[idx] *= response;
    }
  }
  
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
  
  /* Copy back to image */
  for (j = 0; j < height; j++) {
    guint8 *row = pixels + j * stride;
    for (i = 0; i < width; i++) {
      gint idx = j * fft_w + i;
      gint value = (gint) (real[idx] + 0.5);
      row[i * channels + channel_idx] = CLAMP (value, 0, 255);
    }
  }
  
  g_free (real);
  g_free (imag);
  g_free (temp_real);
  g_free (temp_imag);
}

static void
gst_video_lux_class_init (GstVideoLuxClass * klass)
{
  GObjectClass *gobject_class = (GObjectClass *) klass;
  GstElementClass *gstelement_class = (GstElementClass *) klass;
  GstVideoFilterClass *filter_class = (GstVideoFilterClass *) klass;

  gobject_class->set_property = gst_video_lux_set_property;
  gobject_class->get_property = gst_video_lux_get_property;

  g_object_class_install_property (gobject_class, PROP_LUX,
      g_param_spec_double ("lux", "Lux", "Lux adjustment value",
          -10.0, 10.0, DEFAULT_LUX,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_BLACK,
      g_param_spec_double ("black", "Black", "Black adjustment value",
          -2.0, 2.0, DEFAULT_BLACK,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_SHADOW,
      g_param_spec_double ("shadow", "Shadow", "Shadow adjustment value",
          -2.0, 2.0, DEFAULT_SHADOW,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_MIDTONE,
      g_param_spec_double ("midtone", "Midtone", "Midtone adjustment value",
          -2.0, 2.0, DEFAULT_MIDTONE,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_HIGHLIGHT,
      g_param_spec_double ("highlight", "Highlight", "Highlight adjustment value",
          -2.0, 2.0, DEFAULT_HIGHLIGHT,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_WHITE,
      g_param_spec_double ("white", "White", "White adjustment value",
          -2.0, 2.0, DEFAULT_WHITE,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_ORANGE,
      g_param_spec_double ("orange", "Orange", "Orange adjustment value",
          -2.0, 2.0, DEFAULT_ORANGE,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_RED,
      g_param_spec_double ("red", "Red", "Red adjustment value",
          -2.0, 2.0, DEFAULT_RED,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_YELLOW,
      g_param_spec_double ("yellow", "Yellow", "Yellow adjustment value",
          -2.0, 2.0, DEFAULT_YELLOW,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_GREEN,
      g_param_spec_double ("green", "Green", "Green adjustment value",
          -2.0, 2.0, DEFAULT_GREEN,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_CYAN,
      g_param_spec_double ("cyan", "Cyan", "Cyan adjustment value",
          -2.0, 2.0, DEFAULT_CYAN,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_BLUE,
      g_param_spec_double ("blue", "Blue", "Blue adjustment value",
          -2.0, 2.0, DEFAULT_BLUE,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_MAGENTA,
      g_param_spec_double ("magenta", "Magenta", "Magenta adjustment value",
          -2.0, 2.0, DEFAULT_MAGENTA,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_COLOR_BREADTH,
      g_param_spec_double ("color-breadth", "Color Breadth", "Selective color breadth (0.0=narrow, 1.0=broad)",
          0.0, 1.0, DEFAULT_COLOR_BREADTH,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_BLUR,
      g_param_spec_double ("blur", "Blur", "Blur adjustment value",
          -10.0, 10.0, DEFAULT_BLUR,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_SHARPNESS,
      g_param_spec_double ("sharpness", "Sharpness", "Sharpness adjustment value",
          -10.0, 10.0, DEFAULT_SHARPNESS,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  gst_element_class_set_static_metadata (gstelement_class,
      "Video Lux", "Filter/Effect/Video",
      "Applies Lux tone curve adjustments to video",
      "Cheese");

  filter_class->transform_frame =
      GST_DEBUG_FUNCPTR (gst_video_lux_transform_frame);

  gst_element_class_add_static_pad_template (gstelement_class,
      &gst_video_lux_sink_template);
  gst_element_class_add_static_pad_template (gstelement_class,
      &gst_video_lux_src_template);
}

static void
gst_video_lux_init (GstVideoLux * filter)
{
  filter->lux = DEFAULT_LUX;
  filter->black = DEFAULT_BLACK;
  filter->shadow = DEFAULT_SHADOW;
  filter->midtone = DEFAULT_MIDTONE;
  filter->highlight = DEFAULT_HIGHLIGHT;
  filter->white = DEFAULT_WHITE;
  filter->orange = DEFAULT_ORANGE;
  filter->red = DEFAULT_RED;
  filter->yellow = DEFAULT_YELLOW;
  filter->green = DEFAULT_GREEN;
  filter->cyan = DEFAULT_CYAN;
  filter->blue = DEFAULT_BLUE;
  filter->magenta = DEFAULT_MAGENTA;
  filter->color_breadth = DEFAULT_COLOR_BREADTH;
  filter->blur = DEFAULT_BLUR;
  filter->sharpness = DEFAULT_SHARPNESS;
}

static void
gst_video_lux_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstVideoLux *filter = GST_VIDEO_LUX (object);

  switch (prop_id) {
    case PROP_LUX:
      filter->lux = g_value_get_double (value);
      break;
    case PROP_BLACK:
      filter->black = g_value_get_double (value);
      break;
    case PROP_SHADOW:
      filter->shadow = g_value_get_double (value);
      break;
    case PROP_MIDTONE:
      filter->midtone = g_value_get_double (value);
      break;
    case PROP_HIGHLIGHT:
      filter->highlight = g_value_get_double (value);
      break;
    case PROP_WHITE:
      filter->white = g_value_get_double (value);
      break;
    case PROP_ORANGE:
      filter->orange = g_value_get_double (value);
      break;
    case PROP_RED:
      filter->red = g_value_get_double (value);
      break;
    case PROP_YELLOW:
      filter->yellow = g_value_get_double (value);
      break;
    case PROP_GREEN:
      filter->green = g_value_get_double (value);
      break;
    case PROP_CYAN:
      filter->cyan = g_value_get_double (value);
      break;
    case PROP_BLUE:
      filter->blue = g_value_get_double (value);
      break;
    case PROP_MAGENTA:
      filter->magenta = g_value_get_double (value);
      break;
    case PROP_COLOR_BREADTH:
      filter->color_breadth = g_value_get_double (value);
      break;
    case PROP_BLUR:
      filter->blur = g_value_get_double (value);
      break;
    case PROP_SHARPNESS:
      filter->sharpness = g_value_get_double (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_video_lux_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstVideoLux *filter = GST_VIDEO_LUX (object);

  switch (prop_id) {
    case PROP_LUX:
      g_value_set_double (value, filter->lux);
      break;
    case PROP_BLACK:
      g_value_set_double (value, filter->black);
      break;
    case PROP_SHADOW:
      g_value_set_double (value, filter->shadow);
      break;
    case PROP_MIDTONE:
      g_value_set_double (value, filter->midtone);
      break;
    case PROP_HIGHLIGHT:
      g_value_set_double (value, filter->highlight);
      break;
    case PROP_WHITE:
      g_value_set_double (value, filter->white);
      break;
    case PROP_ORANGE:
      g_value_set_double (value, filter->orange);
      break;
    case PROP_RED:
      g_value_set_double (value, filter->red);
      break;
    case PROP_YELLOW:
      g_value_set_double (value, filter->yellow);
      break;
    case PROP_GREEN:
      g_value_set_double (value, filter->green);
      break;
    case PROP_CYAN:
      g_value_set_double (value, filter->cyan);
      break;
    case PROP_BLUE:
      g_value_set_double (value, filter->blue);
      break;
    case PROP_MAGENTA:
      g_value_set_double (value, filter->magenta);
      break;
    case PROP_COLOR_BREADTH:
      g_value_set_double (value, filter->color_breadth);
      break;
    case PROP_BLUR:
      g_value_set_double (value, filter->blur);
      break;
    case PROP_SHARPNESS:
      g_value_set_double (value, filter->sharpness);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static GstFlowReturn
gst_video_lux_transform_frame (GstVideoFilter * base, GstVideoFrame * inframe,
    GstVideoFrame * outframe)
{
  GstVideoLux *filter = GST_VIDEO_LUX (base);
  guint8 *in_data, *out_data;
  gint width, height, in_stride, out_stride;
  gint channels;

  in_data = GST_VIDEO_FRAME_PLANE_DATA (inframe, 0);
  out_data = GST_VIDEO_FRAME_PLANE_DATA (outframe, 0);
  width = GST_VIDEO_FRAME_WIDTH (inframe);
  height = GST_VIDEO_FRAME_HEIGHT (inframe);
  in_stride = GST_VIDEO_FRAME_PLANE_STRIDE (inframe, 0);
  out_stride = GST_VIDEO_FRAME_PLANE_STRIDE (outframe, 0);

  channels = GST_VIDEO_FRAME_N_COMPONENTS (inframe);

  /* Check if any adjustment is needed */
  if (filter->lux == 0.0 && filter->black == 0.0 && filter->shadow == 0.0 &&
      filter->midtone == 0.0 && filter->highlight == 0.0 && filter->white == 0.0 &&
      filter->orange == 0.0 && filter->red == 0.0 && filter->yellow == 0.0 &&
      filter->green == 0.0 && filter->cyan == 0.0 && filter->blue == 0.0 &&
      filter->magenta == 0.0 && filter->blur == 0.0 && filter->sharpness == 0.0) {
    /* No adjustment needed, just copy */
    gst_video_frame_copy (outframe, inframe);
    return GST_FLOW_OK;
  }

  /* Convert values to integer (multiply by 256 for fixed-point arithmetic) */
  gint black_int = (gint) (filter->black * 256.0);
  gint shadow_int = (gint) (filter->shadow * 256.0);
  gint midtone_int = (gint) (filter->midtone * 256.0);
  gint highlight_int = (gint) (filter->highlight * 256.0);
  gint white_int = (gint) (filter->white * 256.0);
  gint orange_int = (gint) (filter->orange * 256.0);
  gint red_int = (gint) (filter->red * 256.0);
  gint yellow_int = (gint) (filter->yellow * 256.0);
  gint green_int = (gint) (filter->green * 256.0);
  gint cyan_int = (gint) (filter->cyan * 256.0);
  gint blue_int = (gint) (filter->blue * 256.0);
  gint magenta_int = (gint) (filter->magenta * 256.0);
  gdouble breadth = filter->color_breadth;

  /* First pass: apply tone curve and orange brightness using integer arithmetic */
  for (gint y = 0; y < height; y++) {
    guint8 *in_row = in_data + y * in_stride;
    guint8 *out_row = out_data + y * out_stride;

    for (gint x = 0; x < width; x++) {
      gint idx = x * channels;
      guint8 r = in_row[idx];
      guint8 g = in_row[idx + 1];
      guint8 b = in_row[idx + 2];

      /* Calculate luminance using integer arithmetic */
      gint luminance = calculate_luminance_int (r, g, b);

      /* Apply tone curve adjustment (returns final adjustment value, already divided by 256) */
      gint tone_adjustment = apply_tone_curve_int (luminance, black_int, shadow_int, midtone_int, highlight_int, white_int);

      /* Apply selective color adjustments */
      gint orange = calculate_orange_component_int (r, g, b, breadth);
      gint orange_adjustment = (orange * orange_int * 77) >> 16; /* 0.3 * 256 = 76.8 ≈ 77 */
      
      gint red = calculate_red_component_int (r, g, b, breadth);
      gint red_adjustment = (red * red_int * 77) >> 16;
      
      gint yellow = calculate_yellow_component_int (r, g, b, breadth);
      gint yellow_adjustment = (yellow * yellow_int * 77) >> 16;
      
      gint green = calculate_green_component_int (r, g, b, breadth);
      gint green_adjustment = (green * green_int * 77) >> 16;
      
      gint cyan = calculate_cyan_component_int (r, g, b, breadth);
      gint cyan_adjustment = (cyan * cyan_int * 77) >> 16;
      
      gint blue = calculate_blue_component_int (r, g, b, breadth);
      gint blue_adjustment = (blue * blue_int * 77) >> 16;
      
      gint magenta = calculate_magenta_component_int (r, g, b, breadth);
      gint magenta_adjustment = (magenta * magenta_int * 77) >> 16;

      /* Total adjustment - all are already final values, just add them */
      gint total_adjustment = tone_adjustment + orange_adjustment + red_adjustment +
                               yellow_adjustment + green_adjustment + cyan_adjustment +
                               blue_adjustment + magenta_adjustment;

      /* Apply to RGB channels */
      gint new_r = CLAMP (r + total_adjustment, 0, 255);
      gint new_g = CLAMP (g + total_adjustment, 0, 255);
      gint new_b = CLAMP (b + total_adjustment, 0, 255);

      out_row[idx] = (guint8) new_r;
      out_row[idx + 1] = (guint8) new_g;
      out_row[idx + 2] = (guint8) new_b;

      /* Copy alpha channel if present */
      if (channels == 4) {
        out_row[idx + 3] = in_row[idx + 3];
      }
    }
  }

  /* Second pass: apply FFT-based blur and sharpness if needed */
  if (filter->blur != 0.0 || filter->sharpness != 0.0) {
    /* Process each color channel separately */
    for (gint c = 0; c < channels && c < 3; c++) {
      apply_fft_filter (out_data, width, height, out_stride, channels, c,
                        filter->blur, filter->sharpness);
    }
  }

  return GST_FLOW_OK;
}

static gboolean
plugin_init (GstPlugin * plugin)
{
  GST_DEBUG_CATEGORY_INIT (gst_video_lux_debug, "video_lux", 0,
      "Video Lux plugin");

  return gst_element_register (plugin, "video_lux", GST_RANK_NONE,
      GST_TYPE_VIDEO_LUX);
}

#ifndef PACKAGE
#define PACKAGE "cheese"
#endif

#ifndef VERSION
#define VERSION "44.1"
#endif

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    video_lux,
    "Video Lux effect plugin",
    plugin_init,
    VERSION,
    "LGPL",
    PACKAGE,
    "https://wiki.gnome.org/Apps/Cheese")

