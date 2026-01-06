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
};

struct _GstVideoLuxClass
{
  GstVideoFilterClass base_class;
};

enum
{
  PROP_0,
  PROP_LUX
};

#define DEFAULT_LUX 0.0

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

/* Apply tone curve adjustment based on luminance (returns adjustment * 256 for integer math) */
static inline gint
apply_tone_curve_int (gint luminance, gint lux_int)
{
  /* Normalized luminance thresholds: 0.1*255=25.5, 0.3*255=76.5, 0.7*255=178.5, 0.9*255=229.5 */
  if (luminance < 26) {
    /* Black: +2lux */
    return (lux_int * 2) << 8;
  } else if (luminance < 77) {
    /* Shadow: +lux */
    return lux_int << 8;
  } else if (luminance < 179) {
    /* Midtones: no adjustment */
    return 0;
  } else if (luminance < 230) {
    /* Highlight: -lux */
    return -(lux_int << 8);
  } else {
    /* White: -2lux */
    return -(lux_int * 2) << 8;
  }
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
    gint channels, gint x, gint y, gdouble lux)
{
  if (x < 1 || x >= width - 1 || y < 1 || y >= height - 1)
    return;

  gint idx = y * stride + x * channels;
  gint idx_left = idx - channels;
  gint idx_right = idx + channels;
  gint idx_up = idx - stride;
  gint idx_down = idx + stride;

  gdouble sharpness_factor = 1.0 + lux * 0.1;

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

  if (filter->lux == 0.0) {
    /* No adjustment needed, just copy */
    gst_video_frame_copy (outframe, inframe);
    return GST_FLOW_OK;
  }

  /* Convert lux to integer (multiply by 256 for fixed-point arithmetic) */
  gint lux_int = (gint) (filter->lux * 256.0);

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

      /* Apply tone curve adjustment (returns adjustment * 256) */
      gint tone_adjustment = apply_tone_curve_int (luminance, lux_int);

      /* Apply orange brightness adjustment */
      gint orange = calculate_orange_component_int (r, g, b);
      /* orange * 0.3 * lux = orange * lux * 0.3 = (orange * lux_int * 0.3) / 256 */
      gint orange_adjustment = (orange * lux_int * 77) >> 16; /* 0.3 * 256 = 76.8 ≈ 77 */

      /* Total adjustment (divide by 256 to get final value) */
      gint total_adjustment = (tone_adjustment + (orange_adjustment << 8)) >> 8;

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

  /* Second pass: apply sharpness (only if lux > 0 and make it less intensive) */
//   if (filter->lux > 0.0 && filter->lux < 5.0) {
//     /* Only apply sharpness on every other pixel to reduce computation */
//     for (gint y = 2; y < height - 2; y += 2) {
//       for (gint x = 2; x < width - 2; x += 2) {
//         apply_sharpness (out_data, width, height, out_stride, channels, x, y,
//             filter->lux);
//       }
//     }
//   }

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

