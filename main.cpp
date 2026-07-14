#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <myos>
#include <string.h>
#include <vector>
#include <ranges>
#include <algorithm>
#include <unistd.h>
struct Surface {
    uint32_t* shm;
    uint32_t* pixels;
    SharedMem mem;
    int width;
    int height;
};
struct Window_info {
    uint64_t id;
    uint64_t width;
    uint64_t height;
    int64_t x;
    int64_t y;
    Surface surface;
    bool header;
    bool border;
    uint32_t header_type;
    bool is_open;
    uint64_t process_id; // 창을 연 프로세스 ID
};
Window_info* getWindow(std::vector<Window_info>& windows, uint64_t id) {
    for (auto& win : windows | std::ranges::views::reverse) {       // 뒤에서부터 탐색 (최상위 창이 먼저)
        if (win.id == id) return &win;
    }
    return nullptr;
}
static const uint8_t cursor_mask[19][12] = {
    {1,0,0,0,0,0,0,0,0,0,0,0},
    {1,1,0,0,0,0,0,0,0,0,0,0},
    {1,2,1,0,0,0,0,0,0,0,0,0},
    {1,2,2,1,0,0,0,0,0,0,0,0},
    {1,2,2,2,1,0,0,0,0,0,0,0},
    {1,2,2,2,2,1,0,0,0,0,0,0},
    {1,2,2,2,2,2,1,0,0,0,0,0},
    {1,2,2,2,2,2,2,1,0,0,0,0},
    {1,2,2,2,2,2,2,2,1,0,0,0},
    {1,2,2,2,2,2,2,2,2,1,0,0},
    {1,2,2,2,2,2,2,2,2,2,1,0},
    {1,2,2,2,2,2,2,1,1,1,1,1},
    {1,2,2,2,1,2,2,1,0,0,0,0},
    {1,2,2,1,0,1,2,2,1,0,0,0},
    {1,2,1,0,0,1,2,2,1,0,0,0},
    {1,1,0,0,0,0,1,2,2,1,0,0},
    {1,0,0,0,0,0,1,2,2,1,0,0},
    {0,0,0,0,0,0,0,1,2,1,0,0},
    {0,0,0,0,0,0,0,0,1,0,0,0},
};
constexpr int CURSOR_W = 12;
constexpr int CURSOR_H = 19;
constexpr uint32_t CURSOR_OUTLINE = 0x000000;
constexpr uint32_t CURSOR_FILL = 0xFFFFFF;
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
    memset((void*)fb, -1, ginfo.pitch * ginfo.height * bytesPerPixel);
    uint64_t* frame_zorder = (uint64_t*)malloc(ginfo.pitch * ginfo.height * sizeof(uint64_t)); // 각 픽셀의 z-order를 저장하는 버퍼 (초기값은 0)
    memset(frame_zorder, -1, ginfo.pitch * ginfo.height * sizeof(uint64_t));
    std::vector<Window_info> windows;
    Window_info gui_win;
    bool has_gui_win = false;
    uint32_t header_size = 30; // 예시로 헤더 높이를 30픽셀로 설정
    struct {
        POINT pos;
        bool grepping;
        uint64_t target_win_id;
        POINT grep_offset;
    } mouse;
    uint32_t BACKGROUND_COLOR = 0xFFFFFFUL; // 흰색 배경
    auto get_header_pixel = [&](uint32_t width, uint32_t height, uint32_t x, uint32_t y, bool is_top) {
        // 간단한 예시: 헤더는 회색으로 채우고, 창이 활성화된 경우에는 파란색으로 테두리를 그립니다.
        uint32_t header_color = 0xCCCCCC; // 회색
        if (is_top) header_color = 0xEEEEEE; // 밝은 회색 (활성화된 창)
        uint32_t border_color = 0x000000; // 파란색
        if (x == 0 || x == width + 1 || y == 0 || y == height + 1) {
            return border_color; // 테두리
        }
        return header_color; // 헤더 내부
        };
    std::vector<RECT> dirty_rects;
    auto rezorder = [&]() {
        memset(frame_zorder, -1, ginfo.pitch * ginfo.height * sizeof(uint64_t)); // z-order 초기화
        if (has_gui_win) {
            int wx = gui_win.x, wy = gui_win.y;
            int ww = gui_win.surface.width, wh = gui_win.surface.height;
            int border_px = gui_win.border ? 1 : 0;
            for (int y = 0; y < (gui_win.header ? header_size : 0) + wh + 2 * border_px; y++) {
                for (int x = 0; x < ww + 2 * border_px; x++) {
                    int fb_x = wx + x, fb_y = wy + y;
                    if (fb_x < ginfo.width && fb_y < ginfo.height) {
                        int cx = x - border_px, cy = y - (gui_win.header ? header_size + border_px : border_px);
                        if (cx >= 0 && cx < ww && cy >= 0 && cy < wh && gui_win.surface.pixels && (gui_win.surface.pixels[cy * ww + cx] & PIXEL_TRANSPARENT)) continue;
                        frame_zorder[fb_y * ginfo.pitch + fb_x] = gui_win.id;
                    }
                }
            }
        }
        for (auto& win : windows) {
            if (!win.is_open) continue;

            int wx = win.x, wy = win.y;
            int ww = win.surface.width, wh = win.surface.height;

            int border_px = win.border ? 1 : 0;
            for (int y = 0; y < (win.header ? header_size : 0) + wh + 2 * border_px; y++) {
                for (int x = 0; x < ww + 2 * border_px; x++) {
                    int fb_x = wx + x;
                    int fb_y = wy + y;
                    if (fb_x < ginfo.width && fb_y < ginfo.height) {
                        int cx = x - border_px, cy = y - (win.header ? header_size + border_px : border_px);
                        if (cx >= 0 && cx < ww && cy >= 0 && cy < wh && win.surface.pixels && (win.surface.pixels[cy * ww + cx] & PIXEL_TRANSPARENT)) continue;
                        frame_zorder[fb_y * ginfo.pitch + fb_x] = win.id;
                    }
                }
            }
        }
        };
    auto zorder_update = [&](Window_info& win) {
        int wx = win.x, wy = win.y;
        int ww = win.surface.width, wh = win.surface.height;
        int border_px = win.border ? 1 : 0;
        for (int y = 0; y < (win.header ? header_size : 0) + wh + 2 * border_px; y++) {
            for (int x = 0; x < ww + 2 * border_px; x++) {
                int fb_x = wx + x;
                int fb_y = wy + y;
                if (fb_x < ginfo.width && fb_y < ginfo.height) {
                    int cx = x - border_px, cy = y - (win.header ? header_size + border_px : border_px);
                    if (cx >= 0 && cx < ww && cy >= 0 && cy < wh && win.surface.pixels && (win.surface.pixels[cy * ww + cx] & PIXEL_TRANSPARENT)) continue;
                    frame_zorder[fb_y * ginfo.pitch + fb_x] = win.id;
                }
            }
        }
        };
    auto render_dirty = [&](RECT dirty) {
        if (dirty.x < 0) {
            dirty.width += dirty.x; // Reduce width by the negative offset
            dirty.x = 0; // Start from the left edge of the screen
        }
        if (dirty.y < 0) {
            dirty.height += dirty.y; // Reduce height by the negative offset
            dirty.y = 0; // Start from the top edge of the screen
        }
        if (dirty.x >= ginfo.width || dirty.y >= ginfo.height) {
            return; // 완전히 화면 밖에 있는 경우 렌더링할 필요 없음
        }
        if (dirty.x + dirty.width > ginfo.width) {
            dirty.width = ginfo.width - dirty.x; // 화면 오른쪽 경계에 맞게 너비 조정
        }
        if (dirty.y + dirty.height > ginfo.height) {
            dirty.height = ginfo.height - dirty.y; // 화면 아래쪽 경계에 맞게 높이 조정
        }
        for (int y = dirty.y; y < dirty.y + dirty.height; y++) {
            for (int x = dirty.x; x < dirty.x + dirty.width; x++) {
                if (x < 0 || y < 0) continue; // 음수 좌표는 무시
                if (x >= ginfo.width || y >= ginfo.height) continue;

                uint64_t win_id = frame_zorder[y * ginfo.pitch + x];

                if (win_id == (uint64_t)-1) {
                    // 아무 창도 없는 곳 → 배경색
                    fb[y * ginfo.pitch + x] = BACKGROUND_COLOR;
                    continue;
                }
                Window_info* win = getWindow(windows, win_id);
                if (!win && has_gui_win && gui_win.id == win_id) win = &gui_win;
                if (!win || !win->is_open) {
                    // 예외 처리: 해당 ID의 창이 없을 때 (예: 창이 닫혔는데 z-order가 아직 업데이트 안 된 경우)
					rezorder(); // z-order를 다시 계산
                    continue;
                }
                // 헤더 영역인지 콘텐츠 영역인지 판별
                int local_y = y - win->y;
                int local_x = x - win->x;

                if (win->header && local_y < header_size + 1) {
                    // 헤더 → WM이 직접 색 계산
                    if (win->header_type) {
                        fb[y * ginfo.pitch + x] = get_header_pixel(win->surface.width, header_size, local_x, local_y, win_id == windows.back().id);
                    }
                }
                else {
                    int bpx = win->border ? 1 : 0;
                    int buf_x = local_x - bpx;
                    int buf_y = local_y - (win->header ? header_size + bpx : bpx);
                    if (buf_x < 0 || buf_x >= win->surface.width ||
                        buf_y < 0 || buf_y >= win->surface.height) {
                        if (win->border) fb[y * ginfo.pitch + x] = 0x0; // 테두리 색
                        continue;
                    }
                    fb[y * ginfo.pitch + x] = win->surface.pixels[buf_y * win->surface.width + buf_x];
                }
            }
        }
        // syscall flush
        };
    if (fork() == 0) {
        execv("@/mygui.o", nullptr);
    }
    while (1) {
        msg_t msg;
        wait_for_msg();
        uint64_t result = receive_msg(&msg);
        while (result == 0) {
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
                win.surface.shm = (uint32_t*)shared_mem_addr;
                win.surface.pixels = (uint32_t*)malloc(mem.get_size());
                memcpy(win.surface.pixels, win.surface.shm, mem.get_size());
                win.surface.mem = mem;
                win.surface.width = win.width;
                win.surface.height = win.height;
                win.header_type = 1;
                win.is_open = true;
                win.x = unpack_hi(msg.payload.params.arg[1]);
                win.y = unpack_lo(msg.payload.params.arg[1]);
                win.process_id = msg.sender_pid;
                uint64_t style = unpack_lo(msg.payload.params.arg[3]);
                uint64_t exstyle = unpack_hi(msg.payload.params.arg[3]);
                win.header = !(style & WINDOW_NOHEADER);
                win.border = !(style & WINDOW_NOBORDER);
                if ((exstyle & WINDOW_EX_GUI) && !has_gui_win) {
                    gui_win = win;
                    has_gui_win = true;
                    rezorder();
                    dirty_rects.push_back({ win.x, win.y, win.width + (win.border ? 2 : 0), win.height + (win.header ? header_size : 0) + (win.border ? 2 : 0) });
                    msg_t response{ .sender_pid = 0, .type = MSG_MAKE_WINDOW, .status = 0, .payload{ {win.id} }, .timestamp = 0 };
                    send_msg(msg.sender_pid, &response, false);
                    break;
                }
                if (!windows.empty()) {
                    Window_info before_win = windows.back();
                    dirty_rects.push_back({ before_win.x, before_win.y, before_win.width + (before_win.border ? 2 : 0), before_win.height + (before_win.header ? header_size : 0) + (before_win.border ? 2 : 0) }); // 창 전체를 더티 영역으로 추가
                }
                windows.push_back(win);
                dirty_rects.push_back({ win.x, win.y, win.width + (win.border ? 2 : 0), win.height + (win.header ? header_size : 0) + (win.border ? 2 : 0) }); // 창 전체를 더티 영역으로 추가
                msg_t response{
                    .sender_pid = 0,
                    .type = MSG_MAKE_WINDOW,
                    .status = 0,
                    .payload{ {win.id} },
                    .timestamp = 0
                };
                int result = send_msg(msg.sender_pid, &response, false);
                printf("Code result=%d\n", (int)result);
                rezorder();
                break;
            }
            case MSG_DESTROY_WINDOW:
            {
                uint64_t wid = msg.payload.params.arg[0];
                for (size_t i = 0; i < windows.size(); i++) {
                    if (windows[i].id == wid || windows[i].process_id == msg.sender_pid) {
                        Window_info& w = windows[i];
                        dirty_rects.push_back({ w.x, w.y, w.width + (w.border ? 2 : 0), w.height + (w.header ? header_size : 0) + (w.border ? 2 : 0) });
                        windows.erase(i);
                        break;
                    }
                }
                rezorder();
                break;
            }
            case MSG_DRAW_FRAME:
            {
                int32_t x = unpack_hi(msg.payload.params.arg[1]), y = unpack_lo(msg.payload.params.arg[1]);
                uint32_t width = unpack_hi(msg.payload.params.arg[2]), height = unpack_lo(msg.payload.params.arg[2]);
                if (has_gui_win && gui_win.id == msg.payload.params.arg[0] && msg.sender_pid == gui_win.process_id) {
                    int32_t gx = x, gy = y;
                    uint32_t gw = width, gh = height;
                    if (gx + gw > gui_win.surface.width) gw = gui_win.surface.width - gx;
                    if (gy + gh > gui_win.surface.height) gh = gui_win.surface.height - gy;
                    for (int j = 0; j < (int)gh; j++) {
                        memcpy(&gui_win.surface.pixels[(j + gy) * gui_win.surface.width + gx],
                            &gui_win.surface.shm[(j + gy) * gui_win.surface.width + gx],
                            gw * sizeof(int32_t));
                    }
                    dirty_rects.push_back({ gui_win.x + (int)gx, gui_win.y + (int)gy, gw, gh });
                    break;
                }
                for (auto& win : windows) {
                    if (win.id == msg.payload.params.arg[0]) {
                        if (msg.sender_pid != win.process_id) break; // 창을 연 프로세스만 해당 창을 업데이트할 수 있도록 허용
                        if (x < 0) {
                            width += x; // Reduce width by the negative offset
                            x = 0; // Start from the left edge of the window
                        }
                        if (y < 0) {
                            height += y; // Reduce height by the negative offset
                            y = 0; // Start from the top edge of the window
                        }
                        if (x >= win.surface.width || y >= win.surface.height || width <= 0 || height <= 0) {
                            break;
                        }
                        if (x + width > win.surface.width) width = win.surface.width - x;
                        if (y + height > win.surface.height) height = win.surface.height - y;
                        for (int j = 0; j < height; j++) {
                            uint32_t* src = &win.surface.shm[(j + y) * win.surface.width + x];
                            uint32_t* dst = &win.surface.pixels[(j + y) * win.surface.width + x];
                            memcpy(dst, src, width * sizeof(uint32_t));
                        }
                        dirty_rects.push_back({ win.x + (win.border ? 1 : 0) + x, win.y + (win.header ? header_size : 0) + (win.border ? 1 : 0) + y, width, height }); // 업데이트된 영역을 더티 영역으로 추가
                    }
                }
                break;
            }
            case MSG_REZORDER:
            {
                rezorder();
				break;
            }
            case MSG_MOUSE_MOVE:
            {
                mouse.pos.x = (int)msg.payload.params.arg[0];
                mouse.pos.y = (int)msg.payload.params.arg[1];
                break;
            }
            case MSG_MOUSE_LCLICK:
            {
                if (mouse.grepping) break; // 이미 창 이동 모드인 경우 추가 클릭 무시
                int click_x = (int)msg.payload.params.arg[0];
                int click_y = (int)msg.payload.params.arg[1];
                // 클릭 위치에 있는 창을 최상위로 올리기
                for (auto it = windows.rbegin(); it != windows.rend(); ++it) {
                    Window_info win = *it;
                    if (!win.is_open) continue;
                    if (frame_zorder[click_y * ginfo.pitch + click_x] == win.id) {
                        if (win.header && click_y < win.y + header_size) {
                            // 헤더 클릭 → 창 이동 모드로 진입
                            mouse.grepping = true;
                            mouse.target_win_id = win.id;
                            mouse.grep_offset.x = click_x - win.x;
                            mouse.grep_offset.y = click_y - win.y;
                        }
                        if (it == windows.rbegin()) break;
                        // 해당 창이 클릭된 경우
                        Window_info prev_top = windows.back();
                        dirty_rects.push_back({ prev_top.x, prev_top.y,
                            prev_top.width + 2, (uint64_t)header_size + 1 });
                        std::rotate(&*it, (&*it) + 1, windows.end()); // 클릭된 창을 최상위로 이동
                        zorder_update(win); // z-order 업데이트
                        dirty_rects.push_back({ win.x, win.y, win.width + (win.border ? 2 : 0), win.height + (win.header ? header_size : 0) + (win.border ? 2 : 0) }); // 창 전체를 더티 영역으로 추가
                        break;
                    }
                }
                if (has_gui_win && frame_zorder[click_y * ginfo.pitch + click_x] == gui_win.id) {
                    msg_t click_msg{ .sender_pid = 0, .type = MSG_MOUSE_LCLICK, .status = 0,
                        .payload{ {(uint64_t)click_x, (uint64_t)click_y, 0} }, .timestamp = 0 };
                    send_msg(gui_win.process_id, &click_msg, false);
                }
                break;
            }
            case MSG_MOUSE_LRELEASE:
            {
                mouse.grepping = false;
                break;
            }
            case MSG_KEY_PRESS:
            {
                uint32_t keycode = (uint32_t)msg.payload.params.arg[0];
                msg_t key_msg{
                    .sender_pid = 0,
                    .type = MSG_KEY_PRESS,
                    .status = 0,
                    .payload{ {keycode,windows.back().id,0}},
                    .timestamp = 0
                };
                if (windows.size() > 0) {
                    send_msg(windows.back().process_id, &key_msg, false); // 최상위 창의 프로세스에 키 이벤트 전달
                }
                else if (has_gui_win) {
                    send_msg(gui_win.process_id, &key_msg, false);
                }
                break;
            }
            case MSG_KEY_RELEASE:
            {
                uint32_t keycode = (uint32_t)msg.payload.params.arg[0];
                msg_t key_msg{
                    .sender_pid = 0,
                    .type = MSG_KEY_RELEASE,
                    .status = 0,
                    .payload{ {keycode,windows.back().id,0}},
                    .timestamp = 0
                };
                if (windows.size() > 0) {
                    send_msg(windows.back().process_id, &key_msg, false);
                }
                else if (has_gui_win) {
                    send_msg(gui_win.process_id, &key_msg, false);
                }
                break;
            }
            default:
                break;
            }
            result = receive_msg(&msg);
        }
        if (mouse.grepping) {
            for (auto& win : windows | std::ranges::views::reverse) {
                if (win.id == mouse.target_win_id) {
                    int new_x = mouse.pos.x - mouse.grep_offset.x;
                    int new_y = mouse.pos.y - mouse.grep_offset.y;
                    if (win.x != new_x || win.y != new_y) {
                        dirty_rects.push_back({ win.x, win.y, win.width + (win.border ? 2 : 0), win.height + (win.header ? header_size : 0) + (win.border ? 2 : 0) }); // 이전 위치를 더티 영역으로 추가
                        win.x = new_x;
                        win.y = new_y;
                        rezorder(); // z-order 업데이트
                        dirty_rects.push_back({ win.x, win.y, win.width + (win.border ? 2 : 0), win.height + (win.header ? header_size : 0) + (win.border ? 2 : 0) }); // 새 위치를 더티 영역으로 추가
                    }
                }
            }
        }
        if (dirty_rects.size() > 50) {
            render_dirty({ 0, 0, ginfo.width, ginfo.height }); // 너무 많은 더티 영역이 쌓이면 전체 렌더링
        }
        else {
            for (auto& rect : dirty_rects) {
                render_dirty(rect);
            }
        }
        dirty_rects.clear();
        for (int cy = 0; cy < CURSOR_H; cy++) {
            for (int cx = 0; cx < CURSOR_W; cx++) {
                int fb_x = mouse.pos.x + cx;
                int fb_y = mouse.pos.y + cy;
                if (fb_x >= ginfo.width || fb_y >= ginfo.height) continue;

                uint8_t v = cursor_mask[cy][cx];
                if (v == 0) continue;  // 투명
                fb[fb_y * ginfo.pitch + fb_x] = (v == 1) ? CURSOR_OUTLINE : CURSOR_FILL;
            }
        }
        dirty_rects.push_back({ mouse.pos.x, mouse.pos.y, CURSOR_W, CURSOR_H });
        int status = (int)display_frame((void*)fb);
        if (status != 0) {
            printf("Display frame failed with status: %d\n", status);
        }
    }
}