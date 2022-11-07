#include "test_common.h"

#include "../Context.h"
#include "../Texture.h"

#ifdef USE_GL_RENDER

#if defined(_WIN32)
#include <Windows.h>
#endif

class TextureTest : public Ren::Context {
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
    TextureTest() {
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
        wc.lpszClassName = "TextureTest";

        if (!RegisterClass(&wc)) {
            throw std::runtime_error("Cannot register window class!");
        }

        hWnd = CreateWindow("TextureTest", "!!", WS_OVERLAPPEDWINDOW |
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

    ~TextureTest() {
#if defined(_WIN32)
        wglMakeCurrent(NULL, NULL);
        ReleaseDC(hWnd, hDC);
        wglDeleteContext(hRC);
        DestroyWindow(hWnd);
        UnregisterClass("TextureTest", hInstance);
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

class TextureTest : public Ren::Context {
public:
    TextureTest() {
        Ren::Context::Init(1, 1);
    }
};
#endif

static unsigned char test_tga_img[] = "\x00\x00\x02\x00\x00\x00\x00\x00\x00\x00\x00\x00\x02\x00\x02\x00" \
                                      "\x18\x00\x00\x00\x00\xFF\xFF\xFF\xFF\xFF\xFF\x00\x00\x00\x00\x00" \
                                      "\x00\x00\x00\x00\x00\x00\x54\x52\x55\x45\x56\x49\x53\x49\x4F\x4E" \
                                      "\x2D\x58\x46\x49\x4C\x45\x2E\x00";

void test_texture() {
    {
        // TGA load
        TextureTest test;

        Ren::eTexLoadStatus status;
        Ren::Texture2DParams p;
        Ren::Texture2DRef t_ref = test.LoadTexture2D("checker.tga", nullptr, 0, p, &status);
        require(status == Ren::TexCreatedDefault);

        require(std::string(t_ref->name()) == "checker.tga");
        require(t_ref->params().w == 1);
        require(t_ref->params().h == 1);
        require(t_ref->params().format == Ren::RawRGB888);
        require(!t_ref->ready());

        {
            Ren::Texture2DRef t_ref2 = test.LoadTexture2D("checker.tga", nullptr, 0, p, &status);
            require(status == Ren::TexFound);
            require(!t_ref2->ready());
        }

        {
            Ren::Texture2DRef t_ref3 = test.LoadTexture2D("checker.tga", test_tga_img, (int)sizeof(test_tga_img), p, &status);
            require(status == Ren::TexCreatedFromData);
            require(t_ref3->params().w == 2);
            require(t_ref3->params().h == 2);
            require(t_ref3->params().format == Ren::RawRGB888);
            require(t_ref3->ready());
        }
    }
}
