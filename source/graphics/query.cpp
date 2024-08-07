#include <graphics/graphics.h>

namespace chord::graphics
{
    void GPUTimestamps::init(uint32 numberOfBackBuffers, uint32 maxTimeStampCounts)
    {
        m_numberOfBackBuffers = numberOfBackBuffers;
        m_frame = 0;
        m_maxTimeStampCount = maxTimeStampCounts;

        const VkQueryPoolCreateInfo queryPoolCreateInfo =
        {
            .sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO,
            .pNext = NULL,
            .flags = (VkQueryPoolCreateFlags)0,
            .queryType = VK_QUERY_TYPE_TIMESTAMP ,
            .queryCount = m_maxTimeStampCount * numberOfBackBuffers,
            .pipelineStatistics = 0
        };
        checkVkResult(vkCreateQueryPool(getDevice(), &queryPoolCreateInfo, getAllocationCallbacks(), &m_queryPool));
    }

    void GPUTimestamps::release()
    {
        vkDestroyQueryPool(getDevice(), m_queryPool, getAllocationCallbacks());
        for (uint32 i = 0; i < m_numberOfBackBuffers; i++)
        {
            m_labels[i].clear();
        }
    }

    void GPUTimestamps::getTimeStamp(VkCommandBuffer cmd, const char* label)
    {
        uint32 measurements = (uint32)m_labels[m_frame].size();

        // Now need resize count when stamp count no match desired.
        if (m_maxTimeStampCount <= measurements)
        {
            LOG_WARN("Current GPU already reach {0} but the max time stamps count is {1}, make it bigger!", measurements, m_maxTimeStampCount);
            return;
        }

        uint32 offset = m_frame * m_maxTimeStampCount + measurements;

        vkCmdWriteTimestamp(cmd, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, m_queryPool, offset);
        m_labels[m_frame].push_back(label);
    }

    void GPUTimestamps::getTimeStampCPU(TimeStamp ts)
    {
        m_cpuTimeStamps[m_frame].push_back(ts);
    }

    void GPUTimestamps::onBeginFrame(VkCommandBuffer cmd, std::vector<TimeStamp>* pTimestamps)
    {
        std::vector<TimeStamp>& cpuTimeStamps = m_cpuTimeStamps[m_frame];
        std::vector<std::string>& gpuLabels = m_labels[m_frame];
        uint32 measurements = (uint32)gpuLabels.size();

        pTimestamps->clear();
        pTimestamps->reserve(cpuTimeStamps.size() + gpuLabels.size());

        // Copy CPU timestamps
        for (uint32 i = 0; i < cpuTimeStamps.size(); i++)
        {
            pTimestamps->push_back(cpuTimeStamps[i]);
        }

        // Copy GPU timestamps
        uint32 offset = m_frame * m_maxTimeStampCount;


        if (measurements > 0)
        {
            double microsecondsPerTick = (1e-3f * getContext().getPhysicalDeviceProperties().deviceProperties.limits.timestampPeriod);
            {
                uint64_t timingsInTicks[256] = {};
                VkResult res = vkGetQueryPoolResults(
                    getDevice(),
                    m_queryPool,
                    offset,
                    measurements,
                    measurements * sizeof(uint64_t),
                    &timingsInTicks,
                    sizeof(uint64_t),
                    VK_QUERY_RESULT_64_BIT);

                if (res == VK_SUCCESS)
                {
                    for (uint32 i = 1; i < measurements; i++)
                    {
                        TimeStamp ts =
                        {
                            m_labels[m_frame][i],
                            float(microsecondsPerTick * (double)(timingsInTicks[i] - timingsInTicks[i - 1]))
                        };

                        pTimestamps->push_back(ts);
                    }

                    // Compute total for frame time counter.
                    TimeStamp ts =
                    {
                        "Total GPU Time",
                        float(microsecondsPerTick * (double)(timingsInTicks[measurements - 1] - timingsInTicks[0]))
                    };
                    pTimestamps->push_back(ts);
                }
                else
                {
                    pTimestamps->push_back({ "GPU counters are invalid", 0.0f });
                }
            }
        }

        vkCmdResetQueryPool(cmd, m_queryPool, offset, m_maxTimeStampCount);

        cpuTimeStamps.clear();
        gpuLabels.clear();

        getTimeStamp(cmd, "Begin Frame");
    }

    void GPUTimestamps::onEndFrame()
    {
        m_frame = (m_frame + 1) % m_numberOfBackBuffers;
    }
}