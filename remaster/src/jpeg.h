#pragma once

// A JPEG file's bytes to RGB pixels (8 bits each, rows top down), or null; free with jpeg_free.
#ifdef __cplusplus
extern "C" {
#endif
unsigned char* jpeg_decode(const unsigned char* data, int size, int* width, int* height);
void jpeg_free(unsigned char* pixels);
#ifdef __cplusplus
}
#endif
