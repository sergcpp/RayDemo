#pragma once

#include <cstddef>
#include <cstdint>

#include <mutex>
#include <shared_mutex>
#include <vector>

#include "Types.h"

/**
  @file SceneBase.h
  @brief Contains common scene interface
*/

namespace Ray {
#define DEFINE_HANDLE(object)                                                                                          \
    using object = struct object##_T {                                                                                 \
        uint32_t _index;                                                                                               \
    };                                                                                                                 \
    inline bool operator==(const object lhs, const object rhs) { return lhs._index == rhs._index; }                    \
    inline bool operator!=(const object lhs, const object rhs) { return lhs._index != rhs._index; }                    \
    static const object Invalid##object = object{0xffffffff};

DEFINE_HANDLE(CameraHandle)
DEFINE_HANDLE(TextureHandle)
DEFINE_HANDLE(MaterialHandle)
DEFINE_HANDLE(MeshHandle)
DEFINE_HANDLE(MeshInstanceHandle)
DEFINE_HANDLE(LightHandle)

#undef DEFINE_HANDLE

/// Mesh primitive type
enum class ePrimType {
    TriangleList, ///< indexed triangle list
};

/** Vertex attribute layout.
    P - vertex position
    N - vertex normal
    B - vertex binormal (oriented to vertical texture axis)
    T - vertex texture coordinates
*/
enum class eVertexLayout {
    PxyzNxyzTuv = 0,    ///< [ P.x, P.y, P.z, N.x, N.y, N.z, T.x, T.y ]
    PxyzNxyzTuvTuv,     ///< [ P.x, P.y, P.z, N.x, N.y, N.z, T.x, T.y, T.x, T.y ]
    PxyzNxyzBxyzTuv,    ///< [ P.x, P.y, P.z, N.x, N.y, N.z, B.x, B.y, B.z, T.x, T.y ]
    PxyzNxyzBxyzTuvTuv, ///< [ P.x, P.y, P.z, N.x, N.y, N.z, B.x, B.y, B.z, T.x, T.y, T.x, T.y ]
};

/** Vertex attribute stride value.
    Represented in number of floats
*/
const size_t AttrStrides[] = {
    8,  ///< PxyzNxyzTuv
    10, ///< PxyzNxyzTuvTuv
    11, ///< PxyzNxyzBxyzTuv
    13, ///< PxyzNxyzBxyzTuvTuv
};

/// Mesh region material type
enum class eShadingNode : uint32_t {
    Diffuse,
    Glossy,
    Refractive,
    Emissive,
    Mix,
    Transparent,
    Principled
};

/// Shading node descriptor struct
struct shading_node_desc_t {
    eShadingNode type;                                         ///< Material type
    float base_color[3] = {1, 1, 1};                           ///< Base color
    TextureHandle base_texture = InvalidTextureHandle;         ///< Base texture index
    TextureHandle normal_map = InvalidTextureHandle;           ///< Normal map index
    float normal_map_intensity = 1.0f;                         ///< Normal map intensity
    MaterialHandle mix_materials[2] = {InvalidMaterialHandle}; ///< Indices for two materials for mixing
    float roughness = 0;
    TextureHandle roughness_texture = InvalidTextureHandle;
    float anisotropic = 0;          ///<
    float anisotropic_rotation = 0; ///<
    float sheen = 0;
    float specular = 0;
    float strength = 1; ///< Strength of emissive material
    float fresnel = 1;  ///< Fresnel factor of mix material
    float ior = 1;      ///< IOR for reflective or refractive material
    float tint = 0;
    TextureHandle metallic_texture = InvalidTextureHandle;
    bool multiple_importance = false; ///< Enable explicit emissive geometry sampling
    bool mix_add = false;
};

/// Printcipled material descriptor struct (metallicness workflow)
struct principled_mat_desc_t {
    float base_color[3] = {1, 1, 1};
    TextureHandle base_texture = InvalidTextureHandle;
    float metallic = 0;
    TextureHandle metallic_texture = InvalidTextureHandle;
    float specular = 0.5f;
    TextureHandle specular_texture = InvalidTextureHandle;
    float specular_tint = 0;
    float roughness = 0.5f;
    TextureHandle roughness_texture = InvalidTextureHandle;
    float anisotropic = 0;
    float anisotropic_rotation = 0;
    float sheen = 0;
    float sheen_tint = 0.5f;
    float clearcoat = 0.0f;
    float clearcoat_roughness = 0.0f;
    float ior = 1.45f;
    float transmission = 0.0f;
    float transmission_roughness = 0.0f;
    float emission_color[3] = {0, 0, 0};
    TextureHandle emission_texture = InvalidTextureHandle;
    float emission_strength = 0;
    float alpha = 1.0f;
    TextureHandle alpha_texture = InvalidTextureHandle;
    TextureHandle normal_map = InvalidTextureHandle;
    float normal_map_intensity = 1.0f;
};

/// Defines mesh region with specific material
struct shape_desc_t {
    MaterialHandle front_mat; ///< Index of material
    MaterialHandle back_mat;  ///< Index of material applied for back faces
    size_t vtx_start;         ///< Vertex start index
    size_t vtx_count;         ///< Vertex count

    shape_desc_t(const MaterialHandle _front_material, const MaterialHandle _back_material, size_t _vtx_start,
                 size_t _vtx_count)
        : front_mat(_front_material), back_mat(_back_material), vtx_start(_vtx_start), vtx_count(_vtx_count) {}

    shape_desc_t(const MaterialHandle _front_material, size_t _vtx_start, size_t _vtx_count)
        : front_mat(_front_material), back_mat(InvalidMaterialHandle), vtx_start(_vtx_start), vtx_count(_vtx_count) {}
};

/// Mesh description
struct mesh_desc_t {
    const char *name = nullptr;        ///< Mesh name (for debugging)
    ePrimType prim_type;               ///< Primitive type
    eVertexLayout layout;              ///< Vertex attribute layout
    const float *vtx_attrs;            ///< Pointer to vertex attribute
    size_t vtx_attrs_count;            ///< Vertex attribute count (number of vertices)
    const uint32_t *vtx_indices;       ///< Pointer to vertex indices, defining primitive
    size_t vtx_indices_count;          ///< Primitive indices count
    int base_vertex = 0;               ///< Shift applied to indices
    std::vector<shape_desc_t> shapes;  ///< Vector of shapes in mesh
    bool allow_spatial_splits = false; ///< Better BVH, worse load times and memory consumption
    bool use_fast_bvh_build = false;   ///< Use faster BVH construction with less tree quality
};

enum eTextureFormat { RGBA8888, RGB888, RG88, R8 };

/// Texture description
struct tex_desc_t {
    eTextureFormat format;
    const char *name = nullptr; ///< Debug name
    const void *data;
    int w, ///< Texture width
        h; ///< Texture height
    bool is_srgb = true;
    bool is_normalmap = false;
    bool force_no_compression = false; ///< Make sure texture will have the best quality
    bool generate_mipmaps = false;
};

enum eLightType { SphereLight, SpotLight, DirectionalLight, LineLight, RectLight };

// Light description
struct directional_light_desc_t {
    float color[3];
    float direction[3], angle;
    bool cast_shadow = true;
};

struct sphere_light_desc_t {
    float color[3] = {1.0f, 1.0f, 1.0f};
    float position[3] = {0.0f, 0.0f, 0.0f};
    float radius = 1.0f;
    bool visible = true; // visibility for secondary bounces
    bool cast_shadow = true;
};

struct spot_light_desc_t {
    float color[3] = {1.0f, 1.0f, 1.0f};
    float position[3] = {0.0f, 0.0f, 0.0f};
    float direction[3] = {0.0f, -1.0f, 0.0f};
    float spot_size = 45.0f;
    float spot_blend = 0.15f;
    float radius = 1.0f;
    bool visible = true; // visibility for secondary bounces
    bool cast_shadow = true;
};

struct rect_light_desc_t {
    float color[3] = {1.0f, 1.0f, 1.0f};
    float width = 1.0f, height = 1.0f;
    bool sky_portal = false;
    bool visible = true; // visibility for secondary bounces
    bool cast_shadow = true;
};

struct disk_light_desc_t {
    float color[3] = {1.0f, 1.0f, 1.0f};
    float size_x = 1.0f, size_y = 1.0f;
    bool sky_portal = false;
    bool visible = true; // visibility for secondary bounces
    bool cast_shadow = true;
};

struct line_light_desc_t {
    float color[3] = {1.0f, 1.0f, 1.0f};
    float radius = 1.0f, height = 1.0f;
    bool sky_portal = false;
    bool visible = true; // visibility for secondary bounces
    bool cast_shadow = true;
};

// Camera description
struct camera_desc_t {
    eCamType type = eCamType::Persp;        ///< Type of projection
    eFilterType filter = eFilterType::Tent; ///< Reconstruction filter
    eDeviceType dtype = eDeviceType::SRGB;  ///< Device type
    eLensUnits ltype = eLensUnits::FOV;     ///< Lens units type
    float origin[3] = {};                   ///< Camera origin
    float fwd[3] = {};                      ///< Camera forward unit vector
    float up[3] = {};                       ///< Camera up vector (optional)
    float shift[2] = {};                    ///< Camera shift
    float exposure = 0.0f;                  ///< Camera exposure in stops (output = value * (2 ^ exposure))
    float fov = 45.0f, gamma = 1.0f;        ///< Field of view in degrees, gamma
    float sensor_height = 0.036f;           ///< Camera sensor height
    float focus_distance = 1.0f;            ///< Distance to focus point
    float focal_length = 0.0f;              ///< Focal length
    float fstop = 0.0f;                     ///< Focal fstop
    float lens_rotation = 0.0f;             ///< Bokeh rotation
    float lens_ratio = 1.0f;                ///< Bokeh distortion
    int lens_blades = 0;                    ///< Bokeh shape
    float clip_start = 0;                   ///< Clip start
    float clip_end = 3.402823466e+30F;      ///< Clip end
    uint32_t mi_index = 0xffffffff,         ///< Index of mesh instance
        uv_index = 0;                       ///< UV layer used by geometry cam
    bool lighting_only = false;             ///< Render lightmap only
    bool skip_direct_lighting = false;      ///< Render indirect light contribution only
    bool skip_indirect_lighting = false;    ///< Render direct light contribution only
    bool no_background = false;             ///< Do not render background
    bool clamp = false;                     ///< Clamp color values to [0..1] range
    bool output_sh = false;                 ///< Output 2-band (4 coeff) spherical harmonics data
    bool output_base_color = false;         ///< Output float RGB material base color
    bool output_depth_normals = false;      ///< Output smooth normals and depth
    uint8_t max_diff_depth = 4;             ///< Maximum tracing depth of diffuse rays
    uint8_t max_spec_depth = 8;             ///< Maximum tracing depth of glossy rays
    uint8_t max_refr_depth = 8;             ///< Maximum tracing depth of glossy rays
    uint8_t max_transp_depth = 8; ///< Maximum tracing depth of transparency rays (note: does not obey total depth)
    uint8_t max_total_depth = 8;  ///< Maximum tracing depth of all rays (except transparency)
    uint8_t min_total_depth = 2;  ///< Depth after which random rays termination starts
    uint8_t min_transp_depth = 2; ///< Depth after which random rays termination starts
};

/// Environment description
struct environment_desc_t {
    float env_col[3] = {};                         ///< Environment color
    TextureHandle env_map = InvalidTextureHandle;  ///< Environment texture
    float back_col[3] = {};                        ///< Background color
    TextureHandle back_map = InvalidTextureHandle; ///< Background texture
    float env_map_rotation = 0.0f;                 ///< Environment map rotation in radians
    float back_map_rotation = 0.0f;                ///< Background map rotation in radians
    bool multiple_importance = true;               ///< Enable explicit env map sampling
};

/** Base Scene class,
    cpu and gpu backends have different implementation of SceneBase
*/
class SceneBase {
  protected:
    struct cam_storage_t {
        camera_t cam;
        CameraHandle next_free;
    };

    mutable std::shared_timed_mutex mtx_;

    std::vector<cam_storage_t> cams_;                   ///< scene cameras
    CameraHandle cam_first_free_ = InvalidCameraHandle; ///< index to first free cam in cams_ array

    CameraHandle current_cam_ = InvalidCameraHandle; ///< index of current camera

    void SetCamera_nolock(CameraHandle i, const camera_desc_t &c);

  public:
    virtual ~SceneBase() = default;

    /// Get current environment description
    virtual void GetEnvironment(environment_desc_t &env) = 0;

    /// Set environment from description
    virtual void SetEnvironment(const environment_desc_t &env) = 0;

    /** @brief Adds texture to scene
        @param t texture description
        @return New texture handle
    */
    virtual TextureHandle AddTexture(const tex_desc_t &t) = 0;

    /** @brief Removes texture with specific index from scene
        @param t texture handle
    */
    virtual void RemoveTexture(TextureHandle t) = 0;

    /** @brief Adds material to scene
        @param m root shading node description
        @return New material handle
    */
    virtual MaterialHandle AddMaterial(const shading_node_desc_t &m) = 0;

    /** @brief Adds material to scene
        @param m principled material description
        @return New material handle
    */
    virtual MaterialHandle AddMaterial(const principled_mat_desc_t &m) = 0;

    /** @brief Removes material with specific index from scene
        @param m material handle
    */
    virtual void RemoveMaterial(MaterialHandle m) = 0;

    /** @brief Adds mesh to scene
        @param m mesh description
        @return New mesh index
    */
    virtual MeshHandle AddMesh(const mesh_desc_t &m) = 0;

    /** @brief Removes mesh with specific index from scene
        @param i mesh index
    */
    virtual void RemoveMesh(MeshHandle m) = 0;

    /** @brief Adds light to scene
        @param l light description
        @return New light index
    */
    virtual LightHandle AddLight(const directional_light_desc_t &l) = 0;
    virtual LightHandle AddLight(const sphere_light_desc_t &l) = 0;
    virtual LightHandle AddLight(const spot_light_desc_t &l) = 0;
    virtual LightHandle AddLight(const rect_light_desc_t &l, const float *xform) = 0;
    virtual LightHandle AddLight(const disk_light_desc_t &l, const float *xform) = 0;
    virtual LightHandle AddLight(const line_light_desc_t &l, const float *xform) = 0;

    /** @brief Removes light with specific index from scene
        @param l light handle
    */
    virtual void RemoveLight(LightHandle l) = 0;

    /** @brief Adds mesh instance to a scene
        @param mesh mesh handle
        @param xform array of 16 floats holding transformation matrix
        @return New mesh instance handle
    */
    virtual MeshInstanceHandle AddMeshInstance(MeshHandle mesh, const float *xform) = 0;

    /** @brief Sets mesh instance transformation
        @param mi mesh instance handle
        @param xform array of 16 floats holding transformation matrix
    */
    virtual void SetMeshInstanceTransform(MeshInstanceHandle mi, const float *xform) = 0;

    /** @brief Removes mesh instance from scene
        @param mi mesh instance handle

        Removes mesh instance from scene. Associated mesh remains loaded in scene even if
        there is no instances of this mesh left.
    */
    virtual void RemoveMeshInstance(MeshInstanceHandle mi) = 0;

    virtual void Finalize() = 0;

    /** @brief Adds camera to a scene
        @param c camera description
        @return New camera handle
    */
    CameraHandle AddCamera(const camera_desc_t &c);

    /** @brief Get camera description
        @param i camera handle
    */
    void GetCamera(CameraHandle i, camera_desc_t &c) const;

    /** @brief Sets camera properties
        @param i camera handle
        @param c camera description
    */
    void SetCamera(CameraHandle i, const camera_desc_t &c) {
        std::unique_lock<std::shared_timed_mutex> lock(mtx_);
        SetCamera_nolock(i, c);
    }

    /** @brief Removes camera with specific index from scene
        @param i camera handle

        Removes camera with specific index from scene. Other cameras indices remain valid.
    */
    void RemoveCamera(CameraHandle i);

    /** @brief Get const reference to a camera with specific index
        @return Current camera index
    */
    CameraHandle current_cam() const {
        std::shared_lock<std::shared_timed_mutex> lock(mtx_);
        return current_cam_;
    }

    /** @brief Sets camera with specific index to be current
        @param i camera index
    */
    void set_current_cam(CameraHandle i) {
        std::unique_lock<std::shared_timed_mutex> lock(mtx_);
        current_cam_ = i;
    }

    /// Overall triangle count in scene
    virtual uint32_t triangle_count() const = 0;

    /// Overall BVH node count in scene
    virtual uint32_t node_count() const = 0;
};
} // namespace Ray
