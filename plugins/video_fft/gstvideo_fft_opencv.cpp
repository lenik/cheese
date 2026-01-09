#ifdef HAVE_OPENCV
#include <opencv2/opencv.hpp>
#include <math.h>
#include <glib.h>
#ifdef G_OS_UNIX
#include <time.h>
#endif

/* Calculate next power of 2 - inline for performance */
static inline gint
next_power_of_2 (gint n)
{
  gint p = 1;
  while (p < n)
    p <<= 1;
  return p;
}

/* CAS (Contrast Adaptive Sharpening) - spatial domain sharpening */
static void
apply_cas_sharpening (guint8 *pixels, gint width, gint height, gint stride,
                      gint channels, gint channel_idx, gdouble sharp_val)
{
  using namespace cv;
  
  /* Create Mat from input */
  Mat input_mat(height, width, CV_8UC1);
  for (gint j = 0; j < height; j++) {
    guint8 *src_row = pixels + j * stride;
    guint8 *dst_row = input_mat.ptr<guint8>(j);
    for (gint i = 0; i < width; i++) {
      dst_row[i] = src_row[i * channels + channel_idx];
    }
  }
  
  /* CAS parameters */
  gdouble sharp_strength = fabs(sharp_val);
  gdouble sharpness = (sharp_strength / 10.0) * 0.4;  /* Scale to 0-0.4 range */
  
  /* CAS kernel: sharpens while preserving contrast */
  /* We'll use a simple unsharp mask approach optimized for CAS */
  Mat blurred;
  GaussianBlur(input_mat, blurred, Size(0, 0), 1.0);
  
  Mat result;
  addWeighted(input_mat, 1.0 + sharpness, blurred, -sharpness, 0, result);
  
  /* Copy back to output */
  for (gint j = 0; j < height; j++) {
    guint8 *src_row = result.ptr<guint8>(j);
    guint8 *dst_row = pixels + j * stride;
    for (gint i = 0; i < width; i++) {
      dst_row[i * channels + channel_idx] = src_row[i];
    }
  }
}

/* Apply frequency domain filter using OpenCV's optimized FFT */
extern "C" void
apply_fft_filter_opencv (guint8 *pixels, gint width, gint height, gint stride,
                         gint channels, gint channel_idx, gdouble blur_val, gdouble sharp_val)
{
  using namespace cv;
  
  /* If only sharpening (no blur), use CAS instead of FFT */
  if (blur_val == 0.0 && sharp_val != 0.0) {
    apply_cas_sharpening(pixels, width, height, stride, channels, channel_idx, sharp_val);
    return;
  }
  
  /* If no blur or sharp, return early */
  if (blur_val == 0.0 && sharp_val == 0.0) {
    return;
  }
  
#ifdef G_OS_UNIX
  struct timespec start, end;
  gdouble time_total = 0, time_copy_in = 0, time_convert = 0;
  gdouble time_pad = 0, time_fft_fwd = 0, time_filter = 0;
  gdouble time_fft_inv = 0, time_copy_out = 0;
  clock_gettime (CLOCK_MONOTONIC, &start);
#endif
  
  gint orig_width = width;
  gint orig_height = height;
  gint scale_factor = 1;
  
  /* Downsample to 1/4 for blur operations to improve performance */
  if (blur_val != 0.0) {
    scale_factor = 4;  /* 1/4 scale = 1/16 area */
    width = width / scale_factor;
    height = height / scale_factor;
  }
  
  /* Create OpenCV Mat from input */
  Mat input_mat(height, width, CV_8UC1);
  if (scale_factor > 1) {
    /* Downsampled copy - simple box filter */
    for (gint j = 0; j < height; j++) {
      gint src_j = j * scale_factor;
      guint8 *dst_row = input_mat.ptr<guint8>(j);
      for (gint i = 0; i < width; i++) {
        gint src_i = i * scale_factor;
        gint sum = 0;
        gint count = 0;
        gint max_src_j = (src_j + scale_factor < orig_height) ? src_j + scale_factor : orig_height;
        gint max_src_i = (src_i + scale_factor < orig_width) ? src_i + scale_factor : orig_width;
        for (gint sj = src_j; sj < max_src_j; sj++) {
          guint8 *row = pixels + sj * stride;
          for (gint si = src_i; si < max_src_i; si++) {
            sum += row[si * channels + channel_idx];
            count++;
          }
        }
        dst_row[i] = (count > 0) ? (sum / count) : 0;
      }
    }
  } else {
    /* Normal copy */
    for (gint j = 0; j < height; j++) {
      guint8 *src_row = pixels + j * stride;
      guint8 *dst_row = input_mat.ptr<guint8>(j);
      for (gint i = 0; i < width; i++) {
        dst_row[i] = src_row[i * channels + channel_idx];
      }
    }
  }
  
#ifdef G_OS_UNIX
  clock_gettime(CLOCK_MONOTONIC, &end);
  time_copy_in = (end.tv_sec - start.tv_sec) * 1e9 + (end.tv_nsec - start.tv_nsec);
  clock_gettime(CLOCK_MONOTONIC, &start);
#endif
  
  /* Convert to float for FFT */
  Mat float_mat;
  input_mat.convertTo(float_mat, CV_32F);
  
#ifdef G_OS_UNIX
  clock_gettime(CLOCK_MONOTONIC, &end);
  time_convert = (end.tv_sec - start.tv_sec) * 1e9 + (end.tv_nsec - start.tv_nsec);
  clock_gettime(CLOCK_MONOTONIC, &start);
#endif
  
  /* Pad to optimal FFT size */
  Mat padded;
  int optimal_rows = getOptimalDFTSize(float_mat.rows);
  int optimal_cols = getOptimalDFTSize(float_mat.cols);
  copyMakeBorder(float_mat, padded, 0, optimal_rows - float_mat.rows,
                 0, optimal_cols - float_mat.cols, BORDER_CONSTANT, Scalar::all(0));
  
#ifdef G_OS_UNIX
  clock_gettime(CLOCK_MONOTONIC, &end);
  time_pad = (end.tv_sec - start.tv_sec) * 1e9 + (end.tv_nsec - start.tv_nsec);
  clock_gettime(CLOCK_MONOTONIC, &start);
#endif
  
  /* Create complex matrix (real + imaginary) */
  Mat planes[] = {padded, Mat::zeros(padded.size(), CV_32F)};
  Mat complex_mat;
  merge(planes, 2, complex_mat);
  
  /* Forward FFT */
  dft(complex_mat, complex_mat, DFT_COMPLEX_OUTPUT);
  
#ifdef G_OS_UNIX
  clock_gettime(CLOCK_MONOTONIC, &end);
  time_fft_fwd = (end.tv_sec - start.tv_sec) * 1e9 + (end.tv_nsec - start.tv_nsec);
  clock_gettime(CLOCK_MONOTONIC, &start);
#endif
  
  /* Apply frequency filter - calculate frequencies from (0,0) where DC is located */
  /* No need to shift - we'll calculate frequencies relative to DC at (0,0) */
  gdouble center_x = optimal_cols / 2.0;
  gdouble center_y = optimal_rows / 2.0;
  gdouble max_freq = sqrt(center_x * center_x + center_y * center_y);
  gdouble inv_max_freq = 1.0 / max_freq;
  
  gdouble sigma = 0.0;
  gboolean has_blur = (blur_val != 0.0);
  if (has_blur) {
    gdouble blur_strength = fabs(blur_val);
    gdouble sigma_val = 0.1 + (blur_strength / 10.0) * 0.7;
    sigma = 2.0 * sigma_val * sigma_val;
  }
  
  /* Note: Sharpening is now handled by CAS, so we skip FFT-based sharpening */
  gboolean has_sharp = FALSE;  /* Disable FFT sharpening - use CAS instead */
  
  /* Split into real and imaginary planes */
  split(complex_mat, planes);
  Mat &real_plane = planes[0];
  Mat &imag_plane = planes[1];
  
  /* Preserve DC component at (0,0) */
  gfloat dc_real = real_plane.at<gfloat>(0, 0);
  gfloat dc_imag = imag_plane.at<gfloat>(0, 0);
  
  /* Apply frequency response - calculate frequency from DC at (0,0) */
  /* Pre-compute constants for optimization */
  gdouble inv_0_1 = 10.0;  /* 1.0 / 0.1 for low frequency adjustment */
  
  /* Pre-compute wrapped j values and dy_sq to avoid repeated calculations */
  gdouble *dy_sq_arr = (gdouble *)malloc(optimal_rows * sizeof(gdouble));
  for (gint j = 0; j < optimal_rows; j++) {
    gint j_wrapped = (j <= center_y) ? j : j - optimal_rows;
    gdouble dy = j_wrapped;
    dy_sq_arr[j] = dy * dy;
  }
  
  /* Pre-compute wrapped i values and dx to avoid repeated calculations */
  gint *i_wrapped_arr = (gint *)malloc(optimal_cols * sizeof(gint));
  gdouble *dx_arr = (gdouble *)malloc(optimal_cols * sizeof(gdouble));
  for (gint i = 0; i < optimal_cols; i++) {
    gint i_wrapped = (i <= center_x) ? i : i - optimal_cols;
    i_wrapped_arr[i] = i_wrapped;
    dx_arr[i] = i_wrapped;
  }
  
  for (gint j = 0; j < optimal_rows; j++) {
    gfloat *real_row = real_plane.ptr<gfloat>(j);
    gfloat *imag_row = imag_plane.ptr<gfloat>(j);
    
    /* Use pre-computed dy_sq */
    gdouble dy_sq = dy_sq_arr[j];
    
    for (gint i = 0; i < optimal_cols; i++) {
      if (i == 0 && j == 0) continue;  /* Skip DC component */
      
      /* Use pre-computed dx */
      gdouble dx = dx_arr[i];
      gdouble freq_sq = dx * dx + dy_sq;
      gdouble freq = sqrt(freq_sq);
      gdouble normalized_freq = freq * inv_max_freq;
      
      gdouble response = 1.0;
      
      if (has_blur) {
        /* Blur: Gaussian-like low-pass filter - preserve low frequencies better */
        gdouble normalized_freq_sq = normalized_freq * normalized_freq;
        /* Use gentler falloff to avoid darkening */
        response = exp(-normalized_freq_sq / sigma);
        /* Ensure response doesn't go too low for low frequencies */
        if (normalized_freq < 0.1) {
          response = 0.9 + 0.1 * (normalized_freq * inv_0_1) * response;
        }
      }
      
      /* Sharpening is handled by CAS, not FFT */
      
      real_row[i] *= response;
      imag_row[i] *= response;
    }
  }
  
  free(dy_sq_arr);
  free(i_wrapped_arr);
  free(dx_arr);
  
  /* Restore DC component exactly to preserve brightness */
  real_plane.at<gfloat>(0, 0) = dc_real;
  imag_plane.at<gfloat>(0, 0) = dc_imag;
  
#ifdef G_OS_UNIX
  clock_gettime(CLOCK_MONOTONIC, &end);
  time_filter = (end.tv_sec - start.tv_sec) * 1e9 + (end.tv_nsec - start.tv_nsec);
  clock_gettime(CLOCK_MONOTONIC, &start);
#endif
  
  /* Merge back */
  merge(planes, 2, complex_mat);
  
  /* Inverse FFT */
  dft(complex_mat, complex_mat, DFT_INVERSE | DFT_SCALE);
  
#ifdef G_OS_UNIX
  clock_gettime(CLOCK_MONOTONIC, &end);
  time_fft_inv = (end.tv_sec - start.tv_sec) * 1e9 + (end.tv_nsec - start.tv_nsec);
  clock_gettime(CLOCK_MONOTONIC, &start);
#endif
  
  /* Extract real part */
  split(complex_mat, planes);
  Mat result_float = planes[0];
  
  /* Crop to original size */
  Mat cropped = result_float(Rect(0, 0, width, height));
  
  /* Convert back to uint8 */
  Mat result_uint8;
  cropped.convertTo(result_uint8, CV_8U);
  
#ifdef G_OS_UNIX
  clock_gettime(CLOCK_MONOTONIC, &end);
  gdouble time_convert_back = (end.tv_sec - start.tv_sec) * 1e9 + (end.tv_nsec - start.tv_nsec);
  clock_gettime(CLOCK_MONOTONIC, &start);
#endif
  
  /* Copy back to output (with upsampling if needed) */
  if (scale_factor > 1) {
    /* Upsample using bilinear interpolation */
    Mat upsampled;
    resize(result_uint8, upsampled, Size(orig_width, orig_height), 0, 0, INTER_LINEAR);
    
    for (gint j = 0; j < orig_height; j++) {
      guint8 *src_row = upsampled.ptr<guint8>(j);
      guint8 *dst_row = pixels + j * stride;
      for (gint i = 0; i < orig_width; i++) {
        dst_row[i * channels + channel_idx] = src_row[i];
      }
    }
  } else {
    /* Normal copy */
    for (gint j = 0; j < height; j++) {
      guint8 *src_row = result_uint8.ptr<guint8>(j);
      guint8 *dst_row = pixels + j * stride;
      for (gint i = 0; i < width; i++) {
        dst_row[i * channels + channel_idx] = src_row[i];
      }
    }
  }
  
#ifdef G_OS_UNIX
  clock_gettime(CLOCK_MONOTONIC, &end);
  time_copy_out = (end.tv_sec - start.tv_sec) * 1e9 + (end.tv_nsec - start.tv_nsec);
  time_total = time_copy_in + time_convert + time_pad + time_fft_fwd + 
               time_filter + time_fft_inv + time_convert_back + time_copy_out;
  
  static gint profile_count = 0;
  if (channel_idx == 0 && (++profile_count % 30 == 0)) {
    g_print("FFT Profile (OpenCV) [%dx%d->%dx%d]: copy_in=%.1f%% convert=%.1f%% pad=%.1f%% "
            "fft_fwd=%.1f%% filter=%.1f%% fft_inv=%.1f%% convert_back=%.1f%% copy_out=%.1f%% total=%.2fms\n",
            width, height, optimal_cols, optimal_rows,
            (time_copy_in / time_total) * 100.0,
            (time_convert / time_total) * 100.0,
            (time_pad / time_total) * 100.0,
            (time_fft_fwd / time_total) * 100.0,
            (time_filter / time_total) * 100.0,
            (time_fft_inv / time_total) * 100.0,
            (time_convert_back / time_total) * 100.0,
            (time_copy_out / time_total) * 100.0,
            time_total / 1e6);
  }
#endif
}
#endif

