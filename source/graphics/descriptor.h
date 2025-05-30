#pragma once

#include "common.h"

namespace chord::graphics
{
    class DescriptorAllocator
    {
        friend class DescriptorFactory;
    public:
        struct PoolSizes
        {
            std::vector<std::pair<VkDescriptorType, float>> sizes =
            {
                { VK_DESCRIPTOR_TYPE_SAMPLER,                    .5f },
                { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,     4.f },
                { VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,              4.f },
                { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,              1.f },
                { VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER,       1.f },
                { VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER,       1.f },
                { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,             2.f },
                { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,             2.f },
                { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC,     1.f },
                { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC,     1.f },
                { VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT,           .5f },
                { VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, .5f },
            };
        };

    private:
        VkDescriptorPool m_currentPool = VK_NULL_HANDLE;
        PoolSizes m_descriptorSizes;
        std::vector<VkDescriptorPool> m_usedPools;
        std::vector<VkDescriptorPool> m_freePools;

        VkDescriptorPool requestPool();
    public:
        // reset all using pool to free.
        void resetPools();

        // allocate descriptor, maybe fail.
        [[nodiscard]] bool allocate(VkDescriptorSet* set, VkDescriptorSetLayout layout);

        void release();
    };

    class DescriptorLayoutCache
    {
    public:
        struct DescriptorLayoutInfo
        {
            std::vector<VkDescriptorSetLayoutBinding> bindings;
            bool operator==(const DescriptorLayoutInfo& other) const;
            uint32 hash() const;
        };

    private:
        struct DescriptorLayoutHash
        {
            uint32 operator()(const DescriptorLayoutInfo& k) const
            {
                return k.hash();
            }
        };

        typedef std::unordered_map<DescriptorLayoutInfo, VkDescriptorSetLayout, DescriptorLayoutHash> LayoutCache;
        LayoutCache m_layoutCache;

    public:
        void release();

        VkDescriptorSetLayout createDescriptorLayout(VkDescriptorSetLayoutCreateInfo* info);
    };

    class DescriptorFactory
    {
    public:
        // start building.
        static DescriptorFactory begin(DescriptorLayoutCache* layoutCache, DescriptorAllocator* allocator);

        // use for buffers.
        DescriptorFactory& bindBuffers(uint32 binding, uint32 count, VkDescriptorBufferInfo* bufferInfo, VkDescriptorType type, VkShaderStageFlags stageFlags);

        // use for textures.
        DescriptorFactory& bindImages(uint32 binding, uint32 count, VkDescriptorImageInfo*, VkDescriptorType type, VkShaderStageFlags stageFlags);

        bool build(VkDescriptorSet& set, VkDescriptorSetLayout& layout);
        bool build(VkDescriptorSet& set);

        // No info bind, need update descriptor set before rendering.
        // use vkCmdPushDescriptorKHR to update.
        DescriptorFactory& bindNoInfoStage(
            VkDescriptorType type,
            VkShaderStageFlags stageFlags,
            uint32 binding,
            uint32 count = 1);

        DescriptorFactory& bindNoInfo(
            VkDescriptorType type,
            uint32 binding,
            uint32 count = 1)
        {
            return bindNoInfoStage(type, VK_SHADER_STAGE_COMPUTE_BIT | VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, binding, count);
        }

        DescriptorFactory& buffer(uint32 binding)
        {
            return bindNoInfo(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, binding);
        }

        DescriptorFactory& textureUAV(uint32 binding)
        {
            return bindNoInfo(VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, binding);
        }

        DescriptorFactory& accelerateStructure(uint32 binding)
        {
            return bindNoInfo(VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, binding);
        }

        DescriptorFactory& textureSRV(uint32 binding)
        {
            return bindNoInfo(VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, binding);
        }

        void buildNoInfo(VkDescriptorSetLayout& layout, VkDescriptorSet& set);
        void buildNoInfo(VkDescriptorSet& set);
        VkDescriptorSetLayout buildNoInfoPush();

    private:
        struct DescriptorWriteContainer
        {
            VkDescriptorImageInfo*  imgInfo;
            VkDescriptorBufferInfo* bufInfo;
            uint32 binding;
            VkDescriptorType type;
            uint32 count;
            bool isImg = false;
        };

        std::vector<DescriptorWriteContainer> m_descriptorWriteBufInfos{ };
        std::vector<VkDescriptorSetLayoutBinding> m_bindings;

        DescriptorLayoutCache* m_cache;
        DescriptorAllocator* m_allocator;
    };
}