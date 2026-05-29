#include <assert.h>
#include <bare.h>
#include <js.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_STDIO
#include "stb_image.h"

// ICO file format structures
typedef struct __attribute__((packed)) {
  uint16_t reserved; // Must be 0
  uint16_t type;     // 1 for ICO, 2 for CUR
  uint16_t count;    // Number of images
} IconDir;

typedef struct __attribute__((packed)) {
  uint8_t width;         // Width (0 = 256)
  uint8_t height;        // Height (0 = 256)
  uint8_t color_count;   // Number of colors (0 if >= 8bpp)
  uint8_t reserved;      // Must be 0
  uint16_t planes;       // Color planes
  uint16_t bit_count;    // Bits per pixel
  uint32_t bytes_in_res; // Size of image data
  uint32_t image_offset; // Offset to image data
} IconDirEntry;

static void
bare_ico__on_finalize(js_env_t *env, void *data, void *finalize_hint) {
  free(data);
}

/**
 * Decode ICO file to RGBA format
 * Extracts largest image or closest to requested size
 *
 * Arguments:
 * - buffer: ICO data
 * - options: { size? }
 *
 * Returns: {width, height, data, sizes: [available sizes]}
 */
static js_value_t *
bare_ico_decode(js_env_t *env, js_callback_info_t *info) {
  int err;

  size_t argc = 2;
  js_value_t *argv[2];

  err = js_get_callback_info(env, info, &argc, argv, NULL, NULL);
  assert(err == 0);
  assert(argc >= 1);

  // Get ICO buffer
  uint8_t *ico_data;
  size_t ico_len;
  err = js_get_typedarray_info(env, argv[0], NULL, (void **) &ico_data, &ico_len, NULL, NULL);
  assert(err == 0);

  if (ico_len < sizeof(IconDir)) {
    err = js_throw_error(env, NULL, "Invalid ICO file: too small");
    assert(err == 0);
    return NULL;
  }

  // Parse ICO header
  IconDir *header = (IconDir *) ico_data;
  if (header->reserved != 0 || header->type != 1) {
    err = js_throw_error(env, NULL, "Invalid ICO file: bad header");
    assert(err == 0);
    return NULL;
  }

  uint16_t count = header->count;
  if (count == 0 || ico_len < sizeof(IconDir) + count * sizeof(IconDirEntry)) {
    err = js_throw_error(env, NULL, "Invalid ICO file: bad entry count");
    assert(err == 0);
    return NULL;
  }

  // Parse requested size
  int requested_size = 0;
  if (argc >= 2) {
    bool has_size;
    err = js_has_named_property(env, argv[1], "size", &has_size);
    assert(err == 0);

    if (has_size) {
      js_value_t *size_val;
      err = js_get_named_property(env, argv[1], "size", &size_val);
      assert(err == 0);

      int64_t s;
      err = js_get_value_int64(env, size_val, &s);
      assert(err == 0);
      requested_size = (int) s;
    }
  }

  // Find best matching image
  IconDirEntry *entries = (IconDirEntry *) (ico_data + sizeof(IconDir));
  int best_idx = 0;
  int best_size = entries[0].width == 0 ? 256 : entries[0].width;

  if (requested_size > 0) {
    // Find closest size to requested
    int best_diff = abs(best_size - requested_size);
    for (int i = 1; i < count; i++) {
      int w = entries[i].width == 0 ? 256 : entries[i].width;
      int diff = abs(w - requested_size);
      if (diff < best_diff) {
        best_diff = diff;
        best_size = w;
        best_idx = i;
      }
    }
  } else {
    // Find largest image
    for (int i = 1; i < count; i++) {
      int w = entries[i].width == 0 ? 256 : entries[i].width;
      if (w > best_size) {
        best_size = w;
        best_idx = i;
      }
    }
  }

  // Extract image data
  IconDirEntry *entry = &entries[best_idx];
  uint32_t offset = entry->image_offset;
  uint32_t size = entry->bytes_in_res;

  // Check bounds without overflowing uint32_t
  if (offset > ico_len || size > (uint32_t) (ico_len - offset)) {
    err = js_throw_error(env, NULL, "Invalid ICO file: image data out of bounds");
    assert(err == 0);
    return NULL;
  }

  uint8_t *img_data = ico_data + offset;

  // Check if PNG (magic: 0x89 0x50 0x4E 0x47)
  bool is_png = (size >= 8 && img_data[0] == 0x89 && img_data[1] == 0x50 && img_data[2] == 0x4E && img_data[3] == 0x47);

  int width, height, channels;
  uint8_t *rgba_data = NULL;

  if (is_png) {
    // PNG format - can decode directly
    rgba_data = stbi_load_from_memory(
      img_data, size, &width, &height, &channels, 4
    );
  } else {
    // BMP-in-ICO format: has DIB header but no BITMAPFILEHEADER
    // Height is doubled (includes AND mask)
    // We need to decode the BMP ourselves

    if (size < 40) {
      err = js_throw_error(env, NULL, "Invalid BMP-in-ICO: too small");
      assert(err == 0);
      return NULL;
    }

    // Read BMP DIB header
    uint32_t header_size;
    int32_t bmp_width, bmp_height;
    uint16_t bmp_planes, bmp_bpp;
    uint32_t bmp_compression;

    memcpy(&header_size, img_data + 0, 4);
    memcpy(&bmp_width, img_data + 4, 4);
    memcpy(&bmp_height, img_data + 8, 4);
    memcpy(&bmp_planes, img_data + 12, 2);
    memcpy(&bmp_bpp, img_data + 14, 2);
    memcpy(&bmp_compression, img_data + 16, 4);

    // Validate. Cap dimensions to keep all downstream size math within int32.
    // 65535 is well above any realistic ICO image and prevents overflow in
    // row_size / pixel_data_size / rgba_size calculations below.
    if (header_size < 40 || bmp_width <= 0 || bmp_height <= 0 || bmp_width > 65535 || bmp_height > 131070) {
      err = js_throw_error(env, NULL, "Invalid BMP-in-ICO header");
      assert(err == 0);
      return NULL;
    }

    // ICO BMP height is doubled (image data + AND mask)
    int32_t actual_height = bmp_height / 2;

    // Compute sizes in 64-bit to detect any wrap before we trust them.
    int64_t row_size_64 = (((int64_t) bmp_width * bmp_bpp + 31) / 32) * 4;
    int64_t pixel_data_size_64 = row_size_64 * actual_height;
    int64_t rgba_size_64 = (int64_t) bmp_width * actual_height * 4;

    // Palette size (only present for <=8bpp)
    int palette_size = (bmp_bpp <= 8) ? (1 << bmp_bpp) * 4 : 0;

    // Verify palette + pixel data lie entirely within the input image.
    if ((int64_t) header_size + palette_size > (int64_t) size || pixel_data_size_64 > (int64_t) size - header_size - palette_size) {
      err = js_throw_error(env, NULL, "Invalid BMP-in-ICO: pixel data out of bounds");
      assert(err == 0);

      return NULL;
    }

    int row_size = (int) row_size_64;

    // Allocate output RGBA buffer
    width = bmp_width;
    height = actual_height;
    rgba_data = (uint8_t *) malloc((size_t) rgba_size_64);
    if (!rgba_data) {
      err = js_throw_error(env, NULL, "Memory allocation failed");
      assert(err == 0);

      return NULL;
    }

    // Pixel data starts after header and (optional) color palette
    uint8_t *pixel_start = img_data + header_size + palette_size;

    // Decode based on bit depth
    if (bmp_bpp == 32 && bmp_compression == 0) {
      // 32-bit RGBA - direct copy with BGR→RGB conversion
      for (int y = 0; y < actual_height; y++) {
        uint8_t *src = pixel_start + ((actual_height - 1 - y) * row_size);
        uint8_t *dst = rgba_data + (y * width * 4);
        for (int x = 0; x < width; x++) {
          dst[x * 4 + 0] = src[x * 4 + 2]; // R
          dst[x * 4 + 1] = src[x * 4 + 1]; // G
          dst[x * 4 + 2] = src[x * 4 + 0]; // B
          dst[x * 4 + 3] = src[x * 4 + 3]; // A
        }
      }
    } else if (bmp_bpp == 24 && bmp_compression == 0) {
      // 24-bit RGB - convert to RGBA
      for (int y = 0; y < actual_height; y++) {
        uint8_t *src = pixel_start + ((actual_height - 1 - y) * row_size);
        uint8_t *dst = rgba_data + (y * width * 4);
        for (int x = 0; x < width; x++) {
          dst[x * 4 + 0] = src[x * 3 + 2]; // R
          dst[x * 4 + 1] = src[x * 3 + 1]; // G
          dst[x * 4 + 2] = src[x * 3 + 0]; // B
          dst[x * 4 + 3] = 255;            // A
        }
      }
    } else if (bmp_bpp == 8 && bmp_compression == 0) {
      // 8-bit indexed color - use palette
      uint8_t *palette = img_data + header_size;
      for (int y = 0; y < actual_height; y++) {
        uint8_t *src = pixel_start + ((actual_height - 1 - y) * row_size);
        uint8_t *dst = rgba_data + (y * width * 4);
        for (int x = 0; x < width; x++) {
          uint8_t index = src[x];
          dst[x * 4 + 0] = palette[index * 4 + 2]; // R
          dst[x * 4 + 1] = palette[index * 4 + 1]; // G
          dst[x * 4 + 2] = palette[index * 4 + 0]; // B
          dst[x * 4 + 3] = 255;                    // A
        }
      }
    } else if (bmp_bpp == 4 && bmp_compression == 0) {
      // 4-bit indexed color - use palette
      uint8_t *palette = img_data + header_size;
      for (int y = 0; y < actual_height; y++) {
        uint8_t *src = pixel_start + ((actual_height - 1 - y) * row_size);
        uint8_t *dst = rgba_data + (y * width * 4);
        for (int x = 0; x < width; x++) {
          uint8_t byte = src[x / 2];
          uint8_t index;
          if (x % 2 == 0) {
            index = (byte >> 4) & 0x0F;
          } else {
            index = byte & 0x0F;
          }
          dst[x * 4 + 0] = palette[index * 4 + 2]; // R
          dst[x * 4 + 1] = palette[index * 4 + 1]; // G
          dst[x * 4 + 2] = palette[index * 4 + 0]; // B
          dst[x * 4 + 3] = 255;                    // A (opaque)
        }
      }
    } else {
      // Unsupported format
      free(rgba_data);
      char msg[100];
      snprintf(msg, sizeof(msg), "Unsupported BMP format: %d-bit, compression=%d", bmp_bpp, bmp_compression);
      err = js_throw_error(env, NULL, msg);
      assert(err == 0);
      return NULL;
    }
  }

  if (!rgba_data) {
    err = js_throw_error(env, NULL, "Failed to decode ICO image");
    assert(err == 0);
    return NULL;
  }

  // Create result object
  js_value_t *result;
  err = js_create_object(env, &result);
  assert(err == 0);

  // Set width
  js_value_t *width_val;
  err = js_create_int64(env, width, &width_val);
  assert(err == 0);
  err = js_set_named_property(env, result, "width", width_val);
  assert(err == 0);

  // Set height
  js_value_t *height_val;
  err = js_create_int64(env, height, &height_val);
  assert(err == 0);
  err = js_set_named_property(env, result, "height", height_val);
  assert(err == 0);

  // Set data. Use size_t arithmetic to avoid int overflow on the PNG path
  // where width/height come from stb_image and are not pre-bounded here.
  if (width <= 0 || height <= 0) {
    stbi_image_free(rgba_data);
    err = js_throw_error(env, NULL, "Invalid decoded image dimensions");
    assert(err == 0);
    return NULL;
  }
  size_t data_len = (size_t) width * (size_t) height * 4;
  uint8_t *result_data = malloc(data_len);
  if (!result_data) {
    stbi_image_free(rgba_data);
    err = js_throw_error(env, NULL, "Memory allocation failed");
    assert(err == 0);
    return NULL;
  }

  memcpy(result_data, rgba_data, data_len);
  stbi_image_free(rgba_data);

  js_value_t *buffer;
  err = js_create_external_arraybuffer(
    env, result_data, data_len, bare_ico__on_finalize, NULL, &buffer
  );
  assert(err == 0);
  err = js_set_named_property(env, result, "data", buffer);
  assert(err == 0);

  // Set available sizes array
  js_value_t *sizes_array;
  err = js_create_array_with_length(env, count, &sizes_array);
  assert(err == 0);

  for (int i = 0; i < count; i++) {
    int w = entries[i].width == 0 ? 256 : entries[i].width;
    js_value_t *size_val;
    err = js_create_int64(env, w, &size_val);
    assert(err == 0);
    err = js_set_element(env, sizes_array, i, size_val);
    assert(err == 0);
  }

  err = js_set_named_property(env, result, "sizes", sizes_array);
  assert(err == 0);

  return result;
}

static js_value_t *
bare_ico_exports(js_env_t *env, js_value_t *exports) {
  int err;

#define V(name, fn) \
  { \
    js_value_t *val; \
    err = js_create_function(env, name, -1, fn, NULL, &val); \
    assert(err == 0); \
    err = js_set_named_property(env, exports, name, val); \
    assert(err == 0); \
  }

  V("decode", bare_ico_decode)
#undef V

  return exports;
}

BARE_MODULE(bare_ico, bare_ico_exports)
