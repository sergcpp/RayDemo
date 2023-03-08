#include "test_common.h"

#include "../Context.h"
#include "../Material.h"

#ifdef USE_GL_RENDER

#if defined(_WIN32)
#include <Windows.h>
#endif

class MaterialTest : public Ren::Context {
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
    MaterialTest() {
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
        wc.lpszClassName = "MaterialTest";

        if (!RegisterClass(&wc)) {
            throw std::runtime_error("Cannot register window class!");
        }

        hWnd = CreateWindow("MaterialTest", "!!", WS_OVERLAPPEDWINDOW |
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

    ~MaterialTest() {
#if defined(_WIN32)
        wglMakeCurrent(NULL, NULL);
        ReleaseDC(hWnd, hDC);
        wglDeleteContext(hRC);
        DestroyWindow(hWnd);
        UnregisterClass("MaterialTest", hInstance);
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
class MaterialTest : public Ren::Context {
public:
    MaterialTest() {
        Ren::Context::Init(256, 256);
    }
};
#endif

static Ren::ProgramRef OnProgramNeeded(const char *name, const char *arg1, const char *arg2) {
    return {};
}

static Ren::Texture2DRef OnTextureNeeded(const char *name) {
    return {};
}

void test_material() {
    {
        // Load material
        MaterialTest test;

        auto on_program_needed = [&test](const char *name, const char *arg1, const char *arg2) {
            Ren::eProgLoadStatus status;
#if defined(USE_GL_RENDER)
            return test.LoadProgramGLSL(name, nullptr, nullptr, &status);
#elif defined(USE_SW_RENDER)
            Ren::Attribute _attrs[] = { {} };
            Ren::Uniform _unifs[] = { {} };
            return test.LoadProgramSW(name, nullptr, nullptr, 0, _attrs, _unifs, &status);
#endif
        };

        auto on_texture_needed = [&test](const char *name) {
            Ren::eTexLoadStatus status;
            Ren::Texture2DParams p;
            return test.LoadTexture2D(name, nullptr, 0, p, &status);
        };

        const char *mat_src = "gl_program: constant constant.vs constant.fs\n"
                              "sw_program: constant\n"
                              "flag: alpha_blend\n"
                              "texture: checker.tga\n"
                              "texture: checker.tga\n"
                              "texture: metal_01.tga\n"
                              "texture: checker.tga\n"
                              "param: 0 1 2 3\n"
                              "param: 0.5 1.2 11 15";

        Ren::eMatLoadStatus status;
        Ren::MaterialRef m_ref = test.LoadMaterial("mat1", nullptr, &status, on_program_needed, on_texture_needed);
        require(status == Ren::MatSetToDefault);

        {
            require(m_ref->ready() == false);
        }

        test.LoadMaterial("mat1", mat_src, &status, on_program_needed, on_texture_needed);

        require(status == Ren::MatCreatedFromData);
        require(m_ref->flags() & Ren::AlphaBlend);
        require(m_ref->ready() == true);
        require(std::string(m_ref->name()) == "mat1");

        Ren::ProgramRef p = m_ref->program();

        require(std::string(p->name()) == "constant");
        require(p->ready() == false);

        Ren::Texture2DRef t0 = m_ref->texture(0);
        Ren::Texture2DRef t1 = m_ref->texture(1);
        Ren::Texture2DRef t2 = m_ref->texture(2);
        Ren::Texture2DRef t3 = m_ref->texture(3);

        require(t0 == t1);
        require(t0 == t3);

        require(std::string(t0->name()) == "checker.tga");
        require(std::string(t1->name()) == "checker.tga");
        require(std::string(t2->name()) == "metal_01.tga");
        require(std::string(t3->name()) == "checker.tga");

        require(m_ref->param(0) == Ren::Vec4f(0, 1, 2, 3));
        require(m_ref->param(1) == Ren::Vec4f(0.5f, 1.2f, 11, 15));
    }
}
