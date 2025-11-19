#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
extern "C" __attribute__((naked, section(".entry"))) void _start() {
	__asm__ __volatile__ (
		"sub rsp, 16\n\t"
		"jmp main\n\t"
	);
}
#define GOP_PIXEL_FORMAT_RGBR     0   // PixelRedGreenBlueReserved8BitPerColor
#define GOP_PIXEL_FORMAT_BGRR     1   // PixelBlueGreenRedReserved8BitPerColor
#define GOP_PIXEL_FORMAT_BITMASK  2   // PixelBitMask
#define GOP_PIXEL_FORMAT_BLT_ONLY 3   // PixelBltOnly
struct Ginfo {
    uint64_t width;
    uint64_t height;
    uint64_t pitch;
    uint64_t format;
};
uint64_t get_ginfo(Ginfo* ginfo) {
	uint64_t ret;
    __asm__ __volatile__(
        "int 0x80"
		: "=a"(ret)
        : "a"(0x05), "D"(0), "S"((uint64_t)ginfo)
        : "rcx", "r11", "memory"
    );
	return ret; // return value in rax
}
uint64_t display_frame(void* frame_buffer) {
	uint64_t ret;
    __asm__ __volatile__(
        "int 0x80"
        : "=a"(ret)
        : "a"(0x05), "D"(2), "S"((uint64_t)frame_buffer)
        : "rcx", "r11", "memory"
    );
    return ret; // return value in rax
}
extern "C" void main() {
    Ginfo ginfo;
    get_ginfo(&ginfo);
    printf("Graphics Info: \n");
    printf(" Width:  %d\n", (int)ginfo.width);
    printf(" Height: %d\n", (int)ginfo.height);
    printf(" Pitch:  %d\n", (int)ginfo.pitch);
    printf(" Format: %d\n", (int)ginfo.format);
    uint64_t bytesPerPixel;

    switch (ginfo.format) { // Updated to use ginfo.format instead of ModeInfo->PixelFormat
    case GOP_PIXEL_FORMAT_RGBR:
    case GOP_PIXEL_FORMAT_BGRR:
        bytesPerPixel = 4;
        break;

    case GOP_PIXEL_FORMAT_BITMASK: {
        uint32_t mask = ginfo.format;
        mask = ginfo.width | ginfo.height;
        uint32_t highest = 31;
        while (highest && ((mask >> highest) & 1) == 0)
            highest--;
        bytesPerPixel = ((highest + 1) + 7) / 8;
        break;
    }
    case GOP_PIXEL_FORMAT_BLT_ONLY:
    default:
        bytesPerPixel = 0;
        break;
    }
    volatile uint32_t* fb = (volatile uint32_t*)malloc(ginfo.pitch * ginfo.height * bytesPerPixel);
    printf("Allocated %d bytes via malloc at %p\n", ginfo.pitch * ginfo.height * bytesPerPixel, fb); // Updated to reflect the actual allocated bytes

    if (fb == 0) {
        printf("Memory allocation failed!\n");
        return;
    }
    while (1) {
        for (uint64_t i = 0; i < ginfo.pitch * ginfo.height; i++) {
            fb[i] = 0xFFFFFF; // Fill the framebuffer with base color (assuming 32-bit color)
        }
        int status = display_frame((void*)fb);
        if (status != 0) {
            printf("Display frame failed with status: %d\n", status);
        }
    }
}