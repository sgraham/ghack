// Based on TinyPTC by Gaffer: www.gaffer.org/tinyptc [originally, dead link]

#include "sys.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdlib.h>

static HWND g_wnd;
static char g_bitmapbuffer[sizeof(BITMAPINFO) + 16];
static BITMAPINFO* g_bitmap_header;
static int g_surface_width;
static int g_surface_height;
static unsigned char* g_cached_buffer;
static unsigned int* g_rgba_buffer;

static int g_original_window_width;
static int g_original_window_height;

static HDC g_window_hdc;

#define SC_ZOOM_MSK 0x400
#define SC_ZOOM_1x1 0x401
#define SC_ZOOM_2x2 0x402
#define SC_ZOOM_4x4 0x404

static unsigned int palette[] = {
  0x00000000,
  0xff0000ff,
  0xff00ff00,
  0xff00ffff,
  0xffff0000,
  0xffff00ff,
  0xffffff00,
  0xffffffff,
};

static void MapFrom3BitToRGBA(unsigned char* input_buffer,
                              unsigned int* rgba_buffer) {
  for (int i = 0; i < g_surface_width * g_surface_height; ++i) {
    rgba_buffer[i] = palette[input_buffer[i]];
  }
}

static LRESULT CALLBACK WndProc(HWND hWnd,
                                UINT message,
                                WPARAM wParam,
                                LPARAM lParam) {
  int result = 0;

  switch (message) {
    case WM_PAINT: {
      if (g_cached_buffer != NULL) {
        MapFrom3BitToRGBA(g_cached_buffer, g_rgba_buffer);
        RECT windowsize;
        GetClientRect(hWnd, &windowsize);
        StretchDIBits(g_window_hdc,
                      0,
                      0,
                      windowsize.right,
                      windowsize.bottom,
                      0,
                      0,
                      g_surface_width,
                      g_surface_height,
                      g_rgba_buffer,
                      g_bitmap_header,
                      DIB_RGB_COLORS,
                      SRCCOPY);

        ValidateRect(g_wnd, NULL);
      }
    } break;

    case WM_SYSCOMMAND: {
      if ((wParam & 0xFFFFFFF0) == SC_ZOOM_MSK) {
        int zoom = wParam & 0x7;
        int x =
            (GetSystemMetrics(SM_CXSCREEN) - g_original_window_width * zoom) >>
            1;
        int y =
            (GetSystemMetrics(SM_CYSCREEN) - g_original_window_height * zoom) >>
            1;
        SetWindowPos(hWnd,
                     NULL,
                     x,
                     y,
                     g_original_window_width * zoom,
                     g_original_window_height * zoom,
                     SWP_NOZORDER);
      }
      else
        return DefWindowProc(hWnd, message, wParam, lParam);
    }

    case WM_KEYDOWN: {
      // Close on escape.
      if ((wParam & 0xFF) != 27)
        break;
    }

    case WM_CLOSE: {
      sys_gfx_close();
      ExitProcess(0);
    } break;

    default: {
      result = DefWindowProc(hWnd, message, wParam, lParam);
    }
  }

  return result;
}

int sys_gfx_open(char* title, int width, int height) {
  RECT rect;
  int cc;
  WNDCLASS wc;
  int original_window_x, original_window_y;

  // Register window class.
  wc.style = CS_OWNDC | CS_VREDRAW | CS_HREDRAW;
  wc.lpfnWndProc = WndProc;
  wc.cbClsExtra = 0;
  wc.cbWndExtra = 0;
  wc.hInstance = 0;
  wc.hIcon = NULL;
  wc.hCursor = LoadCursor(0, IDC_ARROW);
  wc.hbrBackground = NULL;
  wc.lpszMenuName = NULL;
  wc.lpszClassName = title;
  RegisterClass(&wc);

  // Calculate window size.
  rect.left = 0;
  rect.top = 0;
  rect.right = width;
  rect.bottom = height;
  AdjustWindowRect(&rect, WS_OVERLAPPEDWINDOW, 0);
  rect.right -= rect.left;
  rect.bottom -= rect.top;

  // Save surface size and original window size.
  g_surface_width = width;
  g_surface_height = height;
  g_original_window_width = rect.right;
  g_original_window_height = rect.bottom;
  g_rgba_buffer = malloc(sizeof(unsigned int) * width  * height);

  // Center window.
  original_window_x = (GetSystemMetrics(SM_CXSCREEN) - rect.right) >> 1;
  original_window_y = (GetSystemMetrics(SM_CYSCREEN) - rect.bottom) >> 1;

  // Create window and show it.
  g_wnd = CreateWindowEx(0,
                         title,
                         title,
                         WS_OVERLAPPEDWINDOW,
                         original_window_x,
                         original_window_y,
                         rect.right,
                         rect.bottom,
                         0,
                         0,
                         0,
                         0);
  ShowWindow(g_wnd, SW_NORMAL);

  // Create bitmap header for StretchDIBits.
  for (cc = 0; cc < sizeof(BITMAPINFOHEADER) + 16; cc++)
    g_bitmapbuffer[cc] = 0;
  g_bitmap_header = (BITMAPINFO*)&g_bitmapbuffer;
  g_bitmap_header->bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
  g_bitmap_header->bmiHeader.biPlanes = 1;
  g_bitmap_header->bmiHeader.biBitCount = 32;
  g_bitmap_header->bmiHeader.biCompression = BI_BITFIELDS;
  g_bitmap_header->bmiHeader.biWidth = g_surface_width;
  g_bitmap_header->bmiHeader.biHeight = -g_surface_height;
  ((unsigned long*)g_bitmap_header->bmiColors)[0] = 0x00FF0000;
  ((unsigned long*)g_bitmap_header->bmiColors)[1] = 0x0000FF00;
  ((unsigned long*)g_bitmap_header->bmiColors)[2] = 0x000000FF;

  g_window_hdc = GetDC(g_wnd);

  {
    // Add entry to system menu to restore original window size.
    HMENU menu = GetSystemMenu(g_wnd, FALSE);
    AppendMenu(menu, MF_STRING, SC_ZOOM_1x1, "Zoom 1 x 1");
    AppendMenu(menu, MF_STRING, SC_ZOOM_2x2, "Zoom 2 x 2");
    AppendMenu(menu, MF_STRING, SC_ZOOM_4x4, "Zoom 4 x 4");
  }

  // Set default zoom to 2x.
  SendMessage(g_wnd, WM_SYSCOMMAND, SC_ZOOM_2x2, 0);

  return 1;
}

int sys_gfx_update(unsigned char* buffer) {
  MSG message;

  g_cached_buffer = buffer;

  InvalidateRect(g_wnd, NULL, TRUE);

  SendMessage(g_wnd, WM_PAINT, 0, 0);

  while (PeekMessage(&message, g_wnd, 0, 0, PM_REMOVE)) {
    TranslateMessage(&message);
    DispatchMessage(&message);
  }

  Sleep(16);

  return 1;
}

void sys_gfx_close(void) {
  ReleaseDC(g_wnd, g_window_hdc);
  DestroyWindow(g_wnd);
}
