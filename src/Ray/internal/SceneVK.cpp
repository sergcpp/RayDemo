#include "SceneVK.h"

#include <cassert>

#ifdef __GNUC__
#define force_inline __attribute__((always_inline)) inline
#endif
#ifdef _MSC_VER
#define force_inline __forceinline
#endif

#include "BVHSplit.h"
#include "TextureParams.h"
#include "TextureUtils.h"
#include "Vk/ContextVK.h"

namespace Ray {
uint32_t next_power_of_two(uint32_t v);

void to_khr_xform(const float xform[16], float matrix[3][4]) {
    // transpose
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 4; ++j) {
            matrix[i][j] = xform[4 * j + i];
        }
    }
}

namespace Vk {
VkDeviceSize align_up(const VkDeviceSize size, const VkDeviceSize alignment) {
    return (size + alignment - 1) & ~(alignment - 1);
}
} // namespace Vk
} // namespace Ray

Ray::Vk::Scene::~Scene() {
    std::unique_lock<std::shared_timed_mutex> lock(mtx_);

    for (auto it = mesh_instances_.begin(); it != mesh_instances_.end();) {
        MeshInstanceHandle to_delete = {it.index(), it.block()};
        ++it;
        Scene::RemoveMeshInstance_nolock(to_delete);
    }
    for (auto it = meshes_.begin(); it != meshes_.end();) {
        MeshHandle to_delete = {it.index(), it.block()};
        ++it;
        Scene::RemoveMesh_nolock(to_delete);
    }

    if (macro_nodes_root_ != 0xffffffff) {
        nodes_.Erase(macro_nodes_block_);
        macro_nodes_root_ = macro_nodes_block_ = 0xffffffff;
    }

    bindless_textures_.clear();
    ctx_->api().vkDestroyDescriptorSetLayout(ctx_->device(), bindless_tex_data_.descr_layout, nullptr);
    ctx_->api().vkDestroyDescriptorSetLayout(ctx_->device(), bindless_tex_data_.rt_descr_layout, nullptr);
}

void Ray::Vk::Scene::GenerateTextureMips_nolock() {
    struct mip_gen_info {
        uint32_t texture_index;
        uint16_t size; // used for sorting
        uint8_t dst_mip;
        uint8_t atlas_index; // used for sorting
    };

    std::vector<mip_gen_info> mips_to_generate;
    mips_to_generate.reserve(atlas_textures_.size());

    for (uint32_t i = 0; i < uint32_t(atlas_textures_.size()); ++i) {
        const atlas_texture_t &t = atlas_textures_[i];
        if ((t.height & ATLAS_TEX_MIPS_BIT) == 0 || IsCompressedFormat(tex_atlases_[t.atlas].format())) {
            continue;
        }

        int mip = 0;
        int res[2] = {(t.width & ATLAS_TEX_WIDTH_BITS), (t.height & ATLAS_TEX_HEIGHT_BITS)};

        res[0] /= 2;
        res[1] /= 2;
        ++mip;

        while (res[0] >= 1 && res[1] >= 1) {
            const bool requires_generation =
                t.page[mip] == t.page[0] && t.pos[mip][0] == t.pos[0][0] && t.pos[mip][1] == t.pos[0][1];
            if (requires_generation) {
                mips_to_generate.emplace_back();
                auto &m = mips_to_generate.back();
                m.texture_index = i;
                m.size = std::max(res[0], res[1]);
                m.dst_mip = mip;
                m.atlas_index = t.atlas;
            }

            res[0] /= 2;
            res[1] /= 2;
            ++mip;
        }
    }

    // Sort for more optimal allocation
    sort(begin(mips_to_generate), end(mips_to_generate), [](const mip_gen_info &lhs, const mip_gen_info &rhs) {
        if (lhs.atlas_index == rhs.atlas_index) {
            return lhs.size > rhs.size;
        }
        return lhs.atlas_index < rhs.atlas_index;
    });

    for (const mip_gen_info &info : mips_to_generate) {
        atlas_texture_t t = atlas_textures_[info.texture_index];

        const int dst_mip = info.dst_mip;
        const int src_mip = dst_mip - 1;
        const int src_res[2] = {(t.width & ATLAS_TEX_WIDTH_BITS) >> src_mip,
                                (t.height & ATLAS_TEX_HEIGHT_BITS) >> src_mip};
        assert(src_res[0] != 0 && src_res[1] != 0);

        const int src_pos[2] = {t.pos[src_mip][0] + 1, t.pos[src_mip][1] + 1};

        int pos[2];
        const int page = tex_atlases_[t.atlas].DownsampleRegion(t.page[src_mip], src_pos, src_res, pos);
        if (page == -1) {
            log_->Error("Failed to allocate texture!");
            break;
        }

        t.page[dst_mip] = uint8_t(page);
        t.pos[dst_mip][0] = uint16_t(pos[0]);
        t.pos[dst_mip][1] = uint16_t(pos[1]);

        if (src_res[0] == 1 || src_res[1] == 1) {
            // fill remaining mip levels with the last one
            for (int i = dst_mip + 1; i < NUM_MIP_LEVELS; i++) {
                t.page[i] = t.page[dst_mip];
                t.pos[i][0] = t.pos[dst_mip][0];
                t.pos[i][1] = t.pos[dst_mip][1];
            }
        }

        atlas_textures_.Set(info.texture_index, t);
    }

    log_->Info("Ray: Atlasses are (RGBA[%i], RGB[%i], RG[%i], R[%i], BC3[%i], BC4[%i], BC5[%i])",
               tex_atlases_[0].page_count(), tex_atlases_[1].page_count(), tex_atlases_[2].page_count(),
               tex_atlases_[3].page_count(), tex_atlases_[4].page_count(), tex_atlases_[5].page_count(),
               tex_atlases_[6].page_count());
}

void Ray::Vk::Scene::PrepareBindlessTextures_nolock() {
    assert(bindless_textures_.capacity() <= ctx_->max_sampled_images());

    DescrSizes descr_sizes;
    descr_sizes.img_count = ctx_->max_sampled_images();

    { // Init shared sampler
        SamplingParams params;
        params.filter = eTexFilter::Nearest;
        params.wrap = eTexWrap::Repeat;

        bindless_tex_data_.shared_sampler.Init(ctx_, params);
    }

    const bool bres = bindless_tex_data_.descr_pool.Init(descr_sizes, 2 /* sets_count */);
    if (!bres) {
        log_->Error("Failed to init descriptor pool!");
    }

    if (!bindless_tex_data_.descr_layout) {
        VkDescriptorSetLayoutBinding textures_binding = {};
        textures_binding.binding = 0;
        textures_binding.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
        textures_binding.descriptorCount = ctx_->max_sampled_images();
        textures_binding.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

        VkDescriptorSetLayoutCreateInfo layout_info = {VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
        layout_info.bindingCount = 1;
        layout_info.pBindings = &textures_binding;

        VkDescriptorBindingFlagsEXT bind_flag = VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT_EXT;

        VkDescriptorSetLayoutBindingFlagsCreateInfoEXT extended_info = {
            VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO_EXT};
        extended_info.bindingCount = 1u;
        extended_info.pBindingFlags = &bind_flag;
        layout_info.pNext = &extended_info;

        const VkResult res = ctx_->api().vkCreateDescriptorSetLayout(ctx_->device(), &layout_info, nullptr,
                                                                     &bindless_tex_data_.descr_layout);
        if (res != VK_SUCCESS) {
            log_->Error("Failed to create descriptor set layout!");
        }
    }

    if (!bindless_tex_data_.rt_descr_layout) {
        VkDescriptorSetLayoutBinding textures_binding = {};
        textures_binding.binding = 0;
        textures_binding.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
        textures_binding.descriptorCount = ctx_->max_sampled_images();
        textures_binding.stageFlags = VK_SHADER_STAGE_RAYGEN_BIT_KHR;

        VkDescriptorSetLayoutCreateInfo layout_info = {VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
        layout_info.bindingCount = 1;
        layout_info.pBindings = &textures_binding;

        VkDescriptorBindingFlagsEXT bind_flag = VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT_EXT;

        VkDescriptorSetLayoutBindingFlagsCreateInfoEXT extended_info = {
            VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO_EXT};
        extended_info.bindingCount = 1u;
        extended_info.pBindingFlags = &bind_flag;
        layout_info.pNext = &extended_info;

        const VkResult res = ctx_->api().vkCreateDescriptorSetLayout(ctx_->device(), &layout_info, nullptr,
                                                                     &bindless_tex_data_.rt_descr_layout);
        if (res != VK_SUCCESS) {
            log_->Error("Failed to create descriptor set layout!");
        }
    }

    bindless_tex_data_.descr_pool.Reset();
    bindless_tex_data_.descr_set = bindless_tex_data_.descr_pool.Alloc(bindless_tex_data_.descr_layout);
    bindless_tex_data_.rt_descr_set = bindless_tex_data_.descr_pool.Alloc(bindless_tex_data_.rt_descr_layout);

    bindless_tex_data_.tex_sizes = Buffer{"Texture sizes", ctx_, eBufType::Storage,
                                          uint32_t(std::max(1u, bindless_textures_.capacity()) * sizeof(uint32_t))};
    Buffer tex_sizes_stage = Buffer{"Texture sizes Stage", ctx_, eBufType::Upload,
                                    uint32_t(std::max(1u, bindless_textures_.capacity()) * sizeof(uint32_t))};

    { // Transition resources
        std::vector<TransitionInfo> img_transitions;
        img_transitions.reserve(bindless_textures_.size());

        for (const auto &tex : bindless_textures_) {
            img_transitions.emplace_back(&tex, eResState::ShaderResource);
        }

        CommandBuffer cmd_buf = BegSingleTimeCommands(ctx_->api(), ctx_->device(), ctx_->temp_command_pool());
        TransitionResourceStates(cmd_buf, AllStages, AllStages, img_transitions);
        EndSingleTimeCommands(ctx_->api(), ctx_->device(), ctx_->graphics_queue(), cmd_buf, ctx_->temp_command_pool());
    }

    uint32_t *p_tex_sizes = reinterpret_cast<uint32_t *>(tex_sizes_stage.Map());
    memset(p_tex_sizes, 0, bindless_tex_data_.tex_sizes.size());

    for (auto it = bindless_textures_.begin(); it != bindless_textures_.end(); ++it) {
        const Texture2D &tex = bindless_textures_[it.index()];

        { // Update descriptor
            VkDescriptorImageInfo img_info = tex.vk_desc_image_info();

            VkWriteDescriptorSet descr_write = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
            descr_write.dstSet = bindless_tex_data_.descr_set;
            descr_write.dstBinding = 0;
            descr_write.dstArrayElement = it.index();
            descr_write.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
            descr_write.descriptorCount = 1;
            descr_write.pBufferInfo = nullptr;
            descr_write.pImageInfo = &img_info;
            descr_write.pTexelBufferView = nullptr;
            descr_write.pNext = nullptr;

            ctx_->api().vkUpdateDescriptorSets(ctx_->device(), 1, &descr_write, 0, nullptr);

            descr_write.dstSet = bindless_tex_data_.rt_descr_set;

            ctx_->api().vkUpdateDescriptorSets(ctx_->device(), 1, &descr_write, 0, nullptr);
        }

        p_tex_sizes[it.index()] = (uint32_t(tex.params.w) << 16) | tex.params.h;
    }

    tex_sizes_stage.Unmap();

    CommandBuffer cmd_buf = BegSingleTimeCommands(ctx_->api(), ctx_->device(), ctx_->temp_command_pool());
    CopyBufferToBuffer(tex_sizes_stage, 0, bindless_tex_data_.tex_sizes, 0, bindless_tex_data_.tex_sizes.size(),
                       cmd_buf);

    TransitionInfo trans(&bindless_tex_data_.tex_sizes, eResState::ShaderResource);
    TransitionResourceStates(cmd_buf, AllStages, AllStages, {&trans, 1});

    EndSingleTimeCommands(ctx_->api(), ctx_->device(), ctx_->graphics_queue(), cmd_buf, ctx_->temp_command_pool());
}

void Ray::Vk::Scene::RebuildHWAccStructures_nolock() {
    if (!use_hwrt_) {
        return;
    }

    static const VkDeviceSize AccStructAlignment = 256;

    struct Blas {
        SmallVector<VkAccelerationStructureGeometryKHR, 16> geometries;
        SmallVector<VkAccelerationStructureBuildRangeInfoKHR, 16> build_ranges;
        SmallVector<uint32_t, 16> prim_counts;
        VkAccelerationStructureBuildSizesInfoKHR size_info = {};
        VkAccelerationStructureBuildGeometryInfoKHR build_info = {};
    };
    std::vector<Blas> all_blases;
    std::vector<uint32_t> mesh_to_blas(meshes_.capacity(), 0xffffffff);

    uint32_t needed_build_scratch_size = 0;
    uint32_t needed_total_acc_struct_size = 0;

    for (auto it = meshes_.cbegin(); it != meshes_.cend(); ++it) {
        const mesh_t &mesh = *it;

        mesh_to_blas[it.index()] = uint32_t(all_blases.size());

        VkAccelerationStructureGeometryTrianglesDataKHR tri_data = {
            VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR};
        tri_data.vertexFormat = VK_FORMAT_R32G32B32_SFLOAT;
        tri_data.vertexData.deviceAddress = vertices_.gpu_buf().vk_device_address();
        tri_data.vertexStride = sizeof(vertex_t);
        tri_data.indexType = VK_INDEX_TYPE_UINT32;
        tri_data.indexData.deviceAddress = vtx_indices_.gpu_buf().vk_device_address();
        // TODO: fix this!
        tri_data.maxVertex = mesh.vert_index + mesh.vert_count;

        //
        // Gather geometries
        //
        all_blases.emplace_back();
        Blas &new_blas = all_blases.back();

        {
            auto &new_geo = new_blas.geometries.emplace_back();
            new_geo = {VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR};
            new_geo.geometryType = VK_GEOMETRY_TYPE_TRIANGLES_KHR;
            new_geo.flags = 0;
            // if ((mat_flags & uint32_t(Ren::eMatFlags::AlphaTest)) == 0) {
            //     new_geo.flags |= VK_GEOMETRY_OPAQUE_BIT_KHR;
            // }
            new_geo.geometry.triangles = tri_data;

            auto &new_range = new_blas.build_ranges.emplace_back();
            new_range.firstVertex = 0; // mesh.vert_index;
            new_range.primitiveCount = mesh.vert_count / 3;
            new_range.primitiveOffset = mesh.vert_index * sizeof(uint32_t);
            new_range.transformOffset = 0;

            new_blas.prim_counts.push_back(new_range.primitiveCount);
        }

        //
        // Query needed memory
        //
        new_blas.build_info = {VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR};
        new_blas.build_info.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
        new_blas.build_info.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
        new_blas.build_info.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR |
                                    VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_COMPACTION_BIT_KHR;
        new_blas.build_info.geometryCount = uint32_t(new_blas.geometries.size());
        new_blas.build_info.pGeometries = new_blas.geometries.cdata();

        new_blas.size_info = {VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR};
        ctx_->api().vkGetAccelerationStructureBuildSizesKHR(
            ctx_->device(), VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &new_blas.build_info,
            new_blas.prim_counts.cdata(), &new_blas.size_info);

        // make sure we will not use this potentially stale pointer
        new_blas.build_info.pGeometries = nullptr;

        needed_build_scratch_size = std::max(needed_build_scratch_size, uint32_t(new_blas.size_info.buildScratchSize));
        needed_total_acc_struct_size +=
            uint32_t(align_up(new_blas.size_info.accelerationStructureSize, AccStructAlignment));

        rt_mesh_blases_.emplace_back();
    }

    if (!all_blases.empty()) {
        //
        // Allocate memory
        //
        Buffer scratch_buf("BLAS Scratch Buf", ctx_, eBufType::Storage, next_power_of_two(needed_build_scratch_size));
        VkDeviceAddress scratch_addr = scratch_buf.vk_device_address();

        Buffer acc_structs_buf("BLAS Before-Compaction Buf", ctx_, eBufType::AccStructure,
                               needed_total_acc_struct_size);

        //

        VkQueryPoolCreateInfo query_pool_create_info = {VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO};
        query_pool_create_info.queryCount = uint32_t(all_blases.size());
        query_pool_create_info.queryType = VK_QUERY_TYPE_ACCELERATION_STRUCTURE_COMPACTED_SIZE_KHR;

        VkQueryPool query_pool;
        VkResult res = ctx_->api().vkCreateQueryPool(ctx_->device(), &query_pool_create_info, nullptr, &query_pool);
        if (res != VK_SUCCESS) {
            log_->Error("Failed to create query pool!");
        }

        std::vector<AccStructure> blases_before_compaction;
        blases_before_compaction.resize(all_blases.size());

        { // Submit build commands
            VkDeviceSize acc_buf_offset = 0;
            CommandBuffer cmd_buf = BegSingleTimeCommands(ctx_->api(), ctx_->device(), ctx_->temp_command_pool());

            ctx_->api().vkCmdResetQueryPool(cmd_buf, query_pool, 0, uint32_t(all_blases.size()));

            for (int i = 0; i < int(all_blases.size()); ++i) {
                VkAccelerationStructureCreateInfoKHR acc_create_info = {
                    VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR};
                acc_create_info.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
                acc_create_info.buffer = acc_structs_buf.vk_handle();
                acc_create_info.offset = acc_buf_offset;
                acc_create_info.size = all_blases[i].size_info.accelerationStructureSize;
                acc_buf_offset += align_up(acc_create_info.size, AccStructAlignment);

                VkAccelerationStructureKHR acc_struct;
                VkResult res = ctx_->api().vkCreateAccelerationStructureKHR(ctx_->device(), &acc_create_info, nullptr,
                                                                            &acc_struct);
                if (res != VK_SUCCESS) {
                    log_->Error("Failed to create acceleration structure!");
                }

                auto &acc = blases_before_compaction[i];
                if (!acc.Init(ctx_, acc_struct)) {
                    log_->Error("Failed to init BLAS!");
                }

                all_blases[i].build_info.pGeometries = all_blases[i].geometries.cdata();

                all_blases[i].build_info.dstAccelerationStructure = acc_struct;
                all_blases[i].build_info.scratchData.deviceAddress = scratch_addr;

                const VkAccelerationStructureBuildRangeInfoKHR *build_ranges = all_blases[i].build_ranges.cdata();
                ctx_->api().vkCmdBuildAccelerationStructuresKHR(cmd_buf, 1, &all_blases[i].build_info, &build_ranges);

                { // Place barrier
                    VkMemoryBarrier scr_buf_barrier = {VK_STRUCTURE_TYPE_MEMORY_BARRIER};
                    scr_buf_barrier.srcAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR;
                    scr_buf_barrier.dstAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR;

                    ctx_->api().vkCmdPipelineBarrier(cmd_buf, VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
                                                     VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR, 0, 1,
                                                     &scr_buf_barrier, 0, nullptr, 0, nullptr);
                }

                ctx_->api().vkCmdWriteAccelerationStructuresPropertiesKHR(
                    cmd_buf, 1, &all_blases[i].build_info.dstAccelerationStructure,
                    VK_QUERY_TYPE_ACCELERATION_STRUCTURE_COMPACTED_SIZE_KHR, query_pool, i);
            }

            EndSingleTimeCommands(ctx_->api(), ctx_->device(), ctx_->graphics_queue(), cmd_buf,
                                  ctx_->temp_command_pool());
        }

        std::vector<VkDeviceSize> compact_sizes(all_blases.size());
        res = ctx_->api().vkGetQueryPoolResults(ctx_->device(), query_pool, 0, uint32_t(all_blases.size()),
                                                all_blases.size() * sizeof(VkDeviceSize), compact_sizes.data(),
                                                sizeof(VkDeviceSize), VK_QUERY_RESULT_WAIT_BIT);
        assert(res == VK_SUCCESS);

        ctx_->api().vkDestroyQueryPool(ctx_->device(), query_pool, nullptr);

        VkDeviceSize total_compacted_size = 0;
        for (int i = 0; i < int(compact_sizes.size()); ++i) {
            total_compacted_size += align_up(compact_sizes[i], AccStructAlignment);
        }

        rt_blas_buf_ =
            Buffer{"BLAS After-Compaction Buf", ctx_, eBufType::AccStructure, uint32_t(total_compacted_size)};

        { // Submit compaction commands
            VkDeviceSize compact_acc_buf_offset = 0;
            CommandBuffer cmd_buf = BegSingleTimeCommands(ctx_->api(), ctx_->device(), ctx_->temp_command_pool());

            for (int i = 0; i < int(all_blases.size()); ++i) {
                VkAccelerationStructureCreateInfoKHR acc_create_info = {
                    VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR};
                acc_create_info.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
                acc_create_info.buffer = rt_blas_buf_.vk_handle();
                acc_create_info.offset = compact_acc_buf_offset;
                acc_create_info.size = compact_sizes[i];
                assert(compact_acc_buf_offset + compact_sizes[i] <= total_compacted_size);
                compact_acc_buf_offset += align_up(acc_create_info.size, AccStructAlignment);

                VkAccelerationStructureKHR compact_acc_struct;
                const VkResult res = ctx_->api().vkCreateAccelerationStructureKHR(ctx_->device(), &acc_create_info,
                                                                                  nullptr, &compact_acc_struct);
                if (res != VK_SUCCESS) {
                    log_->Error("Failed to create acceleration structure!");
                }

                VkCopyAccelerationStructureInfoKHR copy_info = {VK_STRUCTURE_TYPE_COPY_ACCELERATION_STRUCTURE_INFO_KHR};
                copy_info.mode = VK_COPY_ACCELERATION_STRUCTURE_MODE_COMPACT_KHR;
                copy_info.src = blases_before_compaction[i].vk_handle();
                copy_info.dst = compact_acc_struct;

                ctx_->api().vkCmdCopyAccelerationStructureKHR(cmd_buf, &copy_info);

                auto &vk_blas = rt_mesh_blases_[i].acc;
                if (!vk_blas.Init(ctx_, compact_acc_struct)) {
                    log_->Error("Blas compaction failed!");
                }
            }

            EndSingleTimeCommands(ctx_->api(), ctx_->device(), ctx_->graphics_queue(), cmd_buf,
                                  ctx_->temp_command_pool());

            for (auto &b : blases_before_compaction) {
                b.FreeImmediate();
            }
            acc_structs_buf.FreeImmediate();
            scratch_buf.FreeImmediate();
        }
    }

    //
    // Build TLAS
    //

    struct RTGeoInstance {
        uint32_t indices_start;
        uint32_t vertices_start;
        uint32_t material_index;
        uint32_t flags;
    };
    static_assert(sizeof(RTGeoInstance) == 16, "!");

    std::vector<RTGeoInstance> geo_instances;
    std::vector<VkAccelerationStructureInstanceKHR> tlas_instances;

    for (auto it = mesh_instances_.cbegin(); it != mesh_instances_.cend(); ++it) {
        const mesh_instance_t &instance = *it;

        auto &blas = rt_mesh_blases_[mesh_to_blas[instance.mesh_index]];
        blas.geo_index = uint32_t(geo_instances.size());
        blas.geo_count = 0;

        auto &vk_blas = blas.acc;

        tlas_instances.emplace_back();
        auto &new_instance = tlas_instances.back();
        to_khr_xform(transforms_[instance.tr_index].xform, new_instance.transform.matrix);
        new_instance.instanceCustomIndex = meshes_[instance.mesh_index].vert_index / 3;
        // blas.geo_index;
        new_instance.mask = instance.ray_visibility;
        new_instance.instanceShaderBindingTableRecordOffset = 0;
        new_instance.flags = 0;
        // VK_GEOMETRY_INSTANCE_TRIANGLE_FRONT_COUNTERCLOCKWISE_BIT_KHR; //
        // VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR;
        new_instance.accelerationStructureReference = static_cast<uint64_t>(vk_blas.vk_device_address());

        // const mesh_t &mesh = meshes_[instance.mesh_index];
        {
            ++blas.geo_count;

            geo_instances.emplace_back();
            auto &geo = geo_instances.back();
            geo.indices_start = 0;  // mesh.
            geo.vertices_start = 0; // acc.mesh->attribs_buf1().offset / 16;
            geo.material_index = 0; // grp.mat.index();
            geo.flags = 0;
        }
    }

    if (geo_instances.empty()) {
        geo_instances.emplace_back();
        auto &dummy_geo = geo_instances.back();
        dummy_geo = {};

        tlas_instances.emplace_back();
        auto &dummy_instance = tlas_instances.back();
        dummy_instance = {};
    }

    rt_geo_data_buf_ =
        Buffer{"RT Geo Data Buf", ctx_, eBufType::Storage, uint32_t(geo_instances.size() * sizeof(RTGeoInstance))};
    Buffer geo_data_stage_buf{"RT Geo Data Stage Buf", ctx_, eBufType::Upload,
                              uint32_t(geo_instances.size() * sizeof(RTGeoInstance))};
    {
        uint8_t *geo_data_stage = geo_data_stage_buf.Map();
        memcpy(geo_data_stage, geo_instances.data(), geo_instances.size() * sizeof(RTGeoInstance));
        geo_data_stage_buf.Unmap();
    }

    rt_instance_buf_ = Buffer{"RT Instance Buf", ctx_, eBufType::Storage,
                              uint32_t(tlas_instances.size() * sizeof(VkAccelerationStructureInstanceKHR))};
    Buffer instance_stage_buf{"RT Instance Stage Buf", ctx_, eBufType::Upload,
                              uint32_t(tlas_instances.size() * sizeof(VkAccelerationStructureInstanceKHR))};
    {
        uint8_t *instance_stage = instance_stage_buf.Map();
        memcpy(instance_stage, tlas_instances.data(),
               tlas_instances.size() * sizeof(VkAccelerationStructureInstanceKHR));
        instance_stage_buf.Unmap();
    }

    VkDeviceAddress instance_buf_addr = rt_instance_buf_.vk_device_address();

    CommandBuffer cmd_buf = BegSingleTimeCommands(ctx_->api(), ctx_->device(), ctx_->temp_command_pool());

    CopyBufferToBuffer(geo_data_stage_buf, 0, rt_geo_data_buf_, 0, geo_data_stage_buf.size(), cmd_buf);
    CopyBufferToBuffer(instance_stage_buf, 0, rt_instance_buf_, 0, instance_stage_buf.size(), cmd_buf);

    { // Make sure compaction copying of BLASes has finished
        VkMemoryBarrier mem_barrier = {VK_STRUCTURE_TYPE_MEMORY_BARRIER};
        mem_barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        mem_barrier.dstAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR;

        ctx_->api().vkCmdPipelineBarrier(cmd_buf, VK_PIPELINE_STAGE_TRANSFER_BIT,
                                         VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR, 0, 1, &mem_barrier, 0,
                                         nullptr, 0, nullptr);
    }

    Buffer tlas_scratch_buf;

    { //
        VkAccelerationStructureGeometryInstancesDataKHR instances_data = {
            VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR};
        instances_data.data.deviceAddress = instance_buf_addr;

        VkAccelerationStructureGeometryKHR tlas_geo = {VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR};
        tlas_geo.geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR;
        tlas_geo.geometry.instances = instances_data;

        VkAccelerationStructureBuildGeometryInfoKHR tlas_build_info = {
            VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR};
        tlas_build_info.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR |
                                VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_UPDATE_BIT_KHR;
        tlas_build_info.geometryCount = 1;
        tlas_build_info.pGeometries = &tlas_geo;
        tlas_build_info.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
        tlas_build_info.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
        tlas_build_info.srcAccelerationStructure = VK_NULL_HANDLE;

        const auto instance_count = uint32_t(tlas_instances.size());
        const uint32_t max_instance_count = instance_count;

        VkAccelerationStructureBuildSizesInfoKHR size_info = {
            VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR};
        ctx_->api().vkGetAccelerationStructureBuildSizesKHR(ctx_->device(),
                                                            VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR,
                                                            &tlas_build_info, &max_instance_count, &size_info);

        rt_tlas_buf_ = Buffer{"TLAS Buf", ctx_, eBufType::AccStructure, uint32_t(size_info.accelerationStructureSize)};

        VkAccelerationStructureCreateInfoKHR create_info = {VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR};
        create_info.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
        create_info.buffer = rt_tlas_buf_.vk_handle();
        create_info.offset = 0;
        create_info.size = size_info.accelerationStructureSize;

        VkAccelerationStructureKHR tlas_handle;
        VkResult res =
            ctx_->api().vkCreateAccelerationStructureKHR(ctx_->device(), &create_info, nullptr, &tlas_handle);
        if (res != VK_SUCCESS) {
            log_->Error("[SceneManager::InitHWAccStructures]: Failed to create acceleration structure!");
        }

        tlas_scratch_buf = Buffer{"TLAS Scratch Buf", ctx_, eBufType::Storage, uint32_t(size_info.buildScratchSize)};
        VkDeviceAddress tlas_scratch_buf_addr = tlas_scratch_buf.vk_device_address();

        tlas_build_info.srcAccelerationStructure = VK_NULL_HANDLE;
        tlas_build_info.dstAccelerationStructure = tlas_handle;
        tlas_build_info.scratchData.deviceAddress = tlas_scratch_buf_addr;

        VkAccelerationStructureBuildRangeInfoKHR range_info = {};
        range_info.primitiveOffset = 0;
        range_info.primitiveCount = instance_count;
        range_info.firstVertex = 0;
        range_info.transformOffset = 0;

        const VkAccelerationStructureBuildRangeInfoKHR *build_range = &range_info;
        ctx_->api().vkCmdBuildAccelerationStructuresKHR(cmd_buf, 1, &tlas_build_info, &build_range);

        if (!rt_tlas_.Init(ctx_, tlas_handle)) {
            log_->Error("[SceneManager::InitHWAccStructures]: Failed to init TLAS!");
        }
    }

    EndSingleTimeCommands(ctx_->api(), ctx_->device(), ctx_->graphics_queue(), cmd_buf, ctx_->temp_command_pool());

    tlas_scratch_buf.FreeImmediate();
    instance_stage_buf.FreeImmediate();
    geo_data_stage_buf.FreeImmediate();
}
