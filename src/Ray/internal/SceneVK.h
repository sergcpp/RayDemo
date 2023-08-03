#pragma once

#include "Vk/AccStructureVK.h"
#include "Vk/DescriptorPoolVK.h"
#include "Vk/SparseStorageVK.h"
#include "Vk/TextureAtlasVK.h"
#include "Vk/TextureVK.h"
#include "Vk/VectorVK.h"

namespace Ray {
namespace Vk {
class Context;
class Renderer;

struct BindlessTexData {
    DescrPool descr_pool;
    VkDescriptorSetLayout descr_layout = {}, rt_descr_layout = {};
    VkDescriptorSet descr_set = {}, rt_descr_set = {};
    Sampler shared_sampler;

    explicit BindlessTexData(Context *ctx) : descr_pool(ctx) {}
};

} // namespace Vk
} // namespace Ray

#define NS Vk
#include "SceneGPU.h"
#undef NS
