#include "Load.h"

#include <cassert>
#include <cctype>

#include <algorithm>
#include <fstream>
#include <iomanip>
#include <limits>
#include <map>
#include <sstream>

#include <Ren/MMat.h>
#include <Ren/SmallVector.h>
#include <Ren/Texture.h>
#include <Ren/Utils.h>
#include <Sys/AssetFile.h>
#include <Sys/Log.h>
#include <Sys/Time_.h>

#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_PKM
#include <Ren/SOIL2/stb_image.h>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <Ren/SOIL2/stb_image_write.h>

#include <turbojpeg.h>

#define _abs(x) ((x) > 0.0f ? (x) : -(x))

#define DUMP_BIN_FILES 1

std::shared_ptr<Ray::SceneBase> LoadScene(Ray::RendererBase *r, const JsObject &js_scene, const int max_tex_res) {
    auto new_scene = std::shared_ptr<Ray::SceneBase>(r->CreateScene());

    std::vector<Ray::CameraHandle> cameras;
    std::map<std::string, Ray::TextureHandle> textures;
    std::map<std::string, Ray::MaterialHandle> materials;
    std::map<std::string, Ray::MeshHandle> meshes;

    auto jpg_decompressor = std::unique_ptr<void, int (*)(tjhandle)>(tjInitDecompress(), &tjDestroy);

    auto ends_with = [](const std::string &value, const std::string &ending) -> bool {
        if (ending.size() > value.size())
            return false;
        return std::equal(ending.rbegin(), ending.rend(), value.rbegin());
    };

    auto get_texture = [&](const std::string &name, const bool srgb, const bool normalmap, const bool gen_mipmaps) {
        auto it = textures.find(name);
        if (it == textures.end()) {
            int w, h, channels;
            uint8_t *img_data = nullptr;
            bool force_no_compression = false;
            if (ends_with(name, ".hdr")) {
                const std::vector<Ray::color_rgba8_t> temp = LoadHDR(name, w, h);

                channels = 4;
                img_data = (uint8_t *)STBI_MALLOC(w * h * 4);
                force_no_compression = true;

                memcpy(img_data, &temp[0].v[0], w * h * sizeof(Ray::color_rgba8_t));
            } else {
                int channel_to_extract = -1;
                std::string _name = name;
                if (ends_with(_name, "@red")) {
                    channel_to_extract = 0;
                    _name.resize(_name.size() - 4);
                } else if (ends_with(_name, "@green")) {
                    channel_to_extract = 1;
                    _name.resize(_name.size() - 6);
                } else if (ends_with(_name, "@blue")) {
                    channel_to_extract = 2;
                    _name.resize(_name.size() - 5);
                }

                if (ends_with(_name, ".jpg") || ends_with(_name, ".jpeg") || ends_with(_name, ".JPG") ||
                    ends_with(_name, ".JPEG")) {
                    std::ifstream in_file(_name, std::ios::binary | std::ios::ate);
                    const auto in_file_size = (unsigned long)(in_file.tellg());
                    in_file.seekg(0, std::ios::beg);

                    std::unique_ptr<uint8_t[]> in_file_buf(new uint8_t[in_file_size]);
                    in_file.read((char *)&in_file_buf[0], in_file_size);

                    const int res =
                        tjDecompressHeader((tjhandle)jpg_decompressor.get(), &in_file_buf[0], in_file_size, &w, &h);
                    if (res == 0) {
                        img_data = (uint8_t *)STBI_MALLOC(w * h * 3);
                        const int res2 = tjDecompress((tjhandle)jpg_decompressor.get(), &in_file_buf[0], in_file_size,
                                                      img_data, w, 0, h, 3, TJXOP_VFLIP);
                        if (res2 == 0) {
                            channels = 3;
                        } else {
                            stbi_image_free(img_data);
                            img_data = nullptr;
                            fprintf(stderr, "tjDecompress error %i\n", res2);
                            return Ray::InvalidTextureHandle;
                        }
                    } else {
                        fprintf(stderr, "tjDecompressHeader error %i\n", res);
                        return Ray::InvalidTextureHandle;
                    }
                } else {
                    stbi_set_flip_vertically_on_load(1);
                    img_data = stbi_load(_name.c_str(), &w, &h, &channels, 0);
                }

                if (channel_to_extract == -1) {
                    // Try to detect single channel texture
                    bool is_grey = true, is_1px_texture = true;
                    for (int i = 0; i < w * h && (is_grey || is_1px_texture); ++i) {
                        for (int j = 1; j < channels; ++j) {
                            is_grey &= (img_data[i * channels + 0] == img_data[i * channels + j]);
                        }
                        for (int j = 0; j < channels; ++j) {
                            is_1px_texture &= (img_data[j] == img_data[i * channels + j]);
                        }
                    }

                    if (is_1px_texture) {
                        w = h = 1;
                    }

                    if (is_grey) {
                        // Use only red channel
                        channel_to_extract = 0;
                    }
                }

                if (channel_to_extract != -1) {
                    for (int i = 0; i < w * h; ++i) {
                        for (int j = 0; j < channels; ++j) {
                            img_data[i + j] = img_data[i * channels + channel_to_extract];
                        }
                    }
                    channels = 1;
                }
            }
            if (!img_data) {
                throw std::runtime_error("Cannot load image!");
            }

            while (max_tex_res != -1 && (w > max_tex_res || h > max_tex_res)) {
                const int new_w = (w / 2), new_h = (h / 2);
                auto new_img_data = (uint8_t *)STBI_MALLOC(new_w * new_h * channels);

                for (int y = 0; y < h - 1; y += 2) {
                    for (int x = 0; x < w - 1; x += 2) {
                        for (int k = 0; k < channels; ++k) {
                            const uint8_t c00 = img_data[channels * ((y + 0) * w + (x + 0)) + k];
                            const uint8_t c01 = img_data[channels * ((y + 0) * w + (x + 1)) + k];
                            const uint8_t c10 = img_data[channels * ((y + 1) * w + (x + 0)) + k];
                            const uint8_t c11 = img_data[channels * ((y + 1) * w + (x + 1)) + k];

                            new_img_data[channels * ((y / 2) * (w / 2) + (x / 2)) + k] = (c00 + c01 + c10 + c11) / 4;
                        }
                    }
                }

                stbi_image_free(img_data);
                img_data = new_img_data;
                w = new_w;
                h = new_h;
            }

            Ray::tex_desc_t tex_desc;
            if (channels == 4) {
                tex_desc.format = Ray::eTextureFormat::RGBA8888;
            } else if (channels == 3) {
                tex_desc.format = Ray::eTextureFormat::RGB888;
            } else if (channels == 2) {
                tex_desc.format = Ray::eTextureFormat::RG88;
            } else if (channels == 1) {
                tex_desc.format = Ray::eTextureFormat::R8;
            }
            tex_desc.name = name.c_str();
            tex_desc.data = &img_data[0];
            tex_desc.w = w;
            tex_desc.h = h;
            tex_desc.is_srgb = srgb;
            tex_desc.is_normalmap = normalmap;
            tex_desc.force_no_compression = force_no_compression;
            tex_desc.generate_mipmaps = gen_mipmaps;

            const Ray::TextureHandle tex_handle = new_scene->AddTexture(tex_desc);
            textures[name] = tex_handle;

            stbi_image_free(img_data);

            return tex_handle;
        } else {
            return it->second;
        }
    };

    auto parse_transform = [](const JsObject &obj) {
        Ren::Mat4f transform;

        if (obj.Has("pos")) {
            const JsArray &js_pos = obj.at("pos").as_arr();

            const auto px = float(js_pos.at(0).as_num().val);
            const auto py = float(js_pos.at(1).as_num().val);
            const auto pz = float(js_pos.at(2).as_num().val);

            transform = Ren::Translate(transform, {px, py, pz});
        }

        int rotation_order[] = {2, 0, 1};
        if (obj.Has("rot_order")) {
            const JsString &js_rot_order = obj.at("rot_order").as_str();
            if (js_rot_order.val == "xyz") {
                rotation_order[0] = 0;
                rotation_order[1] = 1;
                rotation_order[2] = 2;
            } else if (js_rot_order.val == "xzy") {
                rotation_order[0] = 0;
                rotation_order[1] = 2;
                rotation_order[2] = 1;
            } else if (js_rot_order.val == "yxz") {
                rotation_order[0] = 1;
                rotation_order[1] = 0;
                rotation_order[2] = 2;
            } else if (js_rot_order.val == "yzx") {
                rotation_order[0] = 1;
                rotation_order[1] = 2;
                rotation_order[2] = 0;
            } else if (js_rot_order.val == "zyx") {
                rotation_order[0] = 2;
                rotation_order[1] = 1;
                rotation_order[2] = 0;
            }
        }

        if (obj.Has("rot")) {
            const JsArray &js_rot = obj.at("rot").as_arr();

            const float rot[3] = {float(js_rot.at(0).as_num().val) * Ren::Pi<float>() / 180.0f,
                                  float(js_rot.at(1).as_num().val) * Ren::Pi<float>() / 180.0f,
                                  float(js_rot.at(2).as_num().val) * Ren::Pi<float>() / 180.0f};

            static const Ren::Vec3f axes[] = {Ren::Vec3f{1.0f, 0.0f, 0.0f}, Ren::Vec3f{0.0f, 1.0f, 0.0f},
                                              Ren::Vec3f{0.0f, 0.0f, 1.0f}};

            for (const int i : rotation_order) {
                transform = Ren::Rotate(transform, rot[i], axes[i]);
            }
        }

        if (obj.Has("scale")) {
            const JsArray &js_scale = obj.at("scale").as_arr();

            const auto sx = float(js_scale.at(0).as_num().val);
            const auto sy = float(js_scale.at(1).as_num().val);
            const auto sz = float(js_scale.at(2).as_num().val);

            transform = Ren::Scale(transform, {sx, sy, sz});
        }

        return transform;
    };

    try {
        struct {
            bool use_fast_bvh_build = false;
        } global_settings;

        if (js_scene.Has("settings")) {
            const JsObject &js_settings = js_scene.at("settings").as_obj();
            if (js_settings.Has("use_fast_bvh_build")) {
                const JsLiteral use_fast = js_settings.at("use_fast_bvh_build").as_lit();
                global_settings.use_fast_bvh_build = (use_fast.val == JsLiteralType::True);
            }
        }
        if (js_scene.Has("cameras")) {
            const JsArray &js_cams = js_scene.at("cameras").as_arr();

            for (const auto &js_cam_el : js_cams.elements) {
                const JsObject &js_cam = js_cam_el.as_obj();

                Ray::camera_desc_t cam_desc;
                cam_desc.clamp = true;

                bool view_targeted = false;
                Ren::Vec3f view_origin, view_dir = {0, 0, -1}, view_up, view_target;

                if (js_cam.Has("view_origin")) {
                    const JsArray &js_view_origin = js_cam.at("view_origin").as_arr();

                    view_origin[0] = float(js_view_origin.at(0).as_num().val);
                    view_origin[1] = float(js_view_origin.at(1).as_num().val);
                    view_origin[2] = float(js_view_origin.at(2).as_num().val);
                }

                if (js_cam.Has("view_dir")) {
                    const JsArray &js_view_dir = js_cam.at("view_dir").as_arr();

                    view_dir[0] = float(js_view_dir.at(0).as_num().val);
                    view_dir[1] = float(js_view_dir.at(1).as_num().val);
                    view_dir[2] = float(js_view_dir.at(2).as_num().val);
                } else if (js_cam.Has("view_rot")) {
                    const JsArray &js_view_rot = js_cam.at("view_rot").as_arr();

                    auto rx = float(js_view_rot.at(0).as_num().val);
                    auto ry = float(js_view_rot.at(1).as_num().val);
                    auto rz = float(js_view_rot.at(2).as_num().val);

                    rx *= Ren::Pi<float>() / 180.0f;
                    ry *= Ren::Pi<float>() / 180.0f;
                    rz *= Ren::Pi<float>() / 180.0f;

                    Ren::Mat4f transform;
                    transform = Ren::Rotate(transform, float(rz), Ren::Vec3f{0.0f, 0.0f, 1.0f});
                    transform = Ren::Rotate(transform, float(rx), Ren::Vec3f{1.0f, 0.0f, 0.0f});
                    transform = Ren::Rotate(transform, float(ry), Ren::Vec3f{0.0f, 1.0f, 0.0f});

                    Ren::Vec4f view_vec = {0.0f, -1.0f, 0.0f, 0.0f};
                    view_vec = transform * view_vec;

                    memcpy(&view_dir[0], Ren::ValuePtr(view_vec), 3 * sizeof(float));

                    Ren::Vec4f view_up_vec = {0.0f, 0.0f, -1.0f, 0.0f};
                    view_up_vec = transform * view_up_vec;

                    memcpy(&view_up[0], Ren::ValuePtr(view_up_vec), 3 * sizeof(float));
                }

                if (js_cam.Has("view_up")) {
                    const JsArray &js_view_up = js_cam.at("view_up").as_arr();

                    view_up[0] = float(js_view_up.at(0).as_num().val);
                    view_up[1] = float(js_view_up.at(1).as_num().val);
                    view_up[2] = float(js_view_up.at(2).as_num().val);
                }

                if (js_cam.Has("view_target")) {
                    const JsArray &js_view_target = js_cam.at("view_target").as_arr();

                    view_target[0] = float(js_view_target.at(0).as_num().val);
                    view_target[1] = float(js_view_target.at(1).as_num().val);
                    view_target[2] = float(js_view_target.at(2).as_num().val);

                    view_targeted = true;
                }

                if (js_cam.Has("shift")) {
                    const JsArray &js_shift = js_cam.at("shift").as_arr();

                    cam_desc.shift[0] = float(js_shift.at(0).as_num().val);
                    cam_desc.shift[1] = float(js_shift.at(1).as_num().val);
                }

                if (js_cam.Has("fov")) {
                    const JsNumber &js_view_fov = js_cam.at("fov").as_num();
                    cam_desc.fov = float(js_view_fov.val);
                }

                if (js_cam.Has("fstop")) {
                    const JsNumber &js_fstop = js_cam.at("fstop").as_num();
                    cam_desc.fstop = float(js_fstop.val);
                }

                if (js_cam.Has("focus_distance")) {
                    const JsNumber &js_focus_distance = js_cam.at("focus_distance").as_num();
                    cam_desc.focus_distance = float(js_focus_distance.val);
                } else {
                    cam_desc.focus_distance = 0.4f;
                }

                if (js_cam.Has("sensor_height")) {
                    const JsNumber &js_sensor_height = js_cam.at("sensor_height").as_num();
                    cam_desc.sensor_height = float(js_sensor_height.val);
                }

                if (js_cam.Has("exposure")) {
                    const JsNumber &js_exposure = js_cam.at("exposure").as_num();
                    cam_desc.exposure = float(js_exposure.val);
                }

                if (js_cam.Has("gamma")) {
                    const JsNumber &js_gamma = js_cam.at("gamma").as_num();
                    cam_desc.gamma = float(js_gamma.val);
                }

                if (js_cam.Has("lens_rotation")) {
                    const JsNumber &js_lens_rotation = js_cam.at("lens_rotation").as_num();
                    cam_desc.lens_rotation = float(js_lens_rotation.val);
                }

                if (js_cam.Has("lens_ratio")) {
                    const JsNumber &js_lens_ratio = js_cam.at("lens_ratio").as_num();
                    cam_desc.lens_ratio = float(js_lens_ratio.val);
                }

                if (js_cam.Has("lens_blades")) {
                    const JsNumber &js_lens_blades = js_cam.at("lens_blades").as_num();
                    cam_desc.lens_blades = int(js_lens_blades.val);
                }

                if (js_cam.Has("clip_start")) {
                    const JsNumber &js_clip_start = js_cam.at("clip_start").as_num();
                    cam_desc.clip_start = float(js_clip_start.val);
                }

                if (js_cam.Has("clip_end")) {
                    const JsNumber &js_clip_end = js_cam.at("clip_end").as_num();
                    cam_desc.clip_end = float(js_clip_end.val);
                }

                if (js_cam.Has("filter")) {
                    const JsString &js_filter = js_cam.at("filter").as_str();
                    if (js_filter.val == "box") {
                        cam_desc.filter = Ray::Box;
                    } else if (js_filter.val == "tent") {
                        cam_desc.filter = Ray::Tent;
                    } else {
                        throw std::runtime_error("Unknown filter parameter!");
                    }
                }

                if (js_cam.Has("srgb")) {
                    const JsLiteral &js_srgb = js_cam.at("srgb").as_lit();
                    cam_desc.dtype = (js_srgb.val == JsLiteralType::True) ? Ray::SRGB : Ray::None;
                }

                if (view_targeted) {
                    Ren::Vec3f dir = view_origin - view_target;
                    view_dir = Normalize(-dir);
                }

                memcpy(&cam_desc.origin[0], ValuePtr(view_origin), 3 * sizeof(float));
                memcpy(&cam_desc.fwd[0], ValuePtr(view_dir), 3 * sizeof(float));
                memcpy(&cam_desc.up[0], ValuePtr(view_up), 3 * sizeof(float));

                cameras.emplace_back(new_scene->AddCamera(cam_desc));
            }

            if (js_scene.Has("current_cam")) {
                const JsNumber &js_current_cam = js_scene.at("current_cam").as_num();
                new_scene->set_current_cam(cameras[int(js_current_cam.val)]);
            }
        }

        if (js_scene.Has("environment")) {
            const JsObject &js_env = js_scene.at("environment").as_obj();

            {
                Ray::environment_desc_t env_desc;
                env_desc.env_col[0] = env_desc.env_col[1] = env_desc.env_col[2] = 0.0f;

                if (js_env.Has("env_col")) {
                    const JsArray &js_env_col = js_env.at("env_col").as_arr();
                    env_desc.env_col[0] = float(js_env_col.at(0).as_num().val);
                    env_desc.env_col[1] = float(js_env_col.at(1).as_num().val);
                    env_desc.env_col[2] = float(js_env_col.at(2).as_num().val);
                }

                if (js_env.Has("env_map")) {
                    const JsString &js_env_map = js_env.at("env_map").as_str();
                    env_desc.env_map = get_texture(js_env_map.val, false /* srgb */, false /* normalmap */, false);
                }

                if (js_env.Has("env_map_rot")) {
                    const JsNumber &js_env_map_rot = js_env.at("env_map_rot").as_num();
                    env_desc.env_map_rotation = float(js_env_map_rot.val) * Ren::Pi<float>() / 180.0f;
                }

                if (js_env.Has("back_col")) {
                    const JsArray &js_back_col = js_env.at("back_col").as_arr();
                    env_desc.back_col[0] = float(js_back_col.at(0).as_num().val);
                    env_desc.back_col[1] = float(js_back_col.at(1).as_num().val);
                    env_desc.back_col[2] = float(js_back_col.at(2).as_num().val);
                } else {
                    memcpy(env_desc.back_col, env_desc.env_col, 3 * sizeof(float));
                }

                if (js_env.Has("back_map")) {
                    const JsString &js_back_map = js_env.at("back_map").as_str();
                    env_desc.back_map = get_texture(js_back_map.val, false /* srgb */, false /* normalmap */, false);
                }

                if (js_env.Has("back_map_rot")) {
                    const JsNumber &js_back_map_rot = js_env.at("back_map_rot").as_num();
                    env_desc.back_map_rotation = float(js_back_map_rot.val) * Ren::Pi<float>() / 180.0f;
                }

                if (js_env.Has("multiple_importance")) {
                    const JsLiteral &js_mult_imp = js_env.at("multiple_importance").as_lit();
                    env_desc.multiple_importance = (js_mult_imp.val == JsLiteralType::True);
                }

                new_scene->SetEnvironment(env_desc);
            }

            if ((js_env.Has("sun_dir") || js_env.Has("sun_rot")) && js_env.Has("sun_col")) {
                Ray::directional_light_desc_t sun_desc;

                if (js_env.Has("sun_dir")) {
                    const JsArray &js_sun_dir = js_env.at("sun_dir").as_arr();
                    sun_desc.direction[0] = float(js_sun_dir.at(0).as_num().val);
                    sun_desc.direction[1] = float(js_sun_dir.at(1).as_num().val);
                    sun_desc.direction[2] = float(js_sun_dir.at(2).as_num().val);
                } else if (js_env.Has("sun_rot")) {
                    const JsArray &js_sun_rot = js_env.at("sun_rot").as_arr();

                    const float rot[3] = {float(js_sun_rot.at(0).as_num().val) * Ren::Pi<float>() / 180.0f,
                                          float(js_sun_rot.at(1).as_num().val) * Ren::Pi<float>() / 180.0f,
                                          float(js_sun_rot.at(2).as_num().val) * Ren::Pi<float>() / 180.0f};

                    Ren::Mat4f transform;
                    transform = Ren::Rotate(transform, rot[2], Ren::Vec3f{0.0f, 0.0f, 1.0f});
                    transform = Ren::Rotate(transform, rot[0], Ren::Vec3f{1.0f, 0.0f, 0.0f});
                    transform = Ren::Rotate(transform, rot[1], Ren::Vec3f{0.0f, 1.0f, 0.0f});

                    const Ren::Vec4f sun_dir = Normalize(transform * Ren::Vec4f{0.0f, -1.0f, 0.0f, 0.0f});
                    sun_desc.direction[0] = sun_dir[0];
                    sun_desc.direction[1] = sun_dir[1];
                    sun_desc.direction[2] = sun_dir[2];
                }

                const JsArray &js_sun_col = js_env.at("sun_col").as_arr();

                sun_desc.color[0] = float(js_sun_col.at(0).as_num().val);
                sun_desc.color[1] = float(js_sun_col.at(1).as_num().val);
                sun_desc.color[2] = float(js_sun_col.at(2).as_num().val);

                if (js_env.Has("sun_strength")) {
                    const JsNumber &js_sun_strength = js_env.at("sun_strength").as_num();

                    sun_desc.color[0] *= float(js_sun_strength.val);
                    sun_desc.color[1] *= float(js_sun_strength.val);
                    sun_desc.color[2] *= float(js_sun_strength.val);
                }

                sun_desc.angle = 0.0f;
                if (js_env.Has("sun_angle")) {
                    const JsNumber &js_sun_softness = js_env.at("sun_angle").as_num();
                    sun_desc.angle = float(js_sun_softness.val);
                }

                if ((sun_desc.direction[0] != 0.0f || sun_desc.direction[1] != 0.0f || sun_desc.direction[2] != 0.0f) &&
                    sun_desc.color[0] != 0.0f && sun_desc.color[1] != 0.0f && sun_desc.color[2] != 0.0f) {
                    new_scene->AddLight(sun_desc);
                }
            }
        }

        const JsObject &js_materials = js_scene.at("materials").as_obj();
        for (const auto &js_mat : js_materials.elements) {
            const std::string &js_mat_name = js_mat.first;
            const JsObject &js_mat_obj = js_mat.second.as_obj();
            const JsString &js_type = js_mat_obj.at("type").as_str();
            if (js_type.val == "principled") {
                Ray::principled_mat_desc_t mat_desc;

                if (js_mat_obj.Has("base_color")) {
                    const JsArray &js_main_color = js_mat_obj.at("base_color").as_arr();
                    mat_desc.base_color[0] = float(js_main_color.at(0).as_num().val);
                    mat_desc.base_color[1] = float(js_main_color.at(1).as_num().val);
                    mat_desc.base_color[2] = float(js_main_color.at(2).as_num().val);
                }

                if (js_mat_obj.Has("base_texture")) {
                    const JsString &js_base_tex = js_mat_obj.at("base_texture").as_str();
                    mat_desc.base_texture =
                        get_texture(js_base_tex.val, true /* srgb */, false /* normalmap */, true /* mips */);
                }

                if (js_mat_obj.Has("specular")) {
                    const JsNumber &js_specular = js_mat_obj.at("specular").as_num();
                    mat_desc.specular = float(js_specular.val);
                }

                if (js_mat_obj.Has("specular_texture")) {
                    const JsString &js_specular_tex = js_mat_obj.at("specular_texture").as_str();
                    mat_desc.specular_texture =
                        get_texture(js_specular_tex.val, true /* srgb */, false /* normalmap */, true /* mips */);
                }

                if (js_mat_obj.Has("specular_tint")) {
                    const JsNumber &js_specular_tint = js_mat_obj.at("specular_tint").as_num();
                    mat_desc.specular_tint = float(js_specular_tint.val);
                }

                if (js_mat_obj.Has("metallic")) {
                    const JsNumber &js_metallic = js_mat_obj.at("metallic").as_num();
                    mat_desc.metallic = float(js_metallic.val);
                }

                if (js_mat_obj.Has("metallic_texture")) {
                    const JsString &js_metallic_tex = js_mat_obj.at("metallic_texture").as_str();
                    mat_desc.metallic_texture =
                        get_texture(js_metallic_tex.val, false /* srgb */, false /* normalmap */, true /* mips */);
                }

                if (js_mat_obj.Has("normal_map")) {
                    const JsString &js_normal_map = js_mat_obj.at("normal_map").as_str();
                    mat_desc.normal_map =
                        get_texture(js_normal_map.val, false /* srgb */, true /* normalmap */, false /* mips */);
                }

                if (js_mat_obj.Has("normal_map_intensity")) {
                    const JsNumber &js_normal_map_intensity = js_mat_obj.at("normal_map_intensity").as_num();
                    mat_desc.normal_map_intensity = float(js_normal_map_intensity.val);
                }

                if (js_mat_obj.Has("roughness")) {
                    const JsNumber &js_roughness = js_mat_obj.at("roughness").as_num();
                    mat_desc.roughness = float(js_roughness.val);
                }

                if (js_mat_obj.Has("roughness_texture")) {
                    const JsString &js_roughness_tex = js_mat_obj.at("roughness_texture").as_str();
                    const bool is_srgb = js_mat_obj.Has("roughness_texture_srgb") &&
                                         js_mat_obj.at("roughness_texture_srgb").as_lit().val == JsLiteralType::True;
                    mat_desc.roughness_texture =
                        get_texture(js_roughness_tex.val, is_srgb, false /* normalmap */, true /* mips */);
                }

                if (js_mat_obj.Has("anisotropic")) {
                    const JsNumber &js_anisotropic = js_mat_obj.at("anisotropic").as_num();
                    mat_desc.anisotropic = float(js_anisotropic.val);
                }

                if (js_mat_obj.Has("anisotropic_rotation")) {
                    const JsNumber &js_anisotropic_rotation = js_mat_obj.at("anisotropic_rotation").as_num();
                    mat_desc.anisotropic_rotation = float(js_anisotropic_rotation.val);
                }

                if (js_mat_obj.Has("sheen")) {
                    const JsNumber &js_sheen = js_mat_obj.at("sheen").as_num();
                    mat_desc.sheen = float(js_sheen.val);
                }

                if (js_mat_obj.Has("sheen_tint")) {
                    const JsNumber &js_sheen_tint = js_mat_obj.at("sheen_tint").as_num();
                    mat_desc.sheen_tint = float(js_sheen_tint.val);
                }

                if (js_mat_obj.Has("clearcoat")) {
                    const JsNumber &js_clearcoat = js_mat_obj.at("clearcoat").as_num();
                    mat_desc.clearcoat = float(js_clearcoat.val);
                }

                if (js_mat_obj.Has("clearcoat_roughness")) {
                    const JsNumber &js_clearcoat_roughness = js_mat_obj.at("clearcoat_roughness").as_num();
                    mat_desc.clearcoat_roughness = float(js_clearcoat_roughness.val);
                }

                if (js_mat_obj.Has("ior")) {
                    const JsNumber &js_ior = js_mat_obj.at("ior").as_num();
                    mat_desc.ior = float(js_ior.val);
                }

                if (js_mat_obj.Has("transmission")) {
                    const JsNumber &js_transmission = js_mat_obj.at("transmission").as_num();
                    mat_desc.transmission = float(js_transmission.val);
                }

                if (js_mat_obj.Has("transmission_roughness")) {
                    const JsNumber &js_transmission_roughness = js_mat_obj.at("transmission_roughness").as_num();
                    mat_desc.transmission_roughness = float(js_transmission_roughness.val);
                }

                if (js_mat_obj.Has("emission_color")) {
                    const JsArray &js_emission_color = js_mat_obj.at("emission_color").as_arr();
                    mat_desc.emission_color[0] = float(js_emission_color.at(0).as_num().val);
                    mat_desc.emission_color[1] = float(js_emission_color.at(1).as_num().val);
                    mat_desc.emission_color[2] = float(js_emission_color.at(2).as_num().val);
                }

                if (js_mat_obj.Has("emission_texture")) {
                    const JsString &js_emission_tex = js_mat_obj.at("emission_texture").as_str();
                    mat_desc.emission_texture =
                        get_texture(js_emission_tex.val, true /* srgb */, false /* normalmap */, true /* mips */);
                }

                if (js_mat_obj.Has("emission_strength")) {
                    const JsNumber &js_emission_strength = js_mat_obj.at("emission_strength").as_num();
                    mat_desc.emission_strength = float(js_emission_strength.val);
                }

                if (js_mat_obj.Has("alpha")) {
                    const JsNumber &js_alpha = js_mat_obj.at("alpha").as_num();
                    mat_desc.alpha = float(js_alpha.val);
                }

                if (js_mat_obj.Has("alpha_texture")) {
                    const JsString &js_alpha_tex = js_mat_obj.at("alpha_texture").as_str();
                    mat_desc.alpha_texture =
                        get_texture(js_alpha_tex.val, false /* srgb */, false /* normalmap */, false /* mips */);
                }

                materials[js_mat_name] = new_scene->AddMaterial(mat_desc);
            } else {
                Ray::shading_node_desc_t node_desc;

                if (js_mat_obj.Has("base_texture")) {
                    const JsString &js_base_tex = js_mat_obj.at("base_texture").as_str();
                    node_desc.base_texture =
                        get_texture(js_base_tex.val, js_type.val != "mix", false /* normalmap */, js_type.val != "mix");
                }

                if (js_mat_obj.Has("base_color")) {
                    const JsArray &js_main_color = js_mat_obj.at("base_color").as_arr();
                    node_desc.base_color[0] = float(js_main_color.at(0).as_num().val);
                    node_desc.base_color[1] = float(js_main_color.at(1).as_num().val);
                    node_desc.base_color[2] = float(js_main_color.at(2).as_num().val);
                }

                if (js_mat_obj.Has("normal_map")) {
                    const JsString &js_normal_map = js_mat_obj.at("normal_map").as_str();
                    node_desc.normal_map =
                        get_texture(js_normal_map.val, false /* srgb */, true /* normalmap */, false);
                }

                if (js_mat_obj.Has("roughness")) {
                    const JsNumber &js_roughness = js_mat_obj.at("roughness").as_num();
                    node_desc.roughness = float(js_roughness.val);
                }

                if (js_mat_obj.Has("strength")) {
                    const JsNumber &js_strength = js_mat_obj.at("strength").as_num();
                    node_desc.strength = float(js_strength.val);
                }

                if (js_mat_obj.Has("multiple_importance")) {
                    const JsLiteral &js_mult_imp = js_mat_obj.at("multiple_importance").as_lit();
                    node_desc.multiple_importance = (js_mult_imp.val == JsLiteralType::True);
                }

                if (js_mat_obj.Has("fresnel")) {
                    const JsNumber &js_fresnel = js_mat_obj.at("fresnel").as_num();
                    node_desc.fresnel = float(js_fresnel.val);
                }

                if (js_mat_obj.Has("ior")) {
                    const JsNumber &js_ior = js_mat_obj.at("ior").as_num();
                    node_desc.ior = float(js_ior.val);
                }

                if (js_type.val == "diffuse") {
                    node_desc.type = Ray::DiffuseNode;
                } else if (js_type.val == "glossy") {
                    node_desc.type = Ray::GlossyNode;
                } else if (js_type.val == "refractive") {
                    node_desc.type = Ray::RefractiveNode;
                } else if (js_type.val == "emissive") {
                    node_desc.type = Ray::EmissiveNode;
                } else if (js_type.val == "mix") {
                    node_desc.type = Ray::MixNode;

                    const JsArray &mix_materials = js_mat_obj.at("materials").as_arr();
                    for (const auto &m : mix_materials.elements) {
                        auto it = materials.find(m.as_str().val);
                        if (it != materials.end()) {
                            if (node_desc.mix_materials[0] == Ray::InvalidMaterialHandle) {
                                node_desc.mix_materials[0] = it->second;
                            } else {
                                node_desc.mix_materials[1] = it->second;
                            }
                        }
                    }
                } else if (js_type.val == "transparent") {
                    node_desc.type = Ray::TransparentNode;
                } else {
                    throw std::runtime_error("unknown material type");
                }

                materials[js_mat_name] = new_scene->AddMaterial(node_desc);
            }
        }

        const JsObject &js_meshes = js_scene.at("meshes").as_obj();
        for (const auto &js_mesh : js_meshes.elements) {
            const std::string &js_mesh_name = js_mesh.first;
            const JsObject &js_mesh_obj = js_mesh.second.as_obj();

            std::vector<float> attrs;
            std::vector<unsigned> indices, groups;

            const JsString &js_vtx_data = js_mesh_obj.at("vertex_data").as_str();
            if (js_vtx_data.val.find(".obj") != std::string::npos) {
                const uint64_t t1 = Sys::GetTimeUs();
                std::tie(attrs, indices, groups) = LoadOBJ(js_vtx_data.val);
                const uint64_t t2 = Sys::GetTimeUs();

                LOGI("OBJ loaded in %.2fms", double(t2 - t1) / 1000.0);
            } else if (js_vtx_data.val.find(".bin") != std::string::npos) {
                std::tie(attrs, indices, groups) = LoadBIN(js_vtx_data.val);
            } else {
                throw std::runtime_error("unknown mesh type");
            }

#if !defined(NDEBUG) && defined(_WIN32)
            for (int i = 0; i < int(attrs.size()); ++i) {
                if (attrs[i] > 1.0e+30F) {
                    __debugbreak();
                }
            }
#endif

            const JsArray &js_materials = js_mesh_obj.at("materials").as_arr();

            Ray::mesh_desc_t mesh_desc;
            mesh_desc.prim_type = Ray::TriangleList;
            mesh_desc.layout = Ray::PxyzNxyzTuv;
            mesh_desc.vtx_attrs = &attrs[0];
            mesh_desc.vtx_attrs_count = attrs.size() / 8;
            mesh_desc.vtx_indices = &indices[0];
            mesh_desc.vtx_indices_count = indices.size();
            mesh_desc.use_fast_bvh_build = global_settings.use_fast_bvh_build;

            for (size_t i = 0; i < groups.size(); i += 2) {
                const JsString &js_mat_name = js_materials.at(i / 2).as_str();
                const Ray::MaterialHandle mat_handle = materials.at(js_mat_name.val);
                mesh_desc.shapes.push_back({mat_handle, mat_handle, groups[i], groups[i + 1]});
            }

            if (js_mesh_obj.Has("allow_spatial_splits")) {
                JsLiteral splits = js_mesh_obj.at("allow_spatial_splits").as_lit();
                mesh_desc.allow_spatial_splits = (splits.val == JsLiteralType::True);
            }

            if (js_mesh_obj.Has("use_fast_bvh_build")) {
                JsLiteral use_fast = js_mesh_obj.at("use_fast_bvh_build").as_lit();
                mesh_desc.use_fast_bvh_build = (use_fast.val == JsLiteralType::True);
            }

            meshes[js_mesh_name] = new_scene->AddMesh(mesh_desc);
        }

        if (js_scene.Has("lights")) {
            const JsArray &js_lights = js_scene.at("lights").as_arr();
            for (const auto &js_light : js_lights.elements) {
                const JsObject &js_light_obj = js_light.as_obj();
                const JsArray &js_color = js_light_obj.at("color").as_arr();

                const Ren::Mat4f transform = parse_transform(js_light_obj);

                const JsString &js_light_type = js_light_obj.at("type").as_str();
                if (js_light_type.val == "sphere") {
                    Ray::sphere_light_desc_t new_light;

                    new_light.color[0] = float(js_color.at(0).as_num().val);
                    new_light.color[1] = float(js_color.at(1).as_num().val);
                    new_light.color[2] = float(js_color.at(2).as_num().val);

                    // only position is used
                    new_light.position[0] = transform[3][0];
                    new_light.position[1] = transform[3][1];
                    new_light.position[2] = transform[3][2];

                    new_light.radius = 1.0f;
                    if (js_light_obj.Has("radius")) {
                        const JsNumber &js_radius = js_light_obj.at("radius").as_num();
                        new_light.radius = float(js_radius.val);
                    }

                    float power = 1.0f;
                    if (js_light_obj.Has("power")) {
                        const JsNumber &js_power = js_light_obj.at("power").as_num();
                        power = float(js_power.val);
                    }

                    if (js_light_obj.Has("visible")) {
                        const JsLiteral &js_visible = js_light_obj.at("visible").as_lit();
                        new_light.visible = (js_visible.val == JsLiteralType::True);
                    }

                    if (js_light_obj.Has("cast_shadow")) {
                        const JsLiteral &js_cast_shadow = js_light_obj.at("cast_shadow").as_lit();
                        new_light.cast_shadow = (js_cast_shadow.val == JsLiteralType::True);
                    }

                    float mul = power / (4.0f * Ren::Pi<float>() * new_light.radius * new_light.radius);
                    mul /= Ren::Pi<float>(); // ???

                    new_light.color[0] *= mul;
                    new_light.color[1] *= mul;
                    new_light.color[2] *= mul;

                    if (new_light.color[0] > 0.0f || new_light.color[1] > 0.0f || new_light.color[2] > 0.0f) {
                        new_scene->AddLight(new_light);
                    }
                } else if (js_light_type.val == "spot") {
                    Ray::spot_light_desc_t new_light;

                    new_light.color[0] = float(js_color.at(0).as_num().val);
                    new_light.color[1] = float(js_color.at(1).as_num().val);
                    new_light.color[2] = float(js_color.at(2).as_num().val);

                    new_light.position[0] = transform[3][0];
                    new_light.position[1] = transform[3][1];
                    new_light.position[2] = transform[3][2];

                    new_light.direction[0] = -transform[1][0];
                    new_light.direction[1] = -transform[1][1];
                    new_light.direction[2] = -transform[1][2];

                    new_light.radius = 1.0f;
                    if (js_light_obj.Has("radius")) {
                        const JsNumber &js_radius = js_light_obj.at("radius").as_num();
                        new_light.radius = float(js_radius.val);
                    }

                    new_light.spot_size = 45.0f;
                    if (js_light_obj.Has("spot_size")) {
                        const JsNumber &js_spot_size = js_light_obj.at("spot_size").as_num();
                        new_light.spot_size = float(js_spot_size.val);
                    }

                    new_light.spot_blend = 0.15f;
                    if (js_light_obj.Has("spot_blend")) {
                        const JsNumber &js_spot_blend = js_light_obj.at("spot_blend").as_num();
                        new_light.spot_blend = float(js_spot_blend.val);
                    }

                    float power = 1.0f;
                    if (js_light_obj.Has("power")) {
                        const JsNumber &js_power = js_light_obj.at("power").as_num();
                        power = float(js_power.val);
                    }

                    if (js_light_obj.Has("visible")) {
                        const JsLiteral &js_visible = js_light_obj.at("visible").as_lit();
                        new_light.visible = (js_visible.val == JsLiteralType::True);
                    }

                    if (js_light_obj.Has("cast_shadow")) {
                        const JsLiteral &js_cast_shadow = js_light_obj.at("cast_shadow").as_lit();
                        new_light.cast_shadow = (js_cast_shadow.val == JsLiteralType::True);
                    }

                    float mul = power / (4.0f * Ren::Pi<float>() * new_light.radius * new_light.radius);
                    mul /= Ren::Pi<float>(); // ???

                    new_light.color[0] *= mul;
                    new_light.color[1] *= mul;
                    new_light.color[2] *= mul;

                    if (new_light.color[0] > 0.0f || new_light.color[1] > 0.0f || new_light.color[2] > 0.0f) {
                        new_scene->AddLight(new_light);
                    }
                } else if (js_light_type.val == "rectangle") {
                    Ray::rect_light_desc_t new_light;

                    new_light.color[0] = float(js_color.at(0).as_num().val);
                    new_light.color[1] = float(js_color.at(1).as_num().val);
                    new_light.color[2] = float(js_color.at(2).as_num().val);

                    new_light.width = 1.0f;
                    if (js_light_obj.Has("width")) {
                        const JsNumber &js_width = js_light_obj.at("width").as_num();
                        new_light.width = float(js_width.val);
                    }

                    new_light.height = 1.0f;
                    if (js_light_obj.Has("height")) {
                        const JsNumber &js_height = js_light_obj.at("height").as_num();
                        new_light.height = float(js_height.val);
                    }

                    if (js_light_obj.Has("visible")) {
                        const JsLiteral &js_visible = js_light_obj.at("visible").as_lit();
                        new_light.visible = (js_visible.val == JsLiteralType::True);
                    }

                    if (js_light_obj.Has("sky_portal")) {
                        const JsLiteral &js_sky_portal = js_light_obj.at("sky_portal").as_lit();
                        new_light.sky_portal = (js_sky_portal.val == JsLiteralType::True);
                    }

                    if (js_light_obj.Has("cast_shadow")) {
                        const JsLiteral &js_cast_shadow = js_light_obj.at("cast_shadow").as_lit();
                        new_light.cast_shadow = (js_cast_shadow.val == JsLiteralType::True);
                    }

                    if (!new_light.sky_portal) {
                        float power = 1.0f;
                        if (js_light_obj.Has("power")) {
                            const JsNumber &js_power = js_light_obj.at("power").as_num();
                            power = float(js_power.val);
                        }

                        float mul = power / (new_light.width * new_light.height);
                        mul /= 4.0f; // ???

                        new_light.color[0] *= mul;
                        new_light.color[1] *= mul;
                        new_light.color[2] *= mul;
                    }

                    if (new_light.color[0] > 0.0f || new_light.color[1] > 0.0f || new_light.color[2] > 0.0f) {
                        new_scene->AddLight(new_light, Ren::ValuePtr(transform));
                    }
                } else if (js_light_type.val == "disk") {
                    Ray::disk_light_desc_t new_light;

                    new_light.color[0] = float(js_color.at(0).as_num().val);
                    new_light.color[1] = float(js_color.at(1).as_num().val);
                    new_light.color[2] = float(js_color.at(2).as_num().val);

                    new_light.size_x = 1.0f;
                    if (js_light_obj.Has("size_x")) {
                        const JsNumber &js_size_x = js_light_obj.at("size_x").as_num();
                        new_light.size_x = float(js_size_x.val);
                    }

                    new_light.size_y = 1.0f;
                    if (js_light_obj.Has("size_y")) {
                        const JsNumber &js_size_y = js_light_obj.at("size_y").as_num();
                        new_light.size_y = float(js_size_y.val);
                    }

                    if (js_light_obj.Has("visible")) {
                        const JsLiteral &js_visible = js_light_obj.at("visible").as_lit();
                        new_light.visible = (js_visible.val == JsLiteralType::True);
                    }

                    if (js_light_obj.Has("sky_portal")) {
                        const JsLiteral &js_sky_portal = js_light_obj.at("sky_portal").as_lit();
                        new_light.sky_portal = (js_sky_portal.val == JsLiteralType::True);
                    }

                    if (js_light_obj.Has("cast_shadow")) {
                        const JsLiteral &js_cast_shadow = js_light_obj.at("cast_shadow").as_lit();
                        new_light.cast_shadow = (js_cast_shadow.val == JsLiteralType::True);
                    }

                    if (!new_light.sky_portal) {
                        float power = 1.0f;
                        if (js_light_obj.Has("power")) {
                            const JsNumber &js_power = js_light_obj.at("power").as_num();
                            power = float(js_power.val);
                        }

                        const float mul = power / (Ren::Pi<float>() * new_light.size_x * new_light.size_y);

                        new_light.color[0] *= mul;
                        new_light.color[1] *= mul;
                        new_light.color[2] *= mul;
                    }

                    if (new_light.color[0] > 0.0f || new_light.color[1] > 0.0f || new_light.color[2] > 0.0f) {
                        new_scene->AddLight(new_light, Ren::ValuePtr(transform));
                    }
                } else if (js_light_type.val == "line") {
                    Ray::line_light_desc_t new_light;

                    new_light.color[0] = float(js_color.at(0).as_num().val);
                    new_light.color[1] = float(js_color.at(1).as_num().val);
                    new_light.color[2] = float(js_color.at(2).as_num().val);

                    new_light.radius = 1.0f;
                    if (js_light_obj.Has("radius")) {
                        const JsNumber &js_radius = js_light_obj.at("radius").as_num();
                        new_light.radius = float(js_radius.val);
                    }

                    new_light.height = 1.0f;
                    if (js_light_obj.Has("height")) {
                        const JsNumber &js_height = js_light_obj.at("height").as_num();
                        new_light.height = float(js_height.val);
                    }

                    if (js_light_obj.Has("visible")) {
                        const JsLiteral &js_visible = js_light_obj.at("visible").as_lit();
                        new_light.visible = (js_visible.val == JsLiteralType::True);
                    }

                    if (js_light_obj.Has("sky_portal")) {
                        const JsLiteral &js_sky_portal = js_light_obj.at("sky_portal").as_lit();
                        new_light.sky_portal = (js_sky_portal.val == JsLiteralType::True);
                    }

                    if (js_light_obj.Has("cast_shadow")) {
                        const JsLiteral &js_cast_shadow = js_light_obj.at("cast_shadow").as_lit();
                        new_light.cast_shadow = (js_cast_shadow.val == JsLiteralType::True);
                    }

                    if (!new_light.sky_portal) {
                        float power = 1.0f;
                        if (js_light_obj.Has("power")) {
                            const JsNumber &js_power = js_light_obj.at("power").as_num();
                            power = float(js_power.val);
                        }

                        const float mul = power / (2.0f * Ren::Pi<float>() * new_light.radius * new_light.height);

                        new_light.color[0] *= mul;
                        new_light.color[1] *= mul;
                        new_light.color[2] *= mul;
                    }

                    if (new_light.color[0] > 0.0f || new_light.color[1] > 0.0f || new_light.color[2] > 0.0f) {
                        new_scene->AddLight(new_light, Ren::ValuePtr(transform));
                    }
                }
            }
        }

        const JsArray &js_mesh_instances = js_scene.at("mesh_instances").as_arr();
        for (const auto &js_mesh_instance : js_mesh_instances.elements) {
            const JsObject &js_mesh_instance_obj = js_mesh_instance.as_obj();
            const JsString &js_mesh_name = js_mesh_instance_obj.at("mesh").as_str();

            const Ren::Mat4f transform = parse_transform(js_mesh_instance_obj);

            const Ray::MeshHandle mesh_handle = meshes.at(js_mesh_name.val);
            new_scene->AddMeshInstance(mesh_handle, Ren::ValuePtr(transform));
        }
    } catch (std::runtime_error &e) {
        LOGE("Error in parsing json file! %s", e.what());
        return nullptr;
    }

    new_scene->Finalize();

    return new_scene;
}

std::tuple<std::vector<float>, std::vector<unsigned>, std::vector<unsigned>> LoadOBJ(const std::string &file_name) {
    std::vector<float> attrs;
    std::vector<unsigned> indices;
    std::vector<unsigned> groups;

    std::vector<Ren::Vec3f> v;
    std::vector<float> vn, vt;

    std::ifstream in_file(file_name, std::ios::binary);
    if (!in_file) {
        throw std::runtime_error("File can not be opened!");
    }

    const int SearchGridRes = 32;
    std::vector<Ren::SmallVector<uint32_t, 16>> search_grid(SearchGridRes * SearchGridRes * SearchGridRes);
    auto bbox_min = Ren::Vec3f{std::numeric_limits<float>::max()},
         bbox_max = Ren::Vec3f{std::numeric_limits<float>::lowest()};

    auto grid_index = [&](Ren::Vec3f p) -> int {
        p = (p - bbox_min) / (bbox_max - bbox_min + Ren::Vec3f{std::numeric_limits<float>::epsilon()});
        p *= float(SearchGridRes);

        const int ix = int(p[0]);
        const int iy = int(p[1]);
        const int iz = int(p[2]);

        return std::min(std::max(iz * SearchGridRes * SearchGridRes + iy * SearchGridRes + ix, 0),
                        SearchGridRes * SearchGridRes * SearchGridRes - 1);
    };

    auto indices_at = [&](Ren::Vec3f p) -> const Ren::SmallVectorImpl<uint32_t> & {
        return search_grid[grid_index(p)];
    };

    std::string line;
    while (std::getline(in_file, line)) {
        const char *delims = " /\r\n";
        const char *p = line.c_str();
        const char *q = strpbrk(p + 1, delims);

        if ((q - p) == 1 && p[0] == 'v') {
            p = q + 1;
            q = strpbrk(p, delims);
            const float x = strtof(p, nullptr);
            p = q + 1;
            q = strpbrk(p, delims);
            const float y = strtof(p, nullptr);
            p = q + 1;
            q = strpbrk(p, delims);
            const float z = strtof(p, nullptr);
            v.emplace_back(x, y, z);

            bbox_min = Min(bbox_min, v.back());
            bbox_max = Max(bbox_max, v.back());
        } else if ((q - p) == 2 && p[0] == 'v' && p[1] == 'n') {
            p = q + 1;
            q = strpbrk(p, delims);
            vn.push_back(strtof(p, nullptr));
            p = q + 1;
            q = strpbrk(p, delims);
            vn.push_back(strtof(p, nullptr));
            p = q + 1;
            q = strpbrk(p, delims);
            vn.push_back(strtof(p, nullptr));
        } else if ((q - p) == 2 && p[0] == 'v' && p[1] == 't') {
            p = q + 1;
            q = strpbrk(p, delims);
            vt.push_back(strtof(p, nullptr));
            p = q + 1;
            q = strpbrk(p, delims);
            vt.push_back(strtof(p, nullptr));
        } else if ((q - p) == 1 && p[0] == 'f') {
            for (int j = 0; j < 3; j++) {
                p = q + 1;
                q = strpbrk(p, delims);
                const long i1 = strtol(p, nullptr, 10) - 1;
                p = q + 1;
                q = strpbrk(p, delims);
                const long i2 = strtol(p, nullptr, 10) - 1;
                p = q + 1;
                q = strpbrk(p, delims);
                const long i3 = strtol(p, nullptr, 10) - 1;

                bool found = false;
                for (const uint32_t i : indices_at(v[i1])) {
                    if (_abs(attrs[i * 8 + 0] - v[i1][0]) < 0.0000001f &&
                        _abs(attrs[i * 8 + 1] - v[i1][1]) < 0.0000001f &&
                        _abs(attrs[i * 8 + 2] - v[i1][2]) < 0.0000001f &&
                        _abs(attrs[i * 8 + 3] - vn[i3 * 3]) < 0.0000001f &&
                        _abs(attrs[i * 8 + 4] - vn[i3 * 3 + 1]) < 0.0000001f &&
                        _abs(attrs[i * 8 + 5] - vn[i3 * 3 + 2]) < 0.0000001f &&
                        _abs(attrs[i * 8 + 6] - vt[i2 * 2]) < 0.0000001f &&
                        _abs(attrs[i * 8 + 7] - vt[i2 * 2 + 1]) < 0.0000001f) {
                        indices.push_back(i);
                        found = true;
                        break;
                    }
                }

                if (!found) {
                    const uint32_t index = uint32_t(attrs.size() / 8);
                    search_grid[grid_index(v[i1])].push_back(index);

                    indices.push_back(index);
                    attrs.push_back(v[i1][0]);
                    attrs.push_back(v[i1][1]);
                    attrs.push_back(v[i1][2]);

                    attrs.push_back(vn[i3 * 3]);
                    attrs.push_back(vn[i3 * 3 + 1]);
                    attrs.push_back(vn[i3 * 3 + 2]);

                    attrs.push_back(vt[i2 * 2]);
                    attrs.push_back(vt[i2 * 2 + 1]);
                }
            }
        } else if ((q - p) == 1 && p[0] == 'g') {
            if (!groups.empty()) {
                groups.push_back(uint32_t(indices.size() - groups.back()));
            }
            groups.push_back(uint32_t(indices.size()));
        }
    }

    if (groups.empty()) {
        groups.push_back(0);
    }

    groups.push_back(uint32_t(indices.size() - groups.back()));

#if DUMP_BIN_FILES
    {
        std::string out_file_name = file_name;
        out_file_name[out_file_name.size() - 3] = 'b';
        out_file_name[out_file_name.size() - 2] = 'i';
        out_file_name[out_file_name.size() - 1] = 'n';

        std::ofstream out_file(out_file_name, std::ios::binary);

        uint32_t s;

        s = uint32_t(attrs.size());
        out_file.write((char *)&s, sizeof(s));

        s = uint32_t(indices.size());
        out_file.write((char *)&s, sizeof(s));

        s = uint32_t(groups.size());
        out_file.write((char *)&s, sizeof(s));

        out_file.write((char *)&attrs[0], attrs.size() * sizeof(attrs[0]));
        out_file.write((char *)&indices[0], indices.size() * sizeof(indices[0]));
        out_file.write((char *)&groups[0], groups.size() * sizeof(groups[0]));
    }
#endif

#if 0
    {
        std::string out_file_name = file_name;
        out_file_name[out_file_name.size() - 3] = 'h';
        out_file_name[out_file_name.size() - 2] = '\0';
        out_file_name[out_file_name.size() - 1] = '\0';

        std::ofstream out_file(out_file_name, std::ios::binary);
        out_file << std::setprecision(16) << std::fixed;

        out_file << "static float attrs[] = {\n\t";
        for (size_t i = 0; i < attrs.size(); i++) {
            out_file << attrs[i] << "f, ";
            if (i % 10 == 0 && i != 0) out_file << "\n\t";
        }
        out_file << "\n};\n";
        out_file << "static size_t attrs_count = " << attrs.size() << ";\n\n";

        out_file << "static uint32_t indices[] = {\n\t";
        for (size_t i = 0; i < indices.size(); i++) {
            out_file << indices[i] << ", ";
            if (i % 10 == 0 && i != 0) out_file << "\n\t";
        }
        out_file << "\n};\n";
        out_file << "static size_t indices_count = " << indices.size() << ";\n\n";

        out_file << "static uint32_t groups[] = {\n\t";
        for (size_t i = 0; i < groups.size(); i++) {
            out_file << groups[i] << ", ";
            if (i % 10 == 0 && i != 0) out_file << "\n\t";
        }
        out_file << "\n};\n";
        out_file << "static size_t groups_count = " << groups.size() << ";\n\n";
    }
#endif

    return std::make_tuple(std::move(attrs), std::move(indices), std::move(groups));
}

std::tuple<std::vector<float>, std::vector<unsigned>, std::vector<unsigned>> LoadBIN(const std::string &file_name) {
    std::ifstream in_file(file_name, std::ios::binary);
    uint32_t num_attrs;
    in_file.read((char *)&num_attrs, 4);
    uint32_t num_indices;
    in_file.read((char *)&num_indices, 4);
    uint32_t num_groups;
    in_file.read((char *)&num_groups, 4);

    std::vector<float> attrs;
    attrs.resize(num_attrs);
    in_file.read((char *)&attrs[0], (size_t)num_attrs * 4);

    std::vector<unsigned> indices;
    indices.resize((size_t)num_indices);
    in_file.read((char *)&indices[0], (size_t)num_indices * 4);

    std::vector<unsigned> groups;
    groups.resize((size_t)num_groups);
    in_file.read((char *)&groups[0], (size_t)num_groups * 4);

    return std::make_tuple(std::move(attrs), std::move(indices), std::move(groups));
}

std::vector<Ray::color_rgba8_t> LoadTGA(const std::string &name, int &w, int &h) {
    std::vector<Ray::color_rgba8_t> tex_data;

    {
        std::ifstream in_file(name, std::ios::binary);
        if (!in_file)
            return {};

        in_file.seekg(0, std::ios::end);
        size_t in_file_size = (size_t)in_file.tellg();
        in_file.seekg(0, std::ios::beg);

        std::vector<char> in_file_data(in_file_size);
        in_file.read(&in_file_data[0], in_file_size);

        Ren::eTexColorFormat format;
        auto pixels = Ren::ReadTGAFile(&in_file_data[0], w, h, format);

        if (format == Ren::RawRGB888) {
            for (int y = 0; y < h; y++) {
                for (int x = 0; x < w; x++) {
                    tex_data.push_back(
                        {pixels[3 * (y * w + x)], pixels[3 * (y * w + x) + 1], pixels[3 * (y * w + x) + 2], 255});
                }
            }
        } else if (format == Ren::RawRGBA8888) {
            for (int y = 0; y < h; y++) {
                for (int x = 0; x < w; x++) {
                    tex_data.push_back({pixels[4 * (y * w + x)], pixels[4 * (y * w + x) + 1],
                                        pixels[4 * (y * w + x) + 2], pixels[4 * (y * w + x) + 3]});
                }
            }
        } else {
            assert(false);
        }
    }

    return tex_data;
}

std::vector<Ray::color_rgba8_t> LoadHDR(const std::string &name, int &out_w, int &out_h) {
    std::ifstream in_file(name, std::ios::binary);

    std::string line;
    if (!std::getline(in_file, line) || line != "#?RADIANCE") {
        throw std::runtime_error("Is not HDR file!");
    }

    float exposure = 1.0f;
    std::string format;

    while (std::getline(in_file, line)) {
        if (line.empty())
            break;

        if (!line.compare(0, 6, "FORMAT")) {
            format = line.substr(7);
        } else if (!line.compare(0, 8, "EXPOSURE")) {
            exposure = (float)atof(line.substr(9).c_str());
        }
    }

    if (format != "32-bit_rle_rgbe") {
        throw std::runtime_error("Wrong format!");
    }

    int res_x = 0, res_y = 0;

    std::string resolution;
    if (!std::getline(in_file, resolution)) {
        throw std::runtime_error("Cannot read resolution!");
    }

    { // parse resolution
        const char *delims = " \r\n";
        const char *p = resolution.c_str();
        const char *q = strpbrk(p + 1, delims);

        if ((q - p) != 2 || p[0] != '-' || p[1] != 'Y') {
            throw std::runtime_error("Unsupported format!");
        }

        p = q + 1;
        q = strpbrk(p, delims);
        res_y = int(strtol(p, nullptr, 10));

        p = q + 1;
        q = strpbrk(p, delims);
        if ((q - p) != 2 || p[0] != '+' || p[1] != 'X') {
            throw std::runtime_error("Unsupported format!");
        }

        p = q + 1;
        q = strpbrk(p, delims);
        res_x = int(strtol(p, nullptr, 10));
    }

    if (!res_x || !res_y) {
        throw std::runtime_error("Unsupported format!");
    }

    out_w = res_x;
    out_h = res_y;

    std::vector<Ray::color_rgba8_t> data(res_x * res_y * 4);
    int data_offset = 0;

    int scanlines_left = res_y;
    std::vector<uint8_t> scanline(res_x * 4);

    while (scanlines_left) {
        {
            uint8_t rgbe[4];

            if (!in_file.read((char *)&rgbe[0], 4)) {
                throw std::runtime_error("Cannot read file!");
            }

            if ((rgbe[0] != 2) || (rgbe[1] != 2) || ((rgbe[2] & 0x80) != 0)) {
                data[0].v[0] = rgbe[0];
                data[0].v[1] = rgbe[1];
                data[0].v[2] = rgbe[2];
                data[0].v[3] = rgbe[3];

                if (!in_file.read((char *)&data[4], (res_x * scanlines_left - 1) * 4)) {
                    throw std::runtime_error("Cannot read file!");
                }
                return data;
            }

            if ((((rgbe[2] & 0xFF) << 8) | (rgbe[3] & 0xFF)) != res_x) {
                throw std::runtime_error("Wrong scanline width!");
            }
        }

        int index = 0;
        for (int i = 0; i < 4; i++) {
            int index_end = (i + 1) * res_x;
            while (index < index_end) {
                uint8_t buf[2];
                if (!in_file.read((char *)&buf[0], 2)) {
                    throw std::runtime_error("Cannot read file!");
                }

                if (buf[0] > 128) {
                    int count = buf[0] - 128;
                    if ((count == 0) || (count > index_end - index)) {
                        throw std::runtime_error("Wrong data!");
                    }
                    while (count-- > 0) {
                        scanline[index++] = buf[1];
                    }
                } else {
                    int count = buf[0];
                    if ((count == 0) || (count > index_end - index)) {
                        throw std::runtime_error("Wrong data!");
                    }
                    scanline[index++] = buf[1];
                    if (--count > 0) {
                        if (!in_file.read((char *)&scanline[index], count)) {
                            throw std::runtime_error("Cannot read file!");
                        }
                        index += count;
                    }
                }
            }
        }

        for (int i = 0; i < res_x; i++) {
            data[data_offset].v[0] = scanline[i + 0 * res_x];
            data[data_offset].v[1] = scanline[i + 1 * res_x];
            data[data_offset].v[2] = scanline[i + 2 * res_x];
            data[data_offset].v[3] = scanline[i + 3 * res_x];
            data_offset++;
        }

        scanlines_left--;
    }

    return data;
}

std::vector<Ray::color_rgba8_t> Load_stb_image(const std::string &name, int &w, int &h) {
    stbi_set_flip_vertically_on_load(1);

    int channels;
    uint8_t *img_data = stbi_load(name.c_str(), &w, &h, &channels, 4);
    if (!img_data) {
        return {};
    }

    std::vector<Ray::color_rgba8_t> tex_data(w * h);
    memcpy(&tex_data[0].v[0], img_data, w * h * sizeof(Ray::color_rgba8_t));

    stbi_image_free(img_data);

    return tex_data;
}

#define float_to_byte(val)                                                                                             \
    (((val) <= 0.0f) ? 0 : (((val) > (1.0f - 0.5f / 255.0f)) ? 255 : uint8_t((255.0f * (val)) + 0.5f)))

void WriteTGA(const Ray::pixel_color_t *data, const int w, const int h, const int bpp, const bool flip_vertical,
              const char *name) {
    std::ofstream file(name, std::ios::binary);

    unsigned char header[18] = {0, 0, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};

    header[12] = w & 0xFF;
    header[13] = (w >> 8) & 0xFF;
    header[14] = (h)&0xFF;
    header[15] = (h >> 8) & 0xFF;
    header[16] = bpp * 8;

    file.write((char *)&header[0], sizeof(unsigned char) * 18);

    auto out_data = std::unique_ptr<uint8_t[]>{new uint8_t[size_t(w) * h * bpp]};
    for (int j = 0; j < h; ++j) {
        const int _j = flip_vertical ? (h - j - 1) : j;
        for (int i = 0; i < w; ++i) {
            out_data[(j * w + i) * bpp + 0] = float_to_byte(data[_j * w + i].b);
            out_data[(j * w + i) * bpp + 1] = float_to_byte(data[_j * w + i].g);
            out_data[(j * w + i) * bpp + 2] = float_to_byte(data[_j * w + i].r);
            if (bpp == 4) {
                out_data[i * 4 + 3] = float_to_byte(data[_j * w + i].a);
            }
        }
    }

    file.write((const char *)&out_data[0], size_t(w) * h * bpp);

    static const char footer[26] = "\0\0\0\0"         // no extension area
                                   "\0\0\0\0"         // no developer directory
                                   "TRUEVISION-XFILE" // yep, this is a TGA file
                                   ".";
    file.write(footer, sizeof(footer));
}

void WritePNG(const Ray::pixel_color_t *data, const int w, const int h, const int bpp, const bool flip_vertical,
              const char *name) {
    auto out_data = std::unique_ptr<uint8_t[]>{new uint8_t[size_t(w) * h * bpp]};
    for (int j = 0; j < h; ++j) {
        const int _j = flip_vertical ? (h - j - 1) : j;
        for (int i = 0; i < w; ++i) {
            out_data[(j * w + i) * bpp + 2] = float_to_byte(data[_j * w + i].b);
            out_data[(j * w + i) * bpp + 1] = float_to_byte(data[_j * w + i].g);
            out_data[(j * w + i) * bpp + 0] = float_to_byte(data[_j * w + i].r);
            if (bpp == 4) {
                out_data[i * 4 + 3] = float_to_byte(data[_j * w + i].a);
            }
        }
    }

    stbi_write_png(name, w, h, bpp, out_data.get(), bpp * w);
}

#undef float_to_byte
#undef _abs
