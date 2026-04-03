#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <myos>
#include <string.h>
#include <vector>
struct Surface {
    uint32_t* pixels;
	SharedMem mem;
    int width;
    int height;
};
struct Window_info {
    uint64_t id;
    uint64_t width;
    uint64_t height;
    uint64_t x;
    uint64_t y;
	Surface surface;
    bool header;
	bool is_open;
};
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
        uint32_t mask = (uint32_t)ginfo.format;
        mask = (uint32_t)(ginfo.width | ginfo.height);
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
    memset((void*)fb, 0, ginfo.pitch * ginfo.height * bytesPerPixel);
	std::vector<Window_info> windows;
	uint32_t header_size = 30; // 예시로 헤더 높이를 30픽셀로 설정

    int cursor_x = 0;
    int cursor_y = 0;
    while (1) {
        msg_t msg;
        uint64_t result = receive_msg(&msg);
        if (result == 0) {
            switch (msg.type) {
            case MSG_MAKE_WINDOW:
            {
                printf("Received shared memory request: arg=%d\n", (int)msg.payload.params.arg[0]);
                SharedMem mem;
                void* shared_mem_addr = mem.accept(msg.payload.params.arg[0]);
                printf("Received shared memory request: addr=%p, size=%d\n", shared_mem_addr, (int)mem.get_size());
                Window_info win;
                win.id = msg.payload.params.arg[0];
                win.width = unpack_hi(msg.payload.params.arg[2]);
                win.height = unpack_lo(msg.payload.params.arg[2]);
                win.surface.pixels = (uint32_t*)shared_mem_addr;
                win.surface.mem = mem;
                win.surface.width = win.width;
                win.surface.height = win.height;
                win.header = false;
                win.is_open = true;
                win.x = unpack_hi(msg.payload.params.arg[1]);
                win.y = unpack_lo(msg.payload.params.arg[1]);
                windows.push_back(win);
                break;
            }
            case MSG_MOUSE_MOVE:
            {
                cursor_x = (int)msg.payload.params.arg[0];
                cursor_y = (int)msg.payload.params.arg[1];
                // 마우스 커서 위치 업데이트 (예시로 모든 창의 헤더에 마우스 위치 표시)
				//printf("cursor moved to: (%d, %d)\n", cursor_x, cursor_y);
				break;
            }
            default:
                break;
            }
        }
        for (uint64_t i = 0; i < ginfo.pitch * ginfo.height; i++) {
            fb[i] = 0xF0F0F0; // Fill the framebuffer with base color (assuming 32-bit color)
        }
		for (auto& win : windows) { // 일단 이벤트 없이 모든 창을 그려보자
			if (win.is_open) {
                for (int y = 0; y < header_size + win.surface.height + 2; y++) {
                    int fb_x1 = win.x;
                    int fb_x2 = win.x + win.surface.width + 1;
					int fb_y = win.y + y;
                    if (fb_x1 < ginfo.width && fb_y < ginfo.height) {
                        fb[fb_y * ginfo.pitch + fb_x1] = 0xC0C0C0; // Draw header rect (assuming 32-bit color)
                    }
					if (fb_x2 < ginfo.width && fb_y < ginfo.height) {
						fb[fb_y * ginfo.pitch + fb_x2] = 0xC0C0C0; // Draw header rect (assuming 32-bit color)
					}
                }
                for (int x = 0; x < win.surface.width + 1; x++) {
					int fb_x = win.x + x;
					int fb_y1 = win.y;
                    int fb_y2 = win.y + header_size + win.surface.height + 2;
                    if (fb_x < ginfo.width && fb_y1 < ginfo.height) {
                        fb[fb_y1 * ginfo.pitch + fb_x] = 0xC0C0C0; // Draw header rect (assuming 32-bit color)
					}
                    if (fb_x < ginfo.width && fb_y2 < ginfo.height) {
                        fb[fb_y2 * ginfo.pitch + fb_x] = 0xC0C0C0; // Draw header rect (assuming 32-bit color)
                    }
				}
				for (int y = 0; y < win.surface.height; y++) {
					for (int x = 0; x < win.surface.width; x++) {
                        uint32_t pixel = win.surface.pixels[y * win.surface.width + x];
                        int fb_x = win.x + x + 1;
                        int fb_y = win.y + y + 2 + header_size;
                        if (fb_x < ginfo.width && fb_y < ginfo.height) {
                            fb[fb_y * ginfo.pitch + fb_x] = pixel;
                        }
                    }
				}
			}
		}
        for (int y = 0; y < 10; y++) {
            for(int x = 0;x<10;x++) {
                int fb_x = cursor_x + x;
                int fb_y = cursor_y + y;
                if (fb_x < ginfo.width && fb_y < ginfo.height) {
                    fb[fb_y * ginfo.pitch + fb_x] = 0xFF0000; // Draw mouse cursor as red pixel (assuming 32-bit color)
                }
            }

        }
        int status = (int)display_frame((void*)fb);
        if (status != 0) {
            printf("Display frame failed with status: %d\n", status);
        }
    }
}