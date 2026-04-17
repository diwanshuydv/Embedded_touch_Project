#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#define STB_IMAGE_RESIZE_IMPLEMENTATION
#include "stb_image_resize2.h"

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

#define TARGET_W 240
#define TARGET_H 320

int main(int argc, char **argv) {
    if (argc < 2) {
        printf("Usage: %s <image.jpeg>\n", argv[0]);
        return 1;
    }
    
    int width, height, channels;
    unsigned char *img = stbi_load(argv[1], &width, &height, &channels, 3);
    if (!img) {
        printf("Error loading image %s\n", argv[1]);
        return 1;
    }
    
    unsigned char *resized = malloc(TARGET_W * TARGET_H * 3);
    if (!resized) return 1;

    // Use stbir_resize_uint8_linear for stb_image_resize2
    stbir_resize_uint8_linear(img, width, height, 0,
                              resized, TARGET_W, TARGET_H, 0, STBIR_RGB);

    FILE *f = fopen("image_data.h", "w");
    if (!f) {
        printf("Error opening output file\n");
        return 1;
    }
    
    fprintf(f, "#ifndef IMAGE_DATA_H\n#define IMAGE_DATA_H\n\n");
    fprintf(f, "#include <stdint.h>\n\n");
    fprintf(f, "const int demo_image_width = %d;\n", TARGET_W);
    fprintf(f, "const int demo_image_height = %d;\n", TARGET_H);
    fprintf(f, "const uint16_t demo_image_data[%d] = {\n", TARGET_W * TARGET_H);
    
    for (int i = 0; i < TARGET_W * TARGET_H; i++) {
        unsigned char r = resized[i*3 + 0];
        unsigned char g = resized[i*3 + 1];
        unsigned char b = resized[i*3 + 2];
        
        uint16_t r5 = (r * 31) / 255;
        uint16_t g6 = (g * 63) / 255;
        uint16_t b5 = (b * 31) / 255;
        
        uint16_t rgb565 = (r5 << 11) | (g6 << 5) | b5;
        
        fprintf(f, "0x%04X, ", rgb565);
        if ((i + 1) % 12 == 0) {
            fprintf(f, "\n");
        }
    }
    
    fprintf(f, "\n};\n\n#endif // IMAGE_DATA_H\n");
    fclose(f);
    stbi_image_free(img);
    free(resized);
    printf("Successfully generated image_data.h\n");
    return 0;
}
