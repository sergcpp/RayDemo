#include "test_common.h"

#include "membuf.h"

#include "../Context.h"
#include "../Buffer.h"
#include "../Utils.h"

#ifdef USE_GL_RENDER

#if defined(_WIN32)
#include <Windows.h>
#endif

class BufferTest : public Ren::Context {
#if defined(_WIN32)
    HINSTANCE hInstance;
    HWND hWnd;
    HDC hDC;
    HGLRC hRC;
#else
    SDL_Window *window_;
    void *gl_ctx_;
#endif
public:
    BufferTest() {
#if defined(_WIN32)
        hInstance = GetModuleHandle(NULL);
        WNDCLASS wc;
        wc.style = CS_OWNDC;
        wc.lpfnWndProc = ::DefWindowProc;
        wc.cbClsExtra = 0;
        wc.cbWndExtra = 0;
        wc.hInstance = hInstance;
        wc.hIcon = LoadIcon(NULL, IDI_WINLOGO);
        wc.hCursor = LoadCursor(NULL, IDC_ARROW);
        wc.hbrBackground = NULL;
        wc.lpszMenuName = NULL;
        wc.lpszClassName = "BufferTest";

        if (!RegisterClass(&wc)) {
            throw std::runtime_error("Cannot register window class!");
        }

        hWnd = CreateWindow("BufferTest", "!!", WS_OVERLAPPEDWINDOW |
                            WS_CLIPSIBLINGS | WS_CLIPCHILDREN,
                            0, 0, 100, 100, NULL, NULL, hInstance, NULL);

        if (hWnd == NULL) {
            throw std::runtime_error("Cannot create window!");
        }

        hDC = GetDC(hWnd);

        PIXELFORMATDESCRIPTOR pfd;
        memset(&pfd, 0, sizeof(pfd));
        pfd.nSize = sizeof(pfd);
        pfd.nVersion = 1;
        pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL;
        pfd.iPixelType = PFD_TYPE_RGBA;
        pfd.cColorBits = 32;

        int pf = ChoosePixelFormat(hDC, &pfd);
        if (pf == 0) {
            throw std::runtime_error("Cannot find pixel format!");
        }

        if (SetPixelFormat(hDC, pf, &pfd) == FALSE) {
            throw std::runtime_error("Cannot set pixel format!");
        }

        DescribePixelFormat(hDC, pf, sizeof(PIXELFORMATDESCRIPTOR), &pfd);

        hRC = wglCreateContext(hDC);
        wglMakeCurrent(hDC, hRC);
#else
        SDL_Init(SDL_INIT_VIDEO);

        window_ = SDL_CreateWindow("View", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, 256, 256, SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN);
        gl_ctx_ = SDL_GL_CreateContext(window_);
#endif
        Context::Init(256, 256);
    }

    ~BufferTest() {
#if defined(_WIN32)
        wglMakeCurrent(NULL, NULL);
        ReleaseDC(hWnd, hDC);
        wglDeleteContext(hRC);
        DestroyWindow(hWnd);
        UnregisterClass("BufferTest", hInstance);
#else
        SDL_GL_DeleteContext(gl_ctx_);
        SDL_DestroyWindow(window_);
#ifndef EMSCRIPTEN
        SDL_Quit();
#endif
#endif
    }
};
#else
#include "../SW/SW.h"
class BufferTest : public Ren::Context {
public:
    BufferTest() {
        Ren::Context::Init(256, 256);
    }
};
#endif

void test_buffer() {

    {
        BufferTest test;

        auto buf = Ren::Buffer{ 256 };

        require(buf.Alloc(16) == 0);
        require(buf.Alloc(32) == 16);
        require(buf.Alloc(64) == 16 + 32);
        require(buf.Alloc(16) == 16 + 32 + 64);

        buf.Free(0);

        require(buf.Alloc(16) == 0);
    }
}
