#pragma once

#include <graphics/resource.h>
#include <graphics/graphics.h>
#include <graphics/helper.h>
#include <renderer/fullscreen.h>
#include <renderer/render_helper.h>
#include <astrophysics/atmosphere.h>

struct LineDrawVertex;

namespace chord
{
	extern void tonemapping(
		uint32 cameraViewBufferId, 
		graphics::GraphicsQueue& queue, 
		graphics::PoolTextureRef srcImage, 
		graphics::PoolTextureRef outImage,
		graphics::PoolBufferGPUOnlyRef exposureBuffer);
	
	struct DebugLineCtx
	{
		uint32 cameraViewBufferId;
		std::vector<LineDrawVertex> vertices;

		graphics::PoolBufferRef gpuVertices;
		graphics::PoolBufferRef gpuCountBuffer;
		uint32 gpuMaxCount;

		void prepareForRender(graphics::GraphicsQueue& queue);
	};
	extern DebugLineCtx allocateDebugLineCtx();

	extern void debugLine(
		graphics::GraphicsQueue& queue, 
		const DebugLineCtx& ctx,
		graphics::PoolTextureRef depthImage,
		graphics::PoolTextureRef outImage);

	extern HZBContext buildHZB(
		graphics::GraphicsQueue& queue, 
		graphics::PoolTextureRef depthImage,
		bool bBuildMin,
		bool bBuildMax,
		bool bBuildValidRange);

	using CountAndCmdBuffer = std::pair<graphics::PoolBufferGPUOnlyRef, graphics::PoolBufferGPUOnlyRef>;

	extern graphics::PoolBufferGPUOnlyRef indirectDispatchCmdFill(
		const std::string& name,
		graphics::GraphicsQueue& queue,
		uint groupSize, 
		graphics::PoolBufferGPUOnlyRef countBuffer);

	extern void debugDrawBuiltinMesh(
		graphics::GraphicsQueue& queue,
		std::vector<BuiltinMeshDrawInstance>& instances,
		uint32 cameraViewId,
		graphics::PoolTextureRef depthImage,
		graphics::PoolTextureRef outImage);

	struct DDGIVolumeResource
	{
		double3 scrollAnchor    = { 0.0, 0.0, 0.0 };
		int3    scrollOffset    = {   0,   0,   0 };

		float3 probeCenterRS;
		float3 probeSpacing;

		//
		int3 probeDim;

		//
		graphics::PoolTextureRef iradianceTexture;
		graphics::PoolTextureRef distanceTexture;

		graphics::PoolBufferRef probeTraceMarkerBuffer;
		graphics::PoolBufferRef probeTraceInfoBuffer;
		graphics::PoolBufferRef probeTraceGbufferInfoBuffer;

		graphics::PoolBufferRef probeTraceHistoryValidBuffer;
		graphics::PoolBufferRef probeOffsetBuffer;
		graphics::PoolBufferRef probeTracedFrameBuffer;
	};

	struct DDGIContext
	{
		std::vector<DDGIVolumeResource> volumes;
	};

	struct GIWorldProbeVolumeResource
	{
		double3 scrollAnchor = { 0.0, 0.0, 0.0 };
		int3    scrollOffset = { 0,   0,   0 };

		float3 probeCenterRS;
		float3 probeSpacing;

		//
		int3 probeDim;

		graphics::PoolBufferRef probeIrradianceBuffer;
		graphics::PoolBufferRef probeDirectionBuffer;
	};
	struct GIContext
	{
		graphics::PoolBufferRef screenProbeSpawnInfoBuffer = nullptr;
		graphics::PoolTextureRef screenProbeTraceRadiance = nullptr;
		graphics::PoolTextureRef screenProbeSampleRT = nullptr;
		graphics::PoolTextureRef historyDiffuseRT = nullptr;


		graphics::PoolTextureRef historySpecularRT = nullptr;

		std::vector<GIWorldProbeVolumeResource> volumes;
	};

	extern graphics::PoolBufferGPUOnlyRef computeAutoExposure(
		graphics::GraphicsQueue& queue,
		graphics::PoolTextureRef color,
		const PostprocessConfig& config,
		graphics::PoolBufferGPUOnlyRef historyExposure,
		float deltaTime);
}
