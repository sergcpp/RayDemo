#include "test_common.h"

#include <regex>

#include "../RendererFactory.h"

#include "test_scene.h"
#include "utils.h"

void test_aux_channels(const char *arch_list[], const char *preferred_device) {
    const char TestName[] = "aux_channels";

    //
    // Load reference images
    //

    int test_img_w, test_img_h;
    const auto base_color_ref = LoadTGA("test_data/aux_channels/base_color_ref.tga", test_img_w, test_img_h);
    require_return(!base_color_ref.empty());

    int normals_img_w, normals_img_h;
    const auto normals_ref = LoadTGA("test_data/aux_channels/normals_ref.tga", normals_img_w, normals_img_h);
    require_return(!normals_ref.empty());
    require_return(normals_img_w == test_img_w && normals_img_h == test_img_h);

    int depth_img_w, depth_img_h;
    const auto depth_ref = LoadTGA("test_data/aux_channels/depth_ref.tga", depth_img_w, depth_img_h);
    require_return(!depth_ref.empty());
    require_return(depth_img_w == test_img_w && depth_img_h == test_img_h);

    //
    // Setup scene
    //

    Ray::principled_mat_desc_t mat_desc;
    mat_desc.base_texture = Ray::TextureHandle{0};
    mat_desc.metallic = 1.0f;
    mat_desc.roughness = 1.0f;
    mat_desc.roughness_texture = Ray::TextureHandle{2};
    mat_desc.metallic = 1.0f;
    mat_desc.metallic_texture = Ray::TextureHandle{3};
    mat_desc.normal_map = Ray::TextureHandle{1};
    mat_desc.alpha_texture = Ray::TextureHandle{4};

    const char *textures[] = {
        "test_data/textures/Fence007A_2K_Color.tga", "test_data/textures/Fence007A_2K_NormalGL.tga",
        "test_data/textures/Fence007A_2K_Roughness.tga", "test_data/textures/Fence007A_2K_Metalness.tga",
        "test_data/textures/Fence007A_2K_Opacity.tga"};

    Ray::settings_t s;
    s.w = test_img_w;
    s.h = test_img_h;
#ifdef ENABLE_GPU_IMPL
    s.preferred_device = preferred_device;
#endif
    s.use_wide_bvh = true;

    const int SampleCount = 256;
    const double BaseColor_MinPSNR = 28.8, Normals_MinPSNR = 43.4, Depth_MinPSNR = 43.3;

    const int DiffThres = 32;

    for (const bool use_hwrt : {false, true}) {
        s.use_hwrt = use_hwrt;
        for (const char **arch = arch_list; *arch; ++arch) {
            const auto rt = Ray::RendererTypeFromName(*arch);

            auto renderer = std::unique_ptr<Ray::RendererBase>(Ray::CreateRenderer(s, &g_log_err, rt));
            if (!renderer || renderer->type() != rt || renderer->is_hwrt() != use_hwrt) {
                // skip unsupported (we fell back to some other renderer)
                continue;
            }
            if (preferred_device) {
                // make sure we use requested device
                std::regex match_name(preferred_device);
                if (!require(std::regex_search(renderer->device_name(), match_name)) &&
                    strcmp(renderer->device_name(), preferred_device) != 0) {
                    printf("Wrong device: %s (%s was requested)\n", renderer->device_name(), preferred_device);
                    return;
                }
            }

            auto scene = std::unique_ptr<Ray::SceneBase>(renderer->CreateScene());

            setup_test_scene(*scene, false, true, true, mat_desc, textures, eTestScene::Standard);

            char name_buf[1024];
            snprintf(name_buf, sizeof(name_buf), "Test %s", TestName);
            schedule_render_jobs(*renderer, scene.get(), s, SampleCount, name_buf);

            const auto *base_color_pixels =
                reinterpret_cast<const Ray::color_rgba_t *>(renderer->get_aux_pixels_ref(Ray::eAUXBuffer::BaseColor));
            const auto *depth_normals_pixels =
                reinterpret_cast<const Ray::color_rgba_t *>(renderer->get_aux_pixels_ref(Ray::eAUXBuffer::DepthNormals));

            std::unique_ptr<uint8_t[]> base_color_data_u8(new uint8_t[test_img_w * test_img_h * 3]);
            std::unique_ptr<uint8_t[]> normals_data_u8(new uint8_t[test_img_w * test_img_h * 3]);
            std::unique_ptr<uint8_t[]> depth_data_u8(new uint8_t[test_img_w * test_img_h * 3]);

            double base_color_mse = 0.0, normals_mse = 0.0, depth_mse = 0.0;

            for (int j = 0; j < test_img_h; j++) {
                for (int i = 0; i < test_img_w; i++) {
                    { // check base color
                        Ray::color_rgba_t c = base_color_pixels[j * test_img_w + i];

                        for (int k = 0; k < 3; ++k) {
                            if (c.v[k] < 0.0031308f) {
                                c.v[k] = 12.92f * c.v[k];
                            } else {
                                c.v[k] = 1.055f * std::pow(c.v[k], (1.0f / 2.4f)) - 0.055f;
                            }
                        }

                        const auto r = uint8_t(c.v[0] * 255);
                        const auto g = uint8_t(c.v[1] * 255);
                        const auto b = uint8_t(c.v[2] * 255);

                        base_color_data_u8[3 * ((test_img_h - j - 1) * test_img_w + i) + 0] = r;
                        base_color_data_u8[3 * ((test_img_h - j - 1) * test_img_w + i) + 1] = g;
                        base_color_data_u8[3 * ((test_img_h - j - 1) * test_img_w + i) + 2] = b;

                        const uint8_t diff_r =
                            std::abs(r - base_color_ref[4 * ((test_img_h - j - 1) * test_img_w + i) + 0]);
                        const uint8_t diff_g =
                            std::abs(g - base_color_ref[4 * ((test_img_h - j - 1) * test_img_w + i) + 1]);
                        const uint8_t diff_b =
                            std::abs(b - base_color_ref[4 * ((test_img_h - j - 1) * test_img_w + i) + 2]);

                        base_color_mse += diff_r * diff_r;
                        base_color_mse += diff_g * diff_g;
                        base_color_mse += diff_b * diff_b;
                    }
                    { // check normals
                        const Ray::color_rgba_t &n = depth_normals_pixels[j * test_img_w + i];

                        const auto r = uint8_t((n.v[0] * 0.5f + 0.5f) * 255);
                        const auto g = uint8_t((n.v[1] * 0.5f + 0.5f) * 255);
                        const auto b = uint8_t((n.v[2] * 0.5f + 0.5f) * 255);

                        normals_data_u8[3 * ((test_img_h - j - 1) * test_img_w + i) + 0] = r;
                        normals_data_u8[3 * ((test_img_h - j - 1) * test_img_w + i) + 1] = g;
                        normals_data_u8[3 * ((test_img_h - j - 1) * test_img_w + i) + 2] = b;

                        const uint8_t diff_r =
                            std::abs(r - normals_ref[4 * ((test_img_h - j - 1) * test_img_w + i) + 0]);
                        const uint8_t diff_g =
                            std::abs(g - normals_ref[4 * ((test_img_h - j - 1) * test_img_w + i) + 1]);
                        const uint8_t diff_b =
                            std::abs(b - normals_ref[4 * ((test_img_h - j - 1) * test_img_w + i) + 2]);

                        normals_mse += diff_r * diff_r;
                        normals_mse += diff_g * diff_g;
                        normals_mse += diff_b * diff_b;
                    }
                    { // check depth
                        const Ray::color_rgba_t &n = depth_normals_pixels[j * test_img_w + i];

                        const auto u8 = uint8_t(n.v[3] * 255);

                        depth_data_u8[3 * ((test_img_h - j - 1) * test_img_w + i) + 0] = u8;
                        depth_data_u8[3 * ((test_img_h - j - 1) * test_img_w + i) + 1] = u8;
                        depth_data_u8[3 * ((test_img_h - j - 1) * test_img_w + i) + 2] = u8;

                        const uint8_t diff_d =
                            std::abs(u8 - depth_ref[4 * ((test_img_h - j - 1) * test_img_w + i) + 0]);

                        depth_mse += diff_d * diff_d;
                    }
                }
            }

            base_color_mse /= 3.0 * test_img_w * test_img_h;
            double base_color_psnr = -10.0 * std::log10(base_color_mse / (255.0 * 255.0));
            base_color_psnr = std::floor(base_color_psnr * 100.0) / 100.0;

            normals_mse /= 3.0 * test_img_w * test_img_h;
            double normals_psnr = -10.0 * std::log10(normals_mse / (255.0 * 255.0));
            normals_psnr = std::floor(normals_psnr * 100.0) / 100.0;

            depth_mse /= test_img_w * test_img_h;
            double depth_psnr = -10.0 * std::log10(depth_mse / (255.0 * 255.0));
            depth_psnr = std::floor(depth_psnr * 100.0) / 100.0;

            printf("(PSNR: %.2f/%.2f dB, %.2f/%.2f dB, %.2f/%.2f dB)\n", base_color_psnr, BaseColor_MinPSNR, normals_psnr,
                   Normals_MinPSNR, depth_psnr, Depth_MinPSNR);

            const char *type_name = Ray::RendererTypeName(rt);

            snprintf(name_buf, sizeof(name_buf), "test_data/%s/%s_base_color_out.tga", TestName, type_name);
            WriteTGA(&base_color_data_u8[0], test_img_w, test_img_h, 3, name_buf);
            snprintf(name_buf, sizeof(name_buf), "test_data/%s/%s_normals_out.tga", TestName, type_name);
            WriteTGA(&normals_data_u8[0], test_img_w, test_img_h, 3, name_buf);
            snprintf(name_buf, sizeof(name_buf), "test_data/%s/%s_depth_out.tga", TestName, type_name);
            WriteTGA(&depth_data_u8[0], test_img_w, test_img_h, 3, name_buf);

            require(base_color_psnr >= BaseColor_MinPSNR);
            require(normals_psnr >= Normals_MinPSNR);
            require(depth_psnr >= Depth_MinPSNR);
        }
    }
}