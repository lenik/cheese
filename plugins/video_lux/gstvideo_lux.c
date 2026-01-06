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
  gdouble blur;
  gdouble sharpness;
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

/* Calculate orange component (warm tones) using integer arithmetic */
static inline gint
calculate_orange_component_int (guint8 r, guint8 g, guint8 b)
{
  /* Orange is high in red, medium in green, low in blue */
  /* 0.6*256=153.6, 0.3*256=76.8, 0.1*256=25.6 */
  gint orange = (153 * r + 77 * g - 26 * b) >> 8;
  return CLAMP (orange, 0, 255);
}

/* Apply sharpness using simple edge detection */
static inline void
apply_sharpness (guint8 * pixels, gint width, gint height, gint stride,
    gint channels, gint x, gint y, gdouble sharpness_val)
{
  if (x < 1 || x >= width - 1 || y < 1 || y >= height - 1)
    return;

  gint idx = y * stride + x * channels;
  gint idx_left = idx - channels;
  gint idx_right = idx + channels;
  gint idx_up = idx - stride;
  gint idx_down = idx + stride;

  gdouble sharpness_factor = 1.0 + sharpness_val * 0.1;

  for (gint c = 0; c < channels && c < 3; c++) {
    gint center = pixels[idx + c];
    gint left = pixels[idx_left + c];
    gint right = pixels[idx_right + c];
    gint up = pixels[idx_up + c];
    gint down = pixels[idx_down + c];

    /* Simple edge detection: difference from neighbors */
    gdouble edge = center - (left + right + up + down) / 4.0;
    gdouble sharpened = center + edge * (sharpness_factor - 1.0);

    pixels[idx + c] = CLAMP ((gint) sharpened, 0, 255);
  }
}

/* Apply blur using simple box blur */
static inline void
apply_blur (guint8 * pixels, gint width, gint height, gint stride,
    gint channels, gint x, gint y, gdouble blur_val)
{
  if (x < 1 || x >= width - 1 || y < 1 || y >= height - 1)
    return;

  gint idx = y * stride + x * channels;
  gint idx_left = idx - channels;
  gint idx_right = idx + channels;
  gint idx_up = idx - stride;
  gint idx_down = idx + stride;

  gdouble blur_factor = blur_val * 0.1;

  for (gint c = 0; c < channels && c < 3; c++) {
    gint center = pixels[idx + c];
    gint left = pixels[idx_left + c];
    gint right = pixels[idx_right + c];
    gint up = pixels[idx_up + c];
    gint down = pixels[idx_down + c];

    /* Box blur: blend with neighbors */
    gdouble blurred = center * (1.0 - blur_factor) + 
                     (left + right + up + down) / 4.0 * blur_factor;

    pixels[idx + c] = CLAMP ((gint) blurred, 0, 255);
  }
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
      filter->orange == 0.0 && filter->blur == 0.0 && filter->sharpness == 0.0) {
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

      /* Apply orange brightness adjustment */
      gint orange = calculate_orange_component_int (r, g, b);
      /* orange * 0.3 * orange_val = orange * orange_int * 0.3 / 256 */
      gint orange_adjustment = (orange * orange_int * 77) >> 16; /* 0.3 * 256 = 76.8 ≈ 77 */

      /* Total adjustment - both are already final values, just add them */
      gint total_adjustment = tone_adjustment + orange_adjustment;

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

  /* Second pass: apply blur if needed */
  if (filter->blur != 0.0) {
    for (gint y = 1; y < height - 1; y++) {
      for (gint x = 1; x < width - 1; x++) {
        apply_blur (out_data, width, height, out_stride, channels, x, y, filter->blur);
      }
    }
  }

  /* Third pass: apply sharpness if needed */
  if (filter->sharpness != 0.0) {
    for (gint y = 1; y < height - 1; y++) {
      for (gint x = 1; x < width - 1; x++) {
        apply_sharpness (out_data, width, height, out_stride, channels, x, y, filter->sharpness);
      }
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

