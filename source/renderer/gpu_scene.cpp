#include <renderer/gpu_scene.h>
#include <renderer/renderer.h>

#include <shader/gltf.h>
#include <shader/shader.h>
#include <shader/gpuscene.hlsl>
#include <graphics/helper.h>
#include <renderer/render_helper.h>

#include <renderer/fullscreen.h>
#include <application/application.h>

namespace chord
{
	using namespace graphics;

	class GPUSceneScatterUploadCS : public GlobalShader
	{
	public:
		DECLARE_SUPER_TYPE(GlobalShader);

		static void modifyCompileEnvironment(ShaderCompileEnvironment& o, int32 PermutationId)
		{
			o.setDefine("GPUSCENE_SCATTER_UPLOAD", true);
		}
	};
	IMPLEMENT_GLOBAL_SHADER(GPUSceneScatterUploadCS, "resource/shader/gpuscene.hlsl", "mainCS", EShaderStage::Compute);

	void chord::GPUSceneScatterUpload(
		graphics::GraphicsOrComputeQueue& computeQueue,
		graphics::PoolBufferGPUOnlyRef GPUSceneBuffer,
		std::vector<math::uvec4>&& indexingData,
		std::vector<math::uvec4>&& collectedData)
	{
		auto indexingDataBuffer = getContext().getBufferPool().createHostVisible(
			"indexingDataBuffer", 
			VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, 
			SizedBuffer(sizeof(math::uvec4) * indexingData.size(), (void*)indexingData.data()));

		auto collectedDataBuffer = getContext().getBufferPool().createHostVisible(
			"collectedDataBuffer",
			VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
			SizedBuffer(sizeof(math::uvec4) * collectedData.size(), (void*)collectedData.data()));

		GPUSceneScatterUploadPushConsts pushConst { };
		pushConst.indexingBufferId = asSRV(computeQueue, indexingDataBuffer->getRef());
		pushConst.collectedUploadDataBufferId = asSRV(computeQueue, collectedDataBuffer->getRef());
		pushConst.uploadCount = indexingData.size();
		pushConst.GPUSceneBufferId = asUAV(computeQueue, GPUSceneBuffer->get());

		math::uvec3 dispatchParameter = { divideRoundingUp(pushConst.uploadCount, uint32(kGPUSceneScatterUploadDimX)), 1, 1 };

		auto computePipe = getContext().computePipe<GPUSceneScatterUploadCS>("GPUSceneScatterUploadCS");
		addComputePass(computeQueue, "GPUSceneScatterUpload", computePipe, dispatchParameter, [&](GraphicsOrComputeQueue& queue, ComputePipelineRef pipe, VkCommandBuffer cmd)
		{
			pipe->pushConst(cmd, pushConst);
		});

		// Post update, change stage to SRV.
		asSRV(computeQueue, GPUSceneBuffer->get());
	}

	void chord::enqueueGPUSceneUpdate()
	{
		// Only update once in once frame.
		auto& GPUScene = Application::get().getGPUScene();
		if (GPUScene.m_frameCounter == getFrameCounter())
		{
			return;
		}

		CallOnceInOneFrameEvent::functions.add([](const ApplicationTickData& tickData, graphics::GraphicsQueue& graphics)
		{
			auto& GPUScene = Application::get().getGPUScene();
			GPUScene.update(tickData.tickCount, graphics);
		});
	}

	GPUScene::GPUScene()
		: m_gltfPrimitiveDataPool("GLTF.PrimitiveData")
		, m_gltfPrimitiveDetailPool("GLTF.PrimitiveDetail")
	{
		
	}

	GPUScene::~GPUScene()
	{
		
	}

	void GPUScene::update(uint64 frameCounter, graphics::GraphicsOrComputeQueue& computeQueue)
	{
		// Prereturn if noth
		if (!shouldFlush())
		{
			return;
		}

		check(frameCounter == getFrameCounter());
		if (m_frameCounter == frameCounter)
		{
			return;
		}

		// Only update once in one frame.
		m_frameCounter = frameCounter;

		computeQueue.beginCommand({ computeQueue.getCurrentTimeline() });
		{
			// 
			m_gltfPrimitiveDataPool.flushUpdateCommands(computeQueue);
			m_gltfPrimitiveDetailPool.flushUpdateCommands(computeQueue);
		}
		computeQueue.endCommand();
	}
}

