#pragma once

#include <memory>
#include <string>
#include <tuple>
#include <vector>

#include <Ray/Types.h>
#include <Sys/Json.h>

namespace Ray {
class RendererBase;
class SceneBase;
}

namespace Sys {
class ThreadPool;
}

std::shared_ptr<Ray::SceneBase> LoadScene(Ray::RendererBase *r, const JsObject &js_scene, int max_tex_res,
                                          Sys::ThreadPool *threads);

std::tuple<std::vector<float>, std::vector<unsigned>, std::vector<unsigned>> LoadOBJ(const char *file_name);
std::tuple<std::vector<float>, std::vector<unsigned>, std::vector<unsigned>> LoadBIN(const char *file_name);

std::vector<Ray::color_rgba8_t> LoadTGA(const char *name, int &w, int &h);
std::vector<Ray::color_rgba8_t> LoadHDR(const char *name, int &w, int &h);
std::vector<Ray::color_rgba8_t> Load_stb_image(const char *name, int &w, int &h);

void WriteTGA(const Ray::color_rgba_t *data, int pitch, int w, int h, int bpp, bool flip_vertical, const char *name);
void WritePNG(const Ray::color_rgba_t *data, int pitch, int w, int h, int bpp, bool flip_vertical, const char *name);
