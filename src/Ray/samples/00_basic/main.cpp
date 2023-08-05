#include <cstdint>
#include <cstdio>
#include <cstring>

#include <fstream>
#include <memory>

#include "../../Ray.h"

void WriteTGA(const Ray::color_rgba_t *data, int pitch, int w, int h, int bpp, bool flip_vertical, const char *name);

int main() {
    const int IMG_W = 256, IMG_H = 256;
    const int SAMPLE_COUNT = 64;

    // Initial frame resolution, can be changed later
    Ray::settings_t s;
    s.w = IMG_W;
    s.h = IMG_H;

    // Additional Ray::eRendererType parameter can be passed (Vulkan GPU renderer created by default)
    Ray::RendererBase *renderer = Ray::CreateRenderer(s, &Ray::g_stdout_log);

    // Each renderer has its own storage implementation (RAM, GPU-RAM),
    // so renderer itself should create scene object
    Ray::SceneBase *scene = renderer->CreateScene();

    // Setup environment
    Ray::environment_desc_t env_desc;
    env_desc.env_col[0] = env_desc.env_col[1] = env_desc.env_col[2] = 0.0f;
    scene->SetEnvironment(env_desc);

    // Add diffuse material
    Ray::shading_node_desc_t mat_desc1;
    mat_desc1.type = Ray::eShadingNode::Diffuse;

    const Ray::MaterialHandle mat1 = scene->AddMaterial(mat_desc1);

    // Add emissive materials
    Ray::shading_node_desc_t mat_desc2;
    mat_desc2.type = Ray::eShadingNode::Emissive;
    mat_desc2.strength = 4.0f;
    mat_desc2.base_color[0] = 1.0f;
    mat_desc2.base_color[1] = 0.0f;
    mat_desc2.base_color[2] = 0.0f;
    mat_desc2.multiple_importance = true; // Use NEE for this lightsource

    const Ray::MaterialHandle mat2 = scene->AddMaterial(mat_desc2);

    mat_desc2.base_color[0] = 0.0f;
    mat_desc2.base_color[1] = 1.0f;
    const Ray::MaterialHandle mat3 = scene->AddMaterial(mat_desc2);

    mat_desc2.base_color[1] = 0.0f;
    mat_desc2.base_color[2] = 1.0f;
    const Ray::MaterialHandle mat4 = scene->AddMaterial(mat_desc2);

    // Setup test mesh
    // Attribute layout is controlled by Ray::eVertexLayout enums
    // Is this example(PxyzNxyzTuv): position(3 floats), normal(3 floats), tex_coord(2 floats)
    // clang-format off
    const float attrs[] = { -1.0f, 0.0f, -1.0f,     0.0f, 1.0f, 0.0f,   1.0f, 0.0f,
                            1.0f, 0.0f, -1.0f,      0.0f, 1.0f, 0.0f,   0.0f, 0.0f,
                            1.0f, 0.0f, 1.0f,       0.0f, 1.0f, 0.0f,   0.0f, 1.0f,
                            -1.0f, 0.0f, 1.0f,      0.0f, 1.0f, 0.0f,   1.0f, 1.0f,

                            -1.0f, 0.5f, -1.0f,     0.0f, 1.0f, 0.0f,   1.0f, 1.0f,
                            -0.33f, 0.5f, -1.0f,    0.0f, 1.0f, 0.0f,   1.0f, 1.0f,
                            -0.33f, 0.0f, -1.0f,    0.0f, 1.0f, 0.0f,   1.0f, 1.0f,
                            0.33f, 0.5f, -1.0f,     0.0f, 1.0f, 0.0f,   0.0f, 0.0f,
                            0.33f, 0.0f, -1.0f,     0.0f, 1.0f, 0.0f,   0.0f, 0.0f,
                            1.0f, 0.5f, -1.0f,      0.0f, 1.0f, 0.0f,   0.0f, 0.0f };
    const uint32_t indices[] = { 0, 2, 1, 0, 3, 2,
                                 0, 5, 4, 6, 5, 0,
                                 5, 6, 7, 7, 6, 8,
                                 7, 8, 9, 8, 1, 9 };
    // clang-format on

    Ray::mesh_desc_t mesh_desc;
    mesh_desc.prim_type = Ray::ePrimType::TriangleList;
    mesh_desc.layout = Ray::eVertexLayout::PxyzNxyzTuv;
    mesh_desc.vtx_attrs = &attrs[0];
    mesh_desc.vtx_attrs_count = 10;
    mesh_desc.vtx_indices = &indices[0];
    mesh_desc.vtx_indices_count = 24;

    // Setup material groups
    mesh_desc.shapes.emplace_back(mat1, 0, 6);
    mesh_desc.shapes.emplace_back(mat2, 6, 6);
    mesh_desc.shapes.emplace_back(mat3, 12, 6);
    mesh_desc.shapes.emplace_back(mat4, 18, 6);

    Ray::MeshHandle mesh1 = scene->AddMesh(mesh_desc);

    // Instantiate mesh
    const float xform[] = {1.0f, 0.0f, 0.0f, 0.0f, //
                           0.0f, 1.0f, 0.0f, 0.0f, //
                           0.0f, 0.0f, 1.0f, 0.0f, //
                           0.0f, 0.0f, 0.0f, 1.0f};
    scene->AddMeshInstance(mesh1, xform);

    // Add camera
    const float view_origin[] = {2.0f, 2.0f, 2.0f};
    const float view_dir[] = {-0.577f, -0.577f, -0.577f};

    Ray::camera_desc_t cam_desc;
    cam_desc.type = Ray::eCamType::Persp;
    cam_desc.filter = Ray::eFilterType::Tent;
    memcpy(&cam_desc.origin[0], &view_origin[0], 3 * sizeof(float));
    memcpy(&cam_desc.fwd[0], &view_dir[0], 3 * sizeof(float));
    cam_desc.fov = 45.0f;
    cam_desc.gamma = 2.2f;

    const Ray::CameraHandle cam = scene->AddCamera(cam_desc);
    scene->set_current_cam(cam);

    scene->Finalize();

    // Create region contex for frame, setup to use whole frame
    auto region = Ray::RegionContext{{0, 0, IMG_W, IMG_H}};

    // Render image
    for (int i = 0; i < SAMPLE_COUNT; i++) {
        // Each call performs one iteration, blocks until finished
        renderer->RenderScene(scene, region);
        printf("Renderered %i samples\n", i);
    }
    printf("Done\n");

    // Get rendered image pixels in 32-bit floating point RGBA format
    const Ray::color_data_rgba_t pixels = renderer->get_pixels_ref();

    // Save image
    WriteTGA(pixels.ptr, pixels.pitch, IMG_W, IMG_H, 4, true, "00_basic.tga");
    printf("Image saved as samples/00_basic.tga\n");

    delete scene;
    delete renderer;
}

#define float_to_byte(val)                                                                                             \
    (((val) <= 0.0f) ? 0 : (((val) > (1.0f - 0.5f / 255.0f)) ? 255 : uint8_t((255.0f * (val)) + 0.5f)))

void WriteTGA(const Ray::color_rgba_t *data, int pitch, const int w, const int h, const int bpp,
              const bool flip_vertical, const char *name) {
    if (!pitch) {
        pitch = w;
    }

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
            out_data[(j * w + i) * bpp + 0] = float_to_byte(data[_j * pitch + i].v[2]);
            out_data[(j * w + i) * bpp + 1] = float_to_byte(data[_j * pitch + i].v[1]);
            out_data[(j * w + i) * bpp + 2] = float_to_byte(data[_j * pitch + i].v[0]);
            if (bpp == 4) {
                out_data[i * 4 + 3] = float_to_byte(data[_j * pitch + i].v[3]);
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
