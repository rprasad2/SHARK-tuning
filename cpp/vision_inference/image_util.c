// Copyright 2021 The IREE Authors
//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include "image_util.h"

#include <math.h>

#include "iree/base/internal/flags.h"
#include "iree/base/tracing.h"

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

iree_status_t iree_tools_utils_pixel_rescaled_to_buffer(
    const uint8_t* pixel_data, iree_host_size_t buffer_length,
    const float* input_range, iree_host_size_t range_length,
    float* out_buffer) {
  IREE_TRACE_ZONE_BEGIN(z0);
  if (range_length != 2) {
    IREE_TRACE_ZONE_END(z0);
    return iree_make_status(IREE_STATUS_INVALID_ARGUMENT,
                            "range defined as 2-element [min, max] array.");
  }
  float input_scale = fabsf(input_range[1] - input_range[0]) / 2.0f;
  float input_offset = (input_range[0] + input_range[1]) / 2.0f;
  const float kUint8Mean = 127.5f;
  for (int i = 0; i < buffer_length; ++i) {
    out_buffer[i] =
        (((float)(pixel_data[i])) - kUint8Mean) / kUint8Mean * input_scale +
        input_offset;
  }
  IREE_TRACE_ZONE_END(z0);
  return iree_ok_status();
}

iree_status_t iree_tools_utils_load_pixel_data_impl(
    const iree_string_view_t filename, const iree_hal_dim_t* shape,
    iree_host_size_t shape_rank, iree_hal_element_type_t element_type,
    uint8_t** out_pixel_data, iree_host_size_t* out_buffer_length) {
  int img_dims[3];
  if (stbi_info(filename.data, img_dims, &(img_dims[1]), &(img_dims[2])) == 0) {
    return iree_make_status(IREE_STATUS_NOT_FOUND, "can't load image %.*s",
                            (int)filename.size, filename.data);
  }
  if (!(element_type == IREE_HAL_ELEMENT_TYPE_FLOAT_32 ||
        element_type == IREE_HAL_ELEMENT_TYPE_SINT_8 ||
        element_type == IREE_HAL_ELEMENT_TYPE_UINT_8)) {
    char element_type_str[16];
    IREE_RETURN_IF_ERROR(iree_hal_format_element_type(
        element_type, sizeof(element_type_str), element_type_str, NULL));
    return iree_make_status(IREE_STATUS_UNIMPLEMENTED,
                            "element type %s not supported", element_type_str);
  }
  switch (shape_rank) {
    case 2: {  // Assume tensor <height x width>
      if (img_dims[2] != 1 || (shape[0] != img_dims[1]) ||
          (shape[1] != img_dims[0])) {
        return iree_make_status(
            IREE_STATUS_INVALID_ARGUMENT,
            "image size: %dx%dx%d, expected: %" PRIdim "x%" PRIdim, img_dims[0],
            img_dims[1], img_dims[2], shape[1], shape[0]);
      }
      break;
    }
    case 3: {  // Assume tensor <height x width x channel>
      if (shape[0] != img_dims[1] || shape[1] != img_dims[0] ||
          shape[2] != img_dims[2]) {
        return iree_make_status(IREE_STATUS_INVALID_ARGUMENT,
                                "image size: %dx%dx%d, expected: %" PRIdim
                                "x%" PRIdim "x%" PRIdim,
                                img_dims[0], img_dims[1], img_dims[2], shape[1],
                                shape[0], shape[2]);
      }
      break;
    }
    case 4: {  // Assume tensor <batch x height x width x channel>
      if (shape[1] != img_dims[1] || shape[2] != img_dims[0] ||
          shape[3] != img_dims[2]) {
        return iree_make_status(IREE_STATUS_INVALID_ARGUMENT,
                                "image size: %dx%dx%d, expected: %" PRIdim
                                "x%" PRIdim "x%" PRIdim,
                                img_dims[0], img_dims[1], img_dims[2], shape[2],
                                shape[1], shape[3]);
      }
      break;
    }
    default:
      return iree_make_status(
          IREE_STATUS_INVALID_ARGUMENT,
          "Input buffer shape rank %" PRIhsz " not supported", shape_rank);
  }
  // Drop the alpha channel if present.
  int req_ch = (img_dims[2] >= 3) ? 3 : 0;
  *out_pixel_data = stbi_load(filename.data, img_dims, &(img_dims[1]),
                              &(img_dims[2]), req_ch);
  if (*out_pixel_data == NULL) {
    return iree_make_status(IREE_STATUS_NOT_FOUND, "can't load image %.*s",
                            (int)filename.size, filename.data);
  }
  *out_buffer_length =
      img_dims[0] * img_dims[1] * (img_dims[2] > 3 ? 3 : img_dims[2]);
  return iree_ok_status();
}

iree_status_t iree_tools_utils_load_pixel_data(
    const iree_string_view_t filename, const iree_hal_dim_t* shape,
    iree_host_size_t shape_rank, iree_hal_element_type_t element_type,
    uint8_t** out_pixel_data, iree_host_size_t* out_buffer_length) {
  IREE_TRACE_ZONE_BEGIN(z0);
  iree_status_t result = iree_tools_utils_load_pixel_data_impl(
      filename, shape, shape_rank, element_type, out_pixel_data,
      out_buffer_length);
  IREE_TRACE_ZONE_END(z0);
  return result;
}

iree_status_t iree_tools_utils_buffer_view_from_image(
    const iree_string_view_t filename, const iree_hal_dim_t* shape,
    iree_host_size_t shape_rank, iree_hal_element_type_t element_type,
    iree_hal_allocator_t* allocator, iree_hal_buffer_view_t** out_buffer_view) {
  IREE_TRACE_ZONE_BEGIN(z0);
  *out_buffer_view = NULL;
  if (element_type != IREE_HAL_ELEMENT_TYPE_SINT_8 &&
      element_type != IREE_HAL_ELEMENT_TYPE_UINT_8) {
    IREE_TRACE_ZONE_END(z0);
    return iree_make_status(IREE_STATUS_INVALID_ARGUMENT,
                            "element type should be i8 or u8");
  }

  iree_status_t result;
  uint8_t* pixel_data = NULL;
  iree_host_size_t buffer_length;
  result = iree_tools_utils_load_pixel_data(
      filename, shape, shape_rank, element_type, &pixel_data, &buffer_length);
  if (iree_status_is_ok(result)) {
    iree_host_size_t element_byte =
        iree_hal_element_dense_byte_count(element_type);
    // SINT_8 and UINT_8 perform direct buffer wrap.
    result = iree_hal_buffer_view_allocate_buffer(
        allocator, shape_rank, shape, element_type,
        IREE_HAL_ENCODING_TYPE_DENSE_ROW_MAJOR,
        (iree_hal_buffer_params_t){
            .type = IREE_HAL_MEMORY_TYPE_DEVICE_LOCAL,
            .access = IREE_HAL_MEMORY_ACCESS_READ,
            .usage = IREE_HAL_BUFFER_USAGE_DISPATCH_STORAGE |
                     IREE_HAL_BUFFER_USAGE_TRANSFER,
        },
        iree_make_const_byte_span(pixel_data, element_byte * buffer_length),
        out_buffer_view);
  }
  stbi_image_free(pixel_data);
  IREE_TRACE_ZONE_END(z0);
  return result;
}

typedef struct iree_tools_utils_buffer_view_load_params_t {
  const uint8_t* pixel_data;
  iree_host_size_t pixel_data_length;
  const float* input_range;
  iree_host_size_t input_range_length;
} iree_tools_utils_buffer_view_load_params_t;
static iree_status_t iree_tools_utils_buffer_view_load_image_rescaled(
    iree_hal_buffer_mapping_t* mapping, void* user_data) {
  iree_tools_utils_buffer_view_load_params_t* params =
      (iree_tools_utils_buffer_view_load_params_t*)user_data;
  return iree_tools_utils_pixel_rescaled_to_buffer(
      params->pixel_data, params->pixel_data_length, params->input_range,
      params->input_range_length, (float*)mapping->contents.data);
}

iree_status_t iree_tools_utils_buffer_view_from_image_rescaled(
    const iree_string_view_t filename, const iree_hal_dim_t* shape,
    iree_host_size_t shape_rank, iree_hal_element_type_t element_type,
    iree_hal_allocator_t* allocator, const float* input_range,
    iree_host_size_t input_range_length,
    iree_hal_buffer_view_t** out_buffer_view) {
  IREE_TRACE_ZONE_BEGIN(z0);
  *out_buffer_view = NULL;
  if (element_type != IREE_HAL_ELEMENT_TYPE_FLOAT_32) {
    IREE_TRACE_ZONE_END(z0);
    return iree_make_status(IREE_STATUS_INVALID_ARGUMENT,
                            "element type should be f32");
  }

  // Classic row-major image layout.
  iree_hal_encoding_type_t encoding_type =
      IREE_HAL_ENCODING_TYPE_DENSE_ROW_MAJOR;

  // Load pixel data from the file into a new host memory allocation (the only
  // interface stb_image provides). A real application would want to use the
  // generation callback to directly decode the image into the target mapped
  // device buffer.
  uint8_t* pixel_data = NULL;
  iree_host_size_t buffer_length = 0;
  IREE_RETURN_AND_END_ZONE_IF_ERROR(
      z0, iree_tools_utils_load_pixel_data(filename, shape, shape_rank,
                                           element_type, &pixel_data,
                                           &buffer_length));

  iree_tools_utils_buffer_view_load_params_t params = {
      .pixel_data = pixel_data,
      .pixel_data_length = buffer_length,
      .input_range = input_range,
      .input_range_length = input_range_length,
  };
  iree_status_t status = iree_hal_buffer_view_generate_buffer(
      allocator, shape_rank, shape, element_type, encoding_type,
      (iree_hal_buffer_params_t){
          .type = IREE_HAL_MEMORY_TYPE_DEVICE_LOCAL |
                  IREE_HAL_MEMORY_TYPE_HOST_VISIBLE,
          .usage = IREE_HAL_BUFFER_USAGE_DISPATCH_STORAGE |
                   IREE_HAL_BUFFER_USAGE_TRANSFER |
                   IREE_HAL_BUFFER_USAGE_MAPPING,
      },
      iree_tools_utils_buffer_view_load_image_rescaled, &params,
      out_buffer_view);

  stbi_image_free(pixel_data);
  IREE_TRACE_ZONE_END(z0);
  return status;
}
