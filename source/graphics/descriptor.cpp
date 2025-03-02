#include "descriptor.h"
#include "graphics.h"

namespace chord::graphics
{
    VkDescriptorPool createPool(VkDevice device, const DescriptorAllocator::PoolSizes& poolSizes, int32_t count, VkDescriptorPoolCreateFlags flags)
    {
        std::vector<VkDescriptorPoolSize> sizes;
        sizes.reserve(poolSizes.sizes.size());
        for (const auto& sz : poolSizes.sizes)
        {
            sizes.push_back({ sz.first, uint32_t(sz.second * count) });
        }

        VkDescriptorPoolCreateInfo poolInfo = {};
        poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        poolInfo.flags = flags;
        poolInfo.maxSets = count;
        poolInfo.poolSizeCount = (uint32_t)sizes.size();
        poolInfo.pPoolSizes = sizes.data();

        VkDescriptorPool descriptorPool;
        check(vkCreateDescriptorPool(device, &poolInfo, nullptr, &descriptorPool));
        return descriptorPool;
    }

    void DescriptorAllocator::resetPools()
    {
        for (auto p : m_usedPools)
        {
            vkResetDescriptorPool(getDevice(), p, 0);
        }

        m_freePools = m_usedPools;
        m_usedPools.clear();
        m_currentPool = VK_NULL_HANDLE;
    }

    bool DescriptorAllocator::allocate(VkDescriptorSet* set, VkDescriptorSetLayout layout)
    {
        // when current working pool is null then request new.
        if (m_currentPool == VK_NULL_HANDLE)
        {
            m_currentPool = requestPool();
            m_usedPools.push_back(m_currentPool);
        }

        VkDescriptorSetAllocateInfo allocInfo = {};
        allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        allocInfo.pNext = nullptr;
        allocInfo.pSetLayouts = &layout;
        allocInfo.descriptorPool = m_currentPool;
        allocInfo.descriptorSetCount = 1;
        VkResult allocResult = vkAllocateDescriptorSets(getDevice(), &allocInfo, set);
        bool needReallocate = false;

        switch (allocResult)
        {
        case VK_SUCCESS:
            return true;
            break;
        case VK_ERROR_FRAGMENTED_POOL:
        case VK_ERROR_OUT_OF_POOL_MEMORY:
            needReallocate = true;
            break;
        default:
            LOG_GRAPHICS_ERROR("Allocate descriptor pool error!");
            return false;
        }

        if (needReallocate)
        {
            m_currentPool = requestPool();
            m_usedPools.push_back(m_currentPool);
            allocInfo.descriptorPool = m_currentPool;
            allocResult = vkAllocateDescriptorSets(getDevice(), &allocInfo, set);
            if (allocResult == VK_SUCCESS)
            {
                return true;
            }
        }

        LOG_GRAPHICS_ERROR("Allocate descriptor pool error!");
        return false;
    }

    void DescriptorAllocator::release()
    {
        resetPools();
        for (auto p : m_freePools)
        {
            vkDestroyDescriptorPool(getDevice(), p, nullptr);
        }
        for (auto p : m_usedPools)
        {
            vkDestroyDescriptorPool(getDevice(), p, nullptr);
        }
    }

    VkDescriptorPool DescriptorAllocator::requestPool()
    {
        if (m_freePools.size() > 0)
        {
            VkDescriptorPool pool = m_freePools.back();
            m_freePools.pop_back();
            return pool;
        }
        else
        {
            return createPool(getDevice(), m_descriptorSizes, 1000, 0);
        }
    }

    VkDescriptorSetLayout DescriptorLayoutCache::createDescriptorLayout(VkDescriptorSetLayoutCreateInfo* info)
    {
        DescriptorLayoutInfo layoutinfo { };
        layoutinfo.bindings.reserve(info->bindingCount);
        bool isSorted = true;
        int32 lastBinding = -1;

        // fill DescriptorLayoutInfo
        for (uint32 i = 0; i < info->bindingCount; i++)
        {
            layoutinfo.bindings.push_back(info->pBindings[i]);
            if (static_cast<int32>(info->pBindings[i].binding) > lastBinding)
            {
                lastBinding = info->pBindings[i].binding;
            }
            else
            {
                isSorted = false;
            }
        }

        // sort first.
        if (!isSorted)
        {
            std::sort(layoutinfo.bindings.begin(), layoutinfo.bindings.end(), [](VkDescriptorSetLayoutBinding& a, VkDescriptorSetLayoutBinding& b)
            {
                return a.binding < b.binding;
            });
        }

        // perpare VkDescriptorSetLayout
        auto it = m_layoutCache.find(layoutinfo);
        if (it != m_layoutCache.end())
        {
            return (*it).second;
        }
        else
        {
            VkDescriptorSetLayout layout;
            vkCreateDescriptorSetLayout(getDevice(), info, nullptr, &layout);
            m_layoutCache[layoutinfo] = layout;

            if (m_layoutCache.size() % 1000 == 0)
            {
                LOG_TRACE("Descriptor layout cache increment already reach {}...", m_layoutCache.size());
            }

            return layout;
        }
    }

    void DescriptorLayoutCache::release()
    {
        for (auto& pair : m_layoutCache)
        {
            vkDestroyDescriptorSetLayout(getDevice(), pair.second, nullptr);
        }
        m_layoutCache.clear();
    }

    bool DescriptorLayoutCache::DescriptorLayoutInfo::operator==(const DescriptorLayoutInfo& other) const
    {
        return other.hash() == hash();
    }

    uint32 DescriptorLayoutCache::DescriptorLayoutInfo::hash() const
    {
        return crc::crc32((const void*)bindings.data(), uint32(bindings.size() * sizeof(VkDescriptorSetLayoutBinding)), 0);
    }

    DescriptorFactory DescriptorFactory::begin(DescriptorLayoutCache* layoutCache, DescriptorAllocator* allocator)
    {
        DescriptorFactory builder;
        builder.m_cache = layoutCache;
        builder.m_allocator = allocator;
        return builder;
    }

    DescriptorFactory& DescriptorFactory::bindBuffers(
        uint32_t binding,
        uint32_t count,
        VkDescriptorBufferInfo* bufferInfo,
        VkDescriptorType type,
        VkShaderStageFlags stageFlags)
    {
        VkDescriptorSetLayoutBinding newBinding{};

        newBinding.descriptorCount = count;
        newBinding.descriptorType = type;
        newBinding.pImmutableSamplers = nullptr;
        newBinding.stageFlags = stageFlags;
        newBinding.binding = binding;

        m_bindings.push_back(newBinding);

        DescriptorWriteContainer descriptorWrite{};
        descriptorWrite.isImg = false;
        descriptorWrite.bufInfo = bufferInfo;
        descriptorWrite.type = type;
        descriptorWrite.binding = binding;
        descriptorWrite.count = count;
        m_descriptorWriteBufInfos.push_back(descriptorWrite);

        return *this;
    }

    DescriptorFactory& DescriptorFactory::bindImages(
        uint32_t binding,
        uint32_t count,
        VkDescriptorImageInfo* info,
        VkDescriptorType type,
        VkShaderStageFlags stageFlags)
    {
        VkDescriptorSetLayoutBinding newBinding{};

        newBinding.descriptorCount = count;
        newBinding.descriptorType = type;
        newBinding.pImmutableSamplers = nullptr;
        newBinding.stageFlags = stageFlags;
        newBinding.binding = binding;

        m_bindings.push_back(newBinding);

        DescriptorWriteContainer descriptorWrite{};
        descriptorWrite.isImg = true;
        descriptorWrite.imgInfo = info;
        descriptorWrite.type = type;
        descriptorWrite.binding = binding;
        descriptorWrite.count = count;

        m_descriptorWriteBufInfos.push_back(descriptorWrite);

        return *this;
    }

    DescriptorFactory& DescriptorFactory::bindNoInfoStage(
        VkDescriptorType type,
        VkShaderStageFlags stageFlags,
        uint32_t binding,
        uint32_t count)
    {
        VkDescriptorSetLayoutBinding newBinding{};

        newBinding.descriptorCount = count;
        newBinding.descriptorType = type;
        newBinding.pImmutableSamplers = nullptr;
        newBinding.stageFlags = stageFlags;
        newBinding.binding = binding;

        m_bindings.push_back(newBinding);

        return *this;
    }

    void DescriptorFactory::buildNoInfo(VkDescriptorSetLayout& layout, VkDescriptorSet& set)
    {
        VkDescriptorSetLayoutCreateInfo layoutInfo{};
        layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        layoutInfo.pBindings = m_bindings.data();
        layoutInfo.bindingCount = static_cast<uint32_t>(m_bindings.size());

        layout = m_cache->createDescriptorLayout(&layoutInfo);
        check(m_allocator->allocate(&set, layout));
    }

    void DescriptorFactory::buildNoInfo(VkDescriptorSet& set)
    {
        VkDescriptorSetLayout layout;
        return buildNoInfo(layout, set);
    }

    VkDescriptorSetLayout DescriptorFactory::buildNoInfoPush()
    {
        VkDescriptorSetLayoutCreateInfo layoutInfo{};
        layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        layoutInfo.flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_PUSH_DESCRIPTOR_BIT_KHR;
        layoutInfo.pBindings = m_bindings.data();
        layoutInfo.bindingCount = static_cast<uint32_t>(m_bindings.size());

        return m_cache->createDescriptorLayout(&layoutInfo);
    }

    bool DescriptorFactory::build(VkDescriptorSet& set, VkDescriptorSetLayout& layout)
    {
        VkDescriptorSet retSet;
        VkDescriptorSetLayout retLayout;

        VkDescriptorSetLayoutCreateInfo layoutInfo{};
        layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        layoutInfo.pBindings = m_bindings.data();
        layoutInfo.bindingCount = static_cast<uint32_t>(m_bindings.size());

        retLayout = m_cache->createDescriptorLayout(&layoutInfo);

        bool success = m_allocator->allocate(&retSet, retLayout);
        if (!success)
        {
            return false;
        };

        std::vector<VkWriteDescriptorSet> writes{};
        for (auto& dc : m_descriptorWriteBufInfos)
        {
            VkWriteDescriptorSet newWrite{};
            newWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            newWrite.pNext = nullptr;
            newWrite.descriptorCount = dc.count;
            newWrite.descriptorType  = dc.type;

            if (dc.isImg)
            {
                newWrite.pImageInfo = dc.imgInfo;
            }
            else
            {
                newWrite.pBufferInfo = dc.bufInfo;
            }

            newWrite.dstBinding = dc.binding;
            newWrite.dstSet = retSet;
            writes.push_back(newWrite);
        }

        vkUpdateDescriptorSets(getDevice(), static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);

        set = retSet;
        layout = retLayout;

        return true;
    }

    bool DescriptorFactory::build(VkDescriptorSet& set)
    {
        VkDescriptorSetLayout layout;
        return build(set, layout);
    }
}