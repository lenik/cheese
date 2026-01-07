/*
 * GStreamer canvas plugin
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

GST_DEBUG_CATEGORY_STATIC (gst_canvas_debug);
#define GST_CAT_DEFAULT gst_canvas_debug

#define GST_TYPE_CANVAS (gst_canvas_get_type())
#define GST_CANVAS(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_CANVAS,GstCanvas))
#define GST_CANVAS_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_CANVAS,GstCanvasClass))
#define GST_IS_CANVAS(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_CANVAS))
#define GST_IS_CANVAS_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_CANVAS))

typedef struct _GstCanvas GstCanvas;
typedef struct _GstCanvasClass GstCanvasClass;

struct _GstCanvas
{
  GstVideoFilter base;
  gdouble scale;
  gdouble rotation; /* in degrees */
  gdouble pan_x;    /* pan offset in x direction (in pixels) */
  gdouble pan_y;    /* pan offset in y direction (in pixels) */
};

struct _GstCanvasClass
{
  GstVideoFilterClass base_class;
};

enum
{
  PROP_0,
  PROP_SCALE,
  PROP_ROTATION,
  PROP_PAN_X,
  PROP_PAN_Y
};

#define DEFAULT_SCALE 1.0
#define DEFAULT_ROTATION 0.0
#define DEFAULT_PAN_X 0.0
#define DEFAULT_PAN_Y 0.0

static GstStaticPadTemplate gst_canvas_src_template =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_MAKE ("{ RGB, BGR, RGBA, BGRA, ARGB, ABGR }"))
);

static GstStaticPadTemplate gst_canvas_sink_template =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_MAKE ("{ RGB, BGR, RGBA, BGRA, ARGB, ABGR }"))
);

/* Forward declaration */
GType gst_canvas_get_type (void);

G_DEFINE_TYPE (GstCanvas, gst_canvas, GST_TYPE_VIDEO_FILTER);

static void gst_canvas_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_canvas_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static GstFlowReturn gst_canvas_transform_frame (GstVideoFilter * filter,
    GstVideoFrame * inframe, GstVideoFrame * outframe);

/* Bilinear interpolation using fixed-point arithmetic */
static inline void
get_pixel_bilinear_int (guint8 *data, gint width, gint height, gint stride,
                        gint channels, gint x_fixed, gint y_fixed, guint8 *out)
{
  /* x_fixed and y_fixed are in 16.16 fixed point format */
  gint x0 = x_fixed >> 16;
  gint y0 = y_fixed >> 16;
  gint x1 = x0 + 1;
  gint y1 = y0 + 1;
  
  /* Clamp coordinates */
  x0 = CLAMP (x0, 0, width - 1);
  y0 = CLAMP (y0, 0, height - 1);
  x1 = CLAMP (x1, 0, width - 1);
  y1 = CLAMP (y1, 0, height - 1);
  
  /* Fractional parts in 16.16 format */
  gint fx = x_fixed & 0xFFFF;
  gint fy = y_fixed & 0xFFFF;
  gint fx_inv = 65536 - fx;
  gint fy_inv = 65536 - fy;
  
  for (gint c = 0; c < channels; c++) {
    gint idx00 = y0 * stride + x0 * channels + c;
    gint idx01 = y0 * stride + x1 * channels + c;
    gint idx10 = y1 * stride + x0 * channels + c;
    gint idx11 = y1 * stride + x1 * channels + c;
    
    gint v00 = data[idx00];
    gint v01 = data[idx01];
    gint v10 = data[idx10];
    gint v11 = data[idx11];
    
    /* Bilinear interpolation using fixed-point */
    gint v0 = (v00 * fx_inv + v01 * fx) >> 16;
    gint v1 = (v10 * fx_inv + v11 * fx) >> 16;
    gint v = (v0 * fy_inv + v1 * fy) >> 16;
    
    out[c] = (guint8) CLAMP (v, 0, 255);
  }
}

/* Simple nearest-neighbor for scale-only (faster) */
static inline void
get_pixel_nearest (guint8 *data, gint width, gint height, gint stride,
                   gint channels, gint x, gint y, guint8 *out)
{
  x = CLAMP (x, 0, width - 1);
  y = CLAMP (y, 0, height - 1);
  
  gint idx = y * stride + x * channels;
  for (gint c = 0; c < channels; c++) {
    out[c] = data[idx + c];
  }
}

static void
gst_canvas_class_init (GstCanvasClass * klass)
{
  GObjectClass *gobject_class = (GObjectClass *) klass;
  GstElementClass *gstelement_class = (GstElementClass *) klass;
  GstVideoFilterClass *filter_class = (GstVideoFilterClass *) klass;

  gobject_class->set_property = gst_canvas_set_property;
  gobject_class->get_property = gst_canvas_get_property;

  g_object_class_install_property (gobject_class, PROP_SCALE,
      g_param_spec_double ("scale", "Scale", "Scale factor (1.0 = no scaling)",
          0.1, 10.0, DEFAULT_SCALE,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_ROTATION,
      g_param_spec_double ("rotation", "Rotation", "Rotation angle in degrees",
          -360.0, 360.0, DEFAULT_ROTATION,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_PAN_X,
      g_param_spec_double ("pan-x", "Pan X", "Pan offset in X direction (in pixels)",
          -10000.0, 10000.0, DEFAULT_PAN_X,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_PAN_Y,
      g_param_spec_double ("pan-y", "Pan Y", "Pan offset in Y direction (in pixels)",
          -10000.0, 10000.0, DEFAULT_PAN_Y,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  gst_element_class_set_static_metadata (gstelement_class,
      "Video Canvas", "Filter/Effect/Video",
      "Applies scale and rotation transformations to video",
      "Cheese");

  filter_class->transform_frame =
      GST_DEBUG_FUNCPTR (gst_canvas_transform_frame);

  gst_element_class_add_static_pad_template (gstelement_class,
      &gst_canvas_sink_template);
  gst_element_class_add_static_pad_template (gstelement_class,
      &gst_canvas_src_template);
}

static void
gst_canvas_init (GstCanvas * filter)
{
  filter->scale = DEFAULT_SCALE;
  filter->rotation = DEFAULT_ROTATION;
  filter->pan_x = DEFAULT_PAN_X;
  filter->pan_y = DEFAULT_PAN_Y;
}

static void
gst_canvas_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstCanvas *filter = GST_CANVAS (object);

  switch (prop_id) {
    case PROP_SCALE:
      filter->scale = g_value_get_double (value);
      break;
    case PROP_ROTATION:
      filter->rotation = g_value_get_double (value);
      break;
    case PROP_PAN_X:
      filter->pan_x = g_value_get_double (value);
      break;
    case PROP_PAN_Y:
      filter->pan_y = g_value_get_double (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_canvas_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstCanvas *filter = GST_CANVAS (object);

  switch (prop_id) {
    case PROP_SCALE:
      g_value_set_double (value, filter->scale);
      break;
    case PROP_ROTATION:
      g_value_set_double (value, filter->rotation);
      break;
    case PROP_PAN_X:
      g_value_set_double (value, filter->pan_x);
      break;
    case PROP_PAN_Y:
      g_value_set_double (value, filter->pan_y);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

/* Optimized scale-only path using fixed-point arithmetic */
static void
transform_scale_only (guint8 *in_data, guint8 *out_data,
                      gint width, gint height, gint in_stride, gint out_stride,
                      gint channels, gdouble scale, gdouble pan_x, gdouble pan_y)
{
  gint center_x = width / 2;
  gint center_y = height / 2;
  
  /* Convert scale to fixed-point (16.16 format) */
  gint scale_fixed = (gint)((1.0 / scale) * 65536.0);
  gint pan_x_fixed = (gint)(pan_x * 65536.0);
  gint pan_y_fixed = (gint)(pan_y * 65536.0);
  gint center_x_fixed = center_x << 16;
  gint center_y_fixed = center_y << 16;
  
  for (gint y = 0; y < height; y++) {
    guint8 *out_row = out_data + y * out_stride;
    /* Calculate y_offset: (y - center_y) * (1/scale) */
    gint y_offset = ((y - center_y) * scale_fixed) >> 16;
    
    for (gint x = 0; x < width; x++) {
      gint idx = x * channels;
      
      /* Calculate source coordinates in fixed-point */
      gint x_offset = ((x - center_x) * scale_fixed) >> 16;
      gint src_x_fixed = center_x_fixed + (x_offset << 16) + pan_x_fixed;
      gint src_y_fixed = center_y_fixed + (y_offset << 16) + pan_y_fixed;
      
      /* Sample using bilinear interpolation */
      get_pixel_bilinear_int (in_data, width, height, in_stride, channels,
                             src_x_fixed, src_y_fixed, out_row + idx);
    }
  }
}

/* Optimized path for 90/180/270/360 degree rotations */
static void
transform_scale_rotation_90deg (guint8 *in_data, guint8 *out_data,
                                gint width, gint height, gint in_stride, gint out_stride,
                                gint channels, gdouble scale, gint rotation_deg,
                                gdouble pan_x, gdouble pan_y)
{
  gint center_x = width / 2;
  gint center_y = height / 2;
  gint scale_fixed = (gint)((1.0 / scale) * 65536.0);
  gint pan_x_fixed = (gint)(pan_x * 65536.0);
  gint pan_y_fixed = (gint)(pan_y * 65536.0);
  gint center_x_fixed = center_x << 16;
  gint center_y_fixed = center_y << 16;
  
  /* Normalize rotation to 0, 90, 180, 270 */
  rotation_deg = rotation_deg % 360;
  if (rotation_deg < 0) rotation_deg += 360;
  
  /* Switch on rotation once, then use optimized loops */
  switch (rotation_deg) {
    case 0: {
      /* No rotation: (x,y) -> (x, y) */
      for (gint y = 0; y < height; y++) {
        guint8 *out_row = out_data + y * out_stride;
        gint y_offset = ((y - center_y) * scale_fixed) >> 16;
        
        for (gint x = 0; x < width; x++) {
          gint idx = x * channels;
          gint x_offset = ((x - center_x) * scale_fixed) >> 16;
          gint src_x_fixed = center_x_fixed + (x_offset << 16) + pan_x_fixed;
          gint src_y_fixed = center_y_fixed + (y_offset << 16) + pan_y_fixed;
          
          get_pixel_bilinear_int (in_data, width, height, in_stride, channels,
                                 src_x_fixed, src_y_fixed, out_row + idx);
        }
      }
      break;
    }
    case 90: {
      /* Rotate 90: (x,y) -> (-y, x) */
      for (gint y = 0; y < height; y++) {
        guint8 *out_row = out_data + y * out_stride;
        gint y_offset = ((y - center_y) * scale_fixed) >> 16;
        
        for (gint x = 0; x < width; x++) {
          gint idx = x * channels;
          gint x_offset = ((x - center_x) * scale_fixed) >> 16;
          gint src_x_fixed = center_x_fixed + ((-y_offset) << 16) + pan_x_fixed;
          gint src_y_fixed = center_y_fixed + (x_offset << 16) + pan_y_fixed;
          
          get_pixel_bilinear_int (in_data, width, height, in_stride, channels,
                                 src_x_fixed, src_y_fixed, out_row + idx);
        }
      }
      break;
    }
    case 180: {
      /* Rotate 180: (x,y) -> (-x, -y) */
      for (gint y = 0; y < height; y++) {
        guint8 *out_row = out_data + y * out_stride;
        gint y_offset = ((y - center_y) * scale_fixed) >> 16;
        
        for (gint x = 0; x < width; x++) {
          gint idx = x * channels;
          gint x_offset = ((x - center_x) * scale_fixed) >> 16;
          gint src_x_fixed = center_x_fixed + ((-x_offset) << 16) + pan_x_fixed;
          gint src_y_fixed = center_y_fixed + ((-y_offset) << 16) + pan_y_fixed;
          
          get_pixel_bilinear_int (in_data, width, height, in_stride, channels,
                                 src_x_fixed, src_y_fixed, out_row + idx);
        }
      }
      break;
    }
    case 270: {
      /* Rotate 270: (x,y) -> (y, -x) */
      for (gint y = 0; y < height; y++) {
        guint8 *out_row = out_data + y * out_stride;
        gint y_offset = ((y - center_y) * scale_fixed) >> 16;
        
        for (gint x = 0; x < width; x++) {
          gint idx = x * channels;
          gint x_offset = ((x - center_x) * scale_fixed) >> 16;
          gint src_x_fixed = center_x_fixed + (y_offset << 16) + pan_x_fixed;
          gint src_y_fixed = center_y_fixed + ((-x_offset) << 16) + pan_y_fixed;
          
          get_pixel_bilinear_int (in_data, width, height, in_stride, channels,
                                 src_x_fixed, src_y_fixed, out_row + idx);
        }
      }
      break;
    }
    default: {
      /* Should not happen, but fallback to no rotation */
      for (gint y = 0; y < height; y++) {
        guint8 *out_row = out_data + y * out_stride;
        gint y_offset = ((y - center_y) * scale_fixed) >> 16;
        
        for (gint x = 0; x < width; x++) {
          gint idx = x * channels;
          gint x_offset = ((x - center_x) * scale_fixed) >> 16;
          gint src_x_fixed = center_x_fixed + (x_offset << 16) + pan_x_fixed;
          gint src_y_fixed = center_y_fixed + (y_offset << 16) + pan_y_fixed;
          
          get_pixel_bilinear_int (in_data, width, height, in_stride, channels,
                                 src_x_fixed, src_y_fixed, out_row + idx);
        }
      }
      break;
    }
  }
}

/* Generic path for arbitrary rotation angles */
static void
transform_scale_rotation_generic (guint8 *in_data, guint8 *out_data,
                                   gint width, gint height, gint in_stride, gint out_stride,
                                   gint channels, gdouble scale, gdouble rotation,
                                   gdouble pan_x, gdouble pan_y)
{
  gint center_x = width / 2;
  gint center_y = height / 2;
  gint scale_fixed = (gint)((1.0 / scale) * 65536.0);
  gint pan_x_fixed = (gint)(pan_x * 65536.0);
  gint pan_y_fixed = (gint)(pan_y * 65536.0);
  gint center_x_fixed = center_x << 16;
  gint center_y_fixed = center_y << 16;
  
  /* Pre-compute rotation coefficients in fixed-point */
  gdouble angle_rad = rotation * M_PI / 180.0;
  gdouble cos_a = cos (angle_rad);
  gdouble sin_a = sin (angle_rad);
  gint cos_a_fixed = (gint)(cos_a * 65536.0);
  gint sin_a_fixed = (gint)(sin_a * 65536.0);
  
  for (gint y = 0; y < height; y++) {
    guint8 *out_row = out_data + y * out_stride;
    gint y_offset = ((y - center_y) * scale_fixed) >> 16;
    
    for (gint x = 0; x < width; x++) {
      gint idx = x * channels;
      gint x_offset = ((x - center_x) * scale_fixed) >> 16;
      
      /* Apply inverse rotation using fixed-point arithmetic */
      /* For clockwise rotation, the inverse transform is:
       *   x = x'*cos(θ) - y'*sin(θ)
       *   y = x'*sin(θ) + y'*cos(θ)
       * This matches the 90° case: (x', y') -> (-y', x') for 90° clockwise */
      /* Convert offsets to fixed-point for rotation */
      gint x_offset_fixed = x_offset << 16;
      gint y_offset_fixed = y_offset << 16;
      /* Inverse rotation matrix for clockwise rotation - use 64-bit to avoid overflow */
      gint64 rot_x_64 = ((gint64)x_offset_fixed * (gint64)cos_a_fixed - (gint64)y_offset_fixed * (gint64)sin_a_fixed) >> 16;
      gint64 rot_y_64 = ((gint64)x_offset_fixed * (gint64)sin_a_fixed + (gint64)y_offset_fixed * (gint64)cos_a_fixed) >> 16;
      gint rot_x = (gint)rot_x_64;
      gint rot_y = (gint)rot_y_64;
      gint src_x_fixed = center_x_fixed + rot_x + pan_x_fixed;
      gint src_y_fixed = center_y_fixed + rot_y + pan_y_fixed;
      
      get_pixel_bilinear_int (in_data, width, height, in_stride, channels,
                             src_x_fixed, src_y_fixed, out_row + idx);
    }
  }
}

static GstFlowReturn
gst_canvas_transform_frame (GstVideoFilter * base, GstVideoFrame * inframe,
    GstVideoFrame * outframe)
{
  GstCanvas *filter = GST_CANVAS (base);
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

  /* Check if any transformation is needed */
  if (filter->scale == 1.0 && filter->rotation == 0.0 &&
      filter->pan_x == 0.0 && filter->pan_y == 0.0) {
    /* No transformation needed, just copy */
    gst_video_frame_copy (outframe, inframe);
    return GST_FLOW_OK;
  }

  /* Clear output frame (black background) */
  memset (out_data, 0, height * out_stride);

  /* Check for optimized paths */
  gint rotation_deg = (gint)(filter->rotation + 0.5); /* Round to nearest integer */
  gboolean is_90deg_rotation = (rotation_deg % 90 == 0);
  
  if (filter->rotation == 0.0) {
    /* Fastest path: scale-only (no rotation) */
    transform_scale_only (in_data, out_data, width, height,
                         in_stride, out_stride, channels,
                         filter->scale, filter->pan_x, filter->pan_y);
  } else if (is_90deg_rotation) {
    /* Optimized path for scale with 90/180/270/360 degree rotations */
    transform_scale_rotation_90deg (in_data, out_data, width, height,
                                    in_stride, out_stride, channels,
                                    filter->scale, rotation_deg,
                                    filter->pan_x, filter->pan_y);
  } else {
    /* General case: use generic function for arbitrary rotation */
    transform_scale_rotation_generic (in_data, out_data, width, height,
                                     in_stride, out_stride, channels,
                                     filter->scale, filter->rotation,
                                     filter->pan_x, filter->pan_y);
  }

  return GST_FLOW_OK;
}

static gboolean
plugin_init (GstPlugin * plugin)
{
  GST_DEBUG_CATEGORY_INIT (gst_canvas_debug, "canvas", 0,
      "Video Canvas plugin");

  return gst_element_register (plugin, "canvas", GST_RANK_NONE,
      GST_TYPE_CANVAS);
}

#ifndef PACKAGE
#define PACKAGE "cheese"
#endif

#ifndef VERSION
#define VERSION "44.1"
#endif

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    canvas,
    "Video Canvas effect plugin",
    plugin_init,
    VERSION,
    "LGPL",
    PACKAGE,
    "https://wiki.gnome.org/Apps/Cheese")

