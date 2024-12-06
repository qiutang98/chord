#include <graphics/helper.h>

namespace chord::graphics::helper
{
	const std::array<VkPipelineStageFlags, 1> SubmitInfo::kDefaultWaitStages =
	{
		VK_PIPELINE_STAGE_ALL_COMMANDS_BIT
	};

	static std::string getRuntimeUniqueGPUASName(const std::string& in)
	{
		static size_t GRuntimeId = 0;
		GRuntimeId++;
		return std::format("AS: {}. {}.", GRuntimeId, in);
	}

	AccelKHR::AccelKHR(const VkAccelerationStructureCreateInfoKHR& inAccelInfo)
		: IResource()
	{
		// Cache create info.
		createInfo = inAccelInfo;

		PoolBufferCreateInfo ci{};
		ci.flags = 0;
		ci.size = createInfo.size;
		ci.usage = VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
		ci.vmaCreateFlag = VMA_MEMORY_USAGE_AUTO;

		//
		buffer = getContext().getBufferPool().create(getRuntimeUniqueGPUASName("AccelBuffer"), ci);

		// Setting the buffer
		createInfo.buffer = buffer->get();

		// Create the acceleration structure
		vkCreateAccelerationStructureKHR(getDevice(), &createInfo, getAllocationCallbacks(), &accel);

		VkAccelerationStructureDeviceAddressInfoKHR addressInfo{ VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR };
		addressInfo.accelerationStructure = accel;
		accelDeviceAddress = vkGetAccelerationStructureDeviceAddressKHR(getDevice(), &addressInfo);
	}

	AccelKHR::~AccelKHR()
	{
		if (accel != VK_NULL_HANDLE)
		{
			vkDestroyAccelerationStructureKHR(getDevice(), accel, getAllocationCallbacks());
			accel = VK_NULL_HANDLE;
		}
		buffer = nullptr;
	}

	bool TLASBuilder::isInit() const
	{
		return m_scratchBuffer != nullptr;
	}

	void TLASBuilder::destroy()
	{
		m_tlas = nullptr;
		m_scratchBuffer = nullptr;
	}

	void TLASBuilder::buildTlas(
		graphics::GraphicsOrComputeQueue& queue,
		const std::vector<VkAccelerationStructureInstanceKHR>& instances,
		bool bRequireUpdate,
		VkBuildAccelerationStructureFlagsKHR flags)
	{
		bool bUpdate = bRequireUpdate && isInit();
		auto activeCmd = queue.getActiveCmd();

		// Copy instance matrix to buffer.
		VkDeviceAddress instBufferAddr;
		{
			auto instanceGPU = getContext().getBufferPool().createHostVisible(
				getRuntimeUniqueGPUASName("TLAS_Instances"),
				VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR,
				SizedBuffer(sizeof(instances[0]) * instances.size(), (void*)instances.data()));

			activeCmd->insertPendingResource(instanceGPU);
			instBufferAddr = instanceGPU->get().getDeviceAddress();
		}

		// Cannot call buildTlas twice except to update.
		uint32 countInstance = static_cast<uint32>(instances.size());

		// All barrier before TLAS build.
		{
			VkMemoryBarrier barrier { VK_STRUCTURE_TYPE_MEMORY_BARRIER };
			barrier.srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT | VK_ACCESS_MEMORY_READ_BIT;
			barrier.dstAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR;
			vkCmdPipelineBarrier(activeCmd->commandBuffer, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 1, &barrier, 0, nullptr, 0, nullptr);
		}

		// Wraps a device pointer to the above uploaded instances.
		VkAccelerationStructureGeometryInstancesDataKHR instancesVk{ VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR };
		instancesVk.data.deviceAddress = instBufferAddr;

		// Put the above into a VkAccelerationStructureGeometryKHR. We need to put the instances struct in a union and label it as instance data.
		VkAccelerationStructureGeometryKHR topASGeometry{ VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR };
		topASGeometry.geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR;
		topASGeometry.geometry.instances = instancesVk;

		// Find sizes
		VkAccelerationStructureBuildGeometryInfoKHR buildInfo{ VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR };
		VkAccelerationStructureBuildSizesInfoKHR sizeInfo{ VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR };

		//
		buildInfo.flags = flags | VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_UPDATE_BIT_KHR;
		buildInfo.geometryCount            = 1;
		buildInfo.pGeometries              = &topASGeometry;
		buildInfo.type                     = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
		buildInfo.srcAccelerationStructure = VK_NULL_HANDLE;

		{

			buildInfo.mode = bUpdate ? VK_BUILD_ACCELERATION_STRUCTURE_MODE_UPDATE_KHR : VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
			vkGetAccelerationStructureBuildSizesKHR(getDevice(), VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &buildInfo, &countInstance, &sizeInfo);
		}

		auto copyBuildInfo = buildInfo;
		auto copySizeInfo  = sizeInfo;
		{
			copyBuildInfo.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;

			vkGetAccelerationStructureBuildSizesKHR(
				getDevice(),
				VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR,
				&copyBuildInfo,
				&countInstance,
				&copySizeInfo);
		}

		// Sometimes it can't update when change too much, so just rebuild.
		if (bUpdate)
		{
			check(m_tlas);
			check(m_scratchBuffer);
		}

		// Size not match, rebuild.
		if (m_buildSizeInfo.accelerationStructureSize < copySizeInfo.accelerationStructureSize ||
			m_buildSizeInfo.buildScratchSize          < copySizeInfo.buildScratchSize ||
			m_buildSizeInfo.accelerationStructureSize > copySizeInfo.accelerationStructureSize * 8U ||
			m_buildSizeInfo.buildScratchSize          > copySizeInfo.buildScratchSize * 8U) // When size change too much, just rebuild TLAS
		{
			bUpdate = false;
			destroy();

			// Cache copy size info.
			buildInfo = copyBuildInfo;
			sizeInfo = copySizeInfo;
		}

		// Create TLAS
		if (m_tlas == nullptr)
		{
			VkAccelerationStructureCreateInfoKHR createInfo{ VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR };
			createInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
			createInfo.size = sizeInfo.accelerationStructureSize;

			m_tlas = std::make_shared<AccelKHR>(createInfo);
		}

		if (m_scratchBuffer == nullptr)
		{
			PoolBufferCreateInfo ci { };
			ci.flags = 0;
			ci.size = sizeInfo.buildScratchSize;
			ci.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
			ci.vmaCreateFlag = VMA_MEMORY_USAGE_AUTO;

			//
			m_scratchBuffer = getContext().getBufferPool().create(getRuntimeUniqueGPUASName("tlas_scratch_context_vma"), ci);
		}

		// Update cached sized info.
		m_buildSizeInfo = sizeInfo;

		activeCmd->insertPendingResource(m_tlas);
		activeCmd->insertPendingResource(m_scratchBuffer);

		VkDeviceAddress scratchAddress = m_scratchBuffer->get().getDeviceAddress();

		// Update build information
		buildInfo.srcAccelerationStructure  = bUpdate ? m_tlas->accel : VK_NULL_HANDLE;
		buildInfo.dstAccelerationStructure  = m_tlas->accel;
		buildInfo.scratchData.deviceAddress = scratchAddress;

		// Build Offsets info: n instances
		VkAccelerationStructureBuildRangeInfoKHR buildOffsetInfo{ countInstance, 0, 0, 0 };
		const VkAccelerationStructureBuildRangeInfoKHR* pBuildOffsetInfo = &buildOffsetInfo;

		// Build the TLAS
		vkCmdBuildAccelerationStructuresKHR(activeCmd->commandBuffer, 1, &buildInfo, &pBuildOffsetInfo);

		{
			// Barrier.
			VkMemoryBarrier barrier{ VK_STRUCTURE_TYPE_MEMORY_BARRIER };
			barrier.srcAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR;
			barrier.dstAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR;
			vkCmdPipelineBarrier(activeCmd->commandBuffer, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 1, &barrier, 0, nullptr, 0, nullptr);
		}
	}

	void BLASBuilder::destroy()
	{
		m_blas = {};
		m_updateScratchBuffer = nullptr;
	}

	VkDeviceAddress BLASBuilder::getBlasDeviceAddress(uint32 inBlasId) const
	{
		return m_blas.at(inBlasId)->accelDeviceAddress;
	}

	void BLASBuilder::build(const std::vector<BlasInput>& input, VkBuildAccelerationStructureFlagsKHR flags)
	{
		auto nbBlas = static_cast<uint32>(input.size());
		VkDeviceSize asTotalSize{ 0 };     // Memory size of all allocated BLAS
		uint32       nbCompactions{ 0 };   // Nb of BLAS requesting compaction
		VkDeviceSize maxScratchSize{ 0 };  // Largest scratch size

		// Preparing the information for the acceleration build commands.
		std::vector<BLASBuilder::BuildAccelerationStructure> buildAs(nbBlas);
		for (uint32 idx = 0; idx < nbBlas; idx++)
		{
			// Filling partially the VkAccelerationStructureBuildGeometryInfoKHR for querying the build sizes.
			// Other information will be filled in the createBlas (see #2)
			buildAs[idx].buildInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
			buildAs[idx].buildInfo.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
			buildAs[idx].buildInfo.flags = input[idx].flags | flags;
			buildAs[idx].buildInfo.geometryCount = static_cast<uint32_t>(input[idx].asGeometry.size());
			buildAs[idx].buildInfo.pGeometries = input[idx].asGeometry.data();

			// Build range information
			buildAs[idx].rangeInfo = input[idx].asBuildOffsetInfo.data();

			// Finding sizes to create acceleration structures and scratch
			std::vector<uint32_t> maxPrimCount(input[idx].asBuildOffsetInfo.size());
			for (auto tt = 0; tt < input[idx].asBuildOffsetInfo.size(); tt++)
			{
				// Number of primitives/triangles
				maxPrimCount[tt] = input[idx].asBuildOffsetInfo[tt].primitiveCount;  
			}

			vkGetAccelerationStructureBuildSizesKHR(
				getDevice(),
				VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR,
				&buildAs[idx].buildInfo, maxPrimCount.data(), &buildAs[idx].sizeInfo);

			// Extra info
			asTotalSize += buildAs[idx].sizeInfo.accelerationStructureSize;
			maxScratchSize = std::max(maxScratchSize, buildAs[idx].sizeInfo.buildScratchSize);
			nbCompactions += hasFlag(buildAs[idx].buildInfo.flags, VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_COMPACTION_BIT_KHR);
		}

		// Allocate a query pool for storing the needed size for every BLAS compaction.
		VkQueryPool queryPool{ VK_NULL_HANDLE };
		if (nbCompactions > 0)  // Is compaction requested?
		{
			check(nbCompactions == nbBlas);  // Don't allow mix of on/off compaction
			VkQueryPoolCreateInfo qpci{ VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO };
			qpci.queryCount = nbBlas;
			qpci.queryType = VK_QUERY_TYPE_ACCELERATION_STRUCTURE_COMPACTED_SIZE_KHR;
			vkCreateQueryPool(getDevice(), &qpci, nullptr, &queryPool);
		}

		PoolBufferRef scratchBuffer;
		{
			PoolBufferCreateInfo ci{ };
			ci.flags = 0;
			ci.size  = maxScratchSize;
			ci.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
			ci.vmaCreateFlag = VMA_MEMORY_USAGE_AUTO;

			//
			scratchBuffer = getContext().getBufferPool().create(getRuntimeUniqueGPUASName("scratch"), ci);
		}
		VkDeviceAddress scratchAddress = scratchBuffer->get().getDeviceAddress();

		// Batching creation/compaction of BLAS to allow staying in restricted amount of memory
		std::vector<uint32>   indices;  // Indices of the BLAS to create
		VkDeviceSize          batchSize { 0 };
		VkDeviceSize          batchLimit { 256 * 1024 * 1024 };  // 256 MB
		for (uint32 idx = 0; idx < nbBlas; idx++)
		{
			indices.push_back(idx);
			batchSize += buildAs[idx].sizeInfo.accelerationStructureSize;

			// Over the limit or last BLAS element
			if (batchSize >= batchLimit || idx == nbBlas - 1)
			{
				getContext().executeImmediatelyMajorCompute(
					[&](VkCommandBuffer cb, uint32 family, VkQueue queue)
					{
						cmdCreateBlas(cb, indices, buildAs, scratchAddress, queryPool);
					});

				if (queryPool)
				{
					getContext().executeImmediatelyMajorCompute(
						[&](VkCommandBuffer cb, uint32 family, VkQueue queue)
						{
							cmdCompactBlas(cb, indices, buildAs, queryPool);
						});

					// Destroy the non-compacted version
					destroyNonCompacted(indices, buildAs);
				}
				// Reset

				batchSize = 0;
				indices.clear();
			}
		}

		// Logging reduction
		if (queryPool)
		{
			VkDeviceSize compactSize = std::accumulate(buildAs.begin(), buildAs.end(), 0ULL, [](const auto& a, const auto& b) 
			{
				return a + b.sizeInfo.accelerationStructureSize;
			});

			LOG_TRACE(" RT BLAS: reducing from: {0} KB to: {1}KB, Save {2}KB({3}% smaller).",
				(asTotalSize / 1024.0f),
				(compactSize / 1024.0f),
				(asTotalSize - compactSize) / 1024.0f,
				(asTotalSize - compactSize) / float(asTotalSize) * 100.f);
		}

		// Keeping all the created acceleration structures
		for (auto& b : buildAs)
		{
			m_blas.emplace_back(b.as);
		}

		// Clean up
		vkDestroyQueryPool(getDevice(), queryPool, nullptr);
	}

	bool BLASBuilder::isInit() const
	{
		return !m_blas.empty();
	}

	void BLASBuilder::update(
		graphics::GraphicsOrComputeQueue& queue, 
		const std::vector<BlasInput>& input, 
		VkBuildAccelerationStructureFlagsKHR flags)
	{
		check(!hasFlag(flags, VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_COMPACTION_BIT_KHR));
		check(isInit());

		auto activeCmd = queue.getActiveCmd();
		std::vector<VkAccelerationStructureBuildGeometryInfoKHR> buildInfosArray(input.size());

		{
			VkMemoryBarrier barrier { VK_STRUCTURE_TYPE_MEMORY_BARRIER };
			barrier.srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT | VK_ACCESS_MEMORY_READ_BIT;
			barrier.dstAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR;
			vkCmdPipelineBarrier(activeCmd->commandBuffer, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 1, &barrier, 0, nullptr, 0, nullptr);
		}

		VkDeviceSize maxSize = 0;
		for (size_t i = 0; i < input.size(); i++)
		{
			auto& blas = input[i];
			auto& buildInfos = buildInfosArray[i];

			// Don't update with compression flag.
			check(!hasFlag(blas.flags, VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_COMPACTION_BIT_KHR));

			// Preparing all build information, acceleration is filled later
			buildInfos.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR;
			buildInfos.flags = flags;
			buildInfos.geometryCount = (uint32_t)blas.asGeometry.size();
			buildInfos.pGeometries = blas.asGeometry.data();
			buildInfos.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_UPDATE_KHR;  // UPDATE
			buildInfos.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
			buildInfos.srcAccelerationStructure = m_blas[i]->accel;  // UPDATE
			buildInfos.dstAccelerationStructure = m_blas[i]->accel;

			//
			activeCmd->insertPendingResource(m_blas[i]);

			// Find size to build on the device
			std::vector<uint32> maxPrimCount(blas.asBuildOffsetInfo.size());
			for (auto tt = 0; tt < blas.asBuildOffsetInfo.size(); tt++)
			{
				maxPrimCount[tt] = blas.asBuildOffsetInfo[tt].primitiveCount;  // Number of primitives/triangles
			}

			VkAccelerationStructureBuildSizesInfoKHR sizeInfo{ VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR };
			vkGetAccelerationStructureBuildSizesKHR(getDevice(), VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &buildInfos, maxPrimCount.data(), &sizeInfo);

			maxSize = math::max(maxSize, math::max(sizeInfo.updateScratchSize, sizeInfo.buildScratchSize));
		}

		if (m_updateScratchBuffer)
		{
			if (maxSize > m_updateScratchBuffer->get().getSize())
			{
				// Release old scratch buffer.
				m_updateScratchBuffer = nullptr;
			}
		}

		// Allocate the scratch buffer and setting the scratch info
		if (m_updateScratchBuffer == nullptr)
		{
			PoolBufferCreateInfo ci{ };
			ci.flags = 0;
			ci.size  = maxSize;
			ci.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
			ci.vmaCreateFlag = VMA_MEMORY_USAGE_AUTO;

			m_updateScratchBuffer = getContext().getBufferPool().create(getRuntimeUniqueGPUASName("blas_update_scratch_vma"), ci);
		}

		activeCmd->insertPendingResource(m_updateScratchBuffer);

		for (size_t i = 0; i < input.size(); i++)
		{
			auto& blas = input[i];
			auto& buildInfos = buildInfosArray[i];

			buildInfos.scratchData.deviceAddress = m_updateScratchBuffer->get().getDeviceAddress();

			std::vector<const VkAccelerationStructureBuildRangeInfoKHR*> pBuildOffset(blas.asBuildOffsetInfo.size());
			for (size_t i = 0; i < blas.asBuildOffsetInfo.size(); i++)
			{
				pBuildOffset[i] = &blas.asBuildOffsetInfo[i];
			}

			// 
			vkCmdBuildAccelerationStructuresKHR(activeCmd->commandBuffer, 1, &buildInfos, pBuildOffset.data());

			// Since the scratch buffer is reused across builds, we need a barrier to ensure one build
			// is finished before starting the next one.
			{
				VkMemoryBarrier barrier{ VK_STRUCTURE_TYPE_MEMORY_BARRIER };
				barrier.srcAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR;
				barrier.dstAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR;
				vkCmdPipelineBarrier(activeCmd->commandBuffer, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 1, &barrier, 0, nullptr, 0, nullptr);
			}
		}
	}

	void BLASBuilder::cmdCreateBlas(
		VkCommandBuffer cmd,
		std::vector<uint32> indices,
		std::vector<BuildAccelerationStructure>& buildAs,
		VkDeviceAddress scratchAddress,
		VkQueryPool queryPool)
	{
		if (queryPool)
		{
			vkResetQueryPool(getDevice(), queryPool, 0, static_cast<uint32>(indices.size()));
		}

		uint32 queryCnt{ 0 };

		for (const auto& idx : indices)
		{
			{
				// Actual allocation of buffer and acceleration structure.
				VkAccelerationStructureCreateInfoKHR createInfo{ VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR };
				createInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
				createInfo.size = buildAs[idx].sizeInfo.accelerationStructureSize;  // Will be used to allocate memory.

				buildAs[idx].as = std::make_shared<AccelKHR>(createInfo);
			}

			{
				// BuildInfo #2 part
				buildAs[idx].buildInfo.dstAccelerationStructure = buildAs[idx].as->accel;  // Setting where the build lands
				buildAs[idx].buildInfo.scratchData.deviceAddress = scratchAddress;  // All build are using the same scratch buffer
			}

			// Building the bottom-level-acceleration-structure
			vkCmdBuildAccelerationStructuresKHR(cmd, 1, &buildAs[idx].buildInfo, &buildAs[idx].rangeInfo);

			// Since the scratch buffer is reused across builds, we need a barrier to ensure one build
			// is finished before starting the next one.
			VkMemoryBarrier barrier{ VK_STRUCTURE_TYPE_MEMORY_BARRIER };
			barrier.srcAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR;
			barrier.dstAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR;
			vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR, VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR, 0, 1, &barrier, 0, nullptr, 0, nullptr);

			if (queryPool)
			{
				// Add a query to find the 'real' amount of memory needed, use for compaction
				vkCmdWriteAccelerationStructuresPropertiesKHR(cmd, 1, &buildAs[idx].buildInfo.dstAccelerationStructure, VK_QUERY_TYPE_ACCELERATION_STRUCTURE_COMPACTED_SIZE_KHR, queryPool, queryCnt++);
			}
		}
	}

	void BLASBuilder::cmdCompactBlas(
		VkCommandBuffer cmd, 
		std::vector<uint32> indices, 
		std::vector<BuildAccelerationStructure>& buildAs, 
		VkQueryPool queryPool)
	{
		uint32 queryCtn{ 0 };

		// Get the compacted size result back
		std::vector<VkDeviceSize> compactSizes(static_cast<uint32>(indices.size()));
		vkGetQueryPoolResults(getDevice(), queryPool, 0, (uint32)compactSizes.size(), compactSizes.size() * sizeof(VkDeviceSize), compactSizes.data(), sizeof(VkDeviceSize), VK_QUERY_RESULT_WAIT_BIT);

		for (auto idx : indices)
		{
			buildAs[idx].cleanupAS = buildAs[idx].as; // previous AS to destroy
			buildAs[idx].sizeInfo.accelerationStructureSize = compactSizes[queryCtn++];  // new reduced size

			// Creating a compact version of the AS
			{
				VkAccelerationStructureCreateInfoKHR asCreateInfo{ VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR };
				asCreateInfo.size = buildAs[idx].sizeInfo.accelerationStructureSize;
				asCreateInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
				buildAs[idx].as = std::make_shared<AccelKHR>(asCreateInfo);
			}

			// Copy the original BLAS to a compact version
			VkCopyAccelerationStructureInfoKHR copyInfo{ VK_STRUCTURE_TYPE_COPY_ACCELERATION_STRUCTURE_INFO_KHR };
			copyInfo.src = buildAs[idx].buildInfo.dstAccelerationStructure;
			copyInfo.dst = buildAs[idx].as->accel;
			copyInfo.mode = VK_COPY_ACCELERATION_STRUCTURE_MODE_COMPACT_KHR;
			vkCmdCopyAccelerationStructureKHR(cmd, &copyInfo);
		}
	}

	void BLASBuilder::destroyNonCompacted(std::vector<uint32> indices, std::vector<BuildAccelerationStructure>& buildAs)
	{
		for (auto& i : indices)
		{
			buildAs[i].cleanupAS = nullptr;
		}
	}
}