// JPEG pictures for the HD art (raylib's own image loader is built without JPEG): stb_image, its
// JPEG decoder only, kept to this file (static) so it never meets the copy inside raylib.
#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_STATIC
#define STBI_ONLY_JPEG
#define STBI_NO_STDIO
#include "third_party/stb_image.h"

#include "jpeg.h"

unsigned char* jpeg_decode(const unsigned char* data, int size, int* width, int* height) {
    int channels = 0;
    return stbi_load_from_memory(data, size, width, height, &channels, 3);
}

void jpeg_free(unsigned char* pixels) { stbi_image_free(pixels); }
