#include <ui/ui.h>
#include <utils/cvar.h>
#include <ui/imgui/imgui.h>
#include <ui/imgui/imgui_impl_glfw.h>
#include <application/application.h>
#include <graphics/graphics.h>
#include <graphics/swapchain.h>
#include <graphics/helper.h>
#include <shader/imgui.hlsl>
#include <shader_compiler/shader.h>
#include <shader_compiler/compiler.h>
#include <graphics/pipeline.h>
#include <ui/imgui/imgui_internal.h>
#include <project.h>
#include <fontawsome/IconsFontAwesome6.h>
#include <fontawsome/IconsFontAwesome6Brands.h>

namespace chord
{ 
	using namespace graphics;

	static bool bEnableDocking = false;
	static AutoCVarRef<bool> cVarEnableDocking(
		"r.ui.docking",
		bEnableDocking, 
		"Enable imgui docking space or not.", 
		EConsoleVarFlags::ReadOnly);

	static bool bEnableViewports = false;
	static AutoCVarRef<bool> cVarEnableViewports(
		"r.ui.viewports", 
		bEnableViewports, 
		"Enable imgui multi viewports or not.", 
		EConsoleVarFlags::ReadOnly);

	static bool bViewportsNoDecoration = false;
	static AutoCVarRef<bool> cVarViewportsNoDecorated(
		"r.ui.viewports.nodecorated", 
		bViewportsNoDecoration, 
		"Multi viewport no decorated.", 
		EConsoleVarFlags::ReadOnly);

	static u16str sUIConfigFileSavePath = u16str("config");
	static AutoCVarRef<u16str> cVarUIConfigFileSavePath(
		"r.ui.configPath", 
		sUIConfigFileSavePath, 
		"UI config file path saved path relative application.", 
		EConsoleVarFlags::ReadOnly);

	static u16str sUIFontFilePath = u16str("resource/font/full_chinese.ttf");
	static AutoCVarRef<u16str> cVarsUIFontFilePath(
		"r.ui.font",
		sUIFontFilePath,
		"ImGui font file path.",
		EConsoleVarFlags::ReadOnly); 

	static float sUIFontSize = 17.0f;
	static AutoCVarRef<float> cVarsUIFontSize(
		"r.ui.font.size", 
		sUIFontSize, 
		"UI font base size.",
		EConsoleVarFlags::ReadOnly);

	PRIVATE_GLOBAL_SHADER(ImGuiDrawVS, "resource/shader/imgui.hlsl", "mainVS", EShaderStage::Vertex);
	PRIVATE_GLOBAL_SHADER(ImGuiDrawPS, "resource/shader/imgui.hlsl", "mainPS", EShaderStage::Pixel);

	static void imguiSetWindowSize(ImGuiViewport* viewport, ImVec2 size)
	{
		if (auto* vd = (ImGuiViewportData*)viewport->RendererUserData)
		{
			vd->swapchain().markDirty();
		}
	}

	static void imguiCreateWindow(ImGuiViewport* viewport)
	{
		// Create new viewport data and assigned.
		viewport->RendererUserData = new ImGuiViewportData(viewport);

		// Update icon and title.
		{
			const auto& icon = Application::get().getIcon();
			GLFWimage glfwIcon
			{
				.width  = icon.getWidth(),
				.height = icon.getHeight(),
				.pixels = (unsigned char*)icon.getPixels()
			};

			auto* nativeHandle = (GLFWwindow*)viewport->PlatformHandle;

			glfwSetWindowIcon(nativeHandle, 1, &glfwIcon);

			const auto titleName = Project::get().getAppTitleName();
			glfwSetWindowTitle(nativeHandle, titleName.c_str());
		}
	}

	static void imguiDestroyWindow(ImGuiViewport* viewport)
	{
		// Clear and release viewport data.
		if (auto* vd = (ImGuiViewportData*)viewport->RendererUserData)
		{
			delete vd;
		}
		viewport->RendererUserData = nullptr;
	}



	static inline VkDeviceSize alignImGuiBufferSize(VkDeviceSize size, VkDeviceSize alignment)
	{
		return (size + alignment - 1) & ~(alignment - 1);
	}

	static void imguiRenderDrawData(uint32 backBufferIndex, VkCommandBuffer commandBuffer, void* drawDataInput, std::shared_ptr<IPipeline> pipeline)
	{
		auto* drawData = (ImDrawData*)drawDataInput;
		auto* vrd = (ImGuiViewportData*)drawData->OwnerViewport->RendererUserData;
		auto* bd = (ImGuiManager*)ImGui::GetIO().BackendRendererUserData;

		auto& swapchain = vrd->swapchain();
		auto& rb = vrd->getRenderBuffers(backBufferIndex);

		// Avoid rendering when minimized, scale coordinates for retina displays (screen coordinates != framebuffer coordinates)
		int32 fbWidth = (int32)(drawData->DisplaySize.x * drawData->FramebufferScale.x);
		int32 fbHeight = (int32)(drawData->DisplaySize.y * drawData->FramebufferScale.y);
		if (fbWidth <= 0 || fbHeight <= 0)
		{
			return;
		}

		// Upload vertex/index buffer.
		if (drawData->TotalVtxCount > 0)
		{
			auto vertexSize = alignImGuiBufferSize(drawData->TotalVtxCount * sizeof(ImDrawVert), 256);
			auto indexSize = alignImGuiBufferSize(drawData->TotalIdxCount * sizeof(ImDrawIdx), 256);
			{
				VkBufferCreateInfo bufferCI{ };
				bufferCI.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
				bufferCI.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

				// Create or resize the vertex/index buffers.
				if (!rb.verticesBuffer || rb.verticesBuffer->getSize() < vertexSize)
				{
					bufferCI.size = vertexSize;
					bufferCI.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
					rb.verticesBuffer = std::make_shared<HostVisibleGPUBuffer>("ImGuiVertices", bufferCI, getHostVisibleCopyUploadGPUBufferVMACI());
				}

				if (!rb.indicesBuffer || rb.indicesBuffer->getSize() < indexSize)
				{
					bufferCI.size = indexSize;
					bufferCI.usage = VK_BUFFER_USAGE_INDEX_BUFFER_BIT;

					rb.indicesBuffer = std::make_shared<HostVisibleGPUBuffer>("ImGuiIndices", bufferCI, getHostVisibleCopyUploadGPUBufferVMACI());
				}
			}

			rb.verticesBuffer->map();
			rb.indicesBuffer->map();
			{
				auto* vtxDst = (ImDrawVert*)rb.verticesBuffer->getMapped();
				auto* idxDst = (ImDrawIdx*)rb.indicesBuffer->getMapped();
				for (int32 n = 0; n < drawData->CmdListsCount; n++)
				{
					const ImDrawList* cmdList = drawData->CmdLists[n];
					memcpy(vtxDst, cmdList->VtxBuffer.Data, cmdList->VtxBuffer.Size * sizeof(ImDrawVert));
					memcpy(idxDst, cmdList->IdxBuffer.Data, cmdList->IdxBuffer.Size * sizeof(ImDrawIdx));
					vtxDst += cmdList->VtxBuffer.Size;
					idxDst += cmdList->IdxBuffer.Size;
				}

				rb.verticesBuffer->flush();
				rb.indicesBuffer->flush();
			}
			rb.verticesBuffer->unmap();
			rb.indicesBuffer->unmap();
		}

		ImGuiDrawPushConsts pushConst{ };
		{
			pipeline->bind(commandBuffer);

			if (drawData->TotalVtxCount > 0)
			{
				VkBuffer vertexBuffers[1] = { *rb.verticesBuffer };
				VkDeviceSize vertexOffset[1] = { 0 };
				vkCmdBindVertexBuffers(commandBuffer, 0, 1, vertexBuffers, vertexOffset);
				vkCmdBindIndexBuffer(commandBuffer, *rb.indicesBuffer, 0, sizeof(ImDrawIdx) == 2 ? VK_INDEX_TYPE_UINT16 : VK_INDEX_TYPE_UINT32);

				const auto binding = helper::vertexInputBindingDescription2EXT(sizeof(ImDrawVert));
				const VkVertexInputAttributeDescription2EXT attributes[3] =
				{
					helper::vertexInputAttributeDescription2EXT(0, VK_FORMAT_R32G32_SFLOAT,  IM_OFFSETOF(ImDrawVert, pos), binding.binding),
					helper::vertexInputAttributeDescription2EXT(1, VK_FORMAT_R32G32_SFLOAT,  IM_OFFSETOF(ImDrawVert, uv),  binding.binding),
					helper::vertexInputAttributeDescription2EXT(2, VK_FORMAT_R8G8B8A8_UNORM, IM_OFFSETOF(ImDrawVert, col), binding.binding),
				};

				vkCmdSetVertexInputEXT(commandBuffer, 1, &binding, countof(attributes), attributes);
			}

			VkBool32 bEnableBlend = VK_TRUE;
			vkCmdSetColorBlendEnableEXT(commandBuffer, 0, 1, &bEnableBlend);
			VkColorBlendEquationEXT ext { };
			ext.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
			ext.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
			ext.colorBlendOp        = VK_BLEND_OP_ADD;
			ext.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
			ext.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
			ext.alphaBlendOp        = VK_BLEND_OP_ADD;
			vkCmdSetColorBlendEquationEXT(commandBuffer, 0, 1, &ext);

			// Setup viewport:
			helper::setViewport(commandBuffer, fbWidth, fbHeight);

			// Setup scale and translation:
			// Our visible imgui space lies from drawData->DisplayPps (top left) to drawData->DisplayPos+data_data->DisplaySize (bottom right). 
			// DisplayPos is (0,0) for single viewport apps.
			{
				pushConst.scale.x = 2.0f / drawData->DisplaySize.x;
				pushConst.scale.y = 2.0f / drawData->DisplaySize.y;
				pushConst.translate.x = -1.0f - drawData->DisplayPos.x * pushConst.scale.x;
				pushConst.translate.y = -1.0f - drawData->DisplayPos.y * pushConst.scale.y;
			}

			pushConst.samplerId = getSamplers().pointClampBorder0000().index.get();
			pipeline->pushConst(commandBuffer, pushConst);
		}

		// Will project scissor/clipping rectangles into framebuffer space
		ImVec2 clipOff = drawData->DisplayPos;         // (0,0) unless using multi-viewports
		ImVec2 clipScale = drawData->FramebufferScale; // (1,1) unless using retina display which are often (2,2)

		// Render command lists
		// (Because we merged all buffers into a single one, we maintain our own offset into them)
		int32 globalVtxOffset = 0;
		int32 globalIdxOffset = 0;
		for (int32 n = 0; n < drawData->CmdListsCount; n++)
		{
			const ImDrawList* cmdList = drawData->CmdLists[n];
			for (int32 cmdIndex = 0; cmdIndex < cmdList->CmdBuffer.Size; cmdIndex++)
			{
				const ImDrawCmd* pcmd = &cmdList->CmdBuffer[cmdIndex];

				// Project scissor/clipping rectangles into framebuffer space
				ImVec2 clipMin((pcmd->ClipRect.x - clipOff.x) * clipScale.x, (pcmd->ClipRect.y - clipOff.y) * clipScale.y);
				ImVec2 clipMax((pcmd->ClipRect.z - clipOff.x) * clipScale.x, (pcmd->ClipRect.w - clipOff.y) * clipScale.y);

				// Clamp to viewport as vkCmdSetScissor() won't accept values that are off bounds
				if (clipMin.x < 0.0f) { clipMin.x = 0.0f; }
				if (clipMin.y < 0.0f) { clipMin.y = 0.0f; }
				if (clipMax.x > fbWidth) { clipMax.x = (float)fbWidth; }
				if (clipMax.y > fbHeight) { clipMax.y = (float)fbHeight; }
				if (clipMax.x <= clipMin.x || clipMax.y <= clipMin.y)
				{
					continue;
				}

				// Apply scissor/clipping rectangle
				helper::setScissor(commandBuffer, { int32(clipMin.x), int32(clipMin.y) }, { uint32(clipMax.x - clipMin.x), uint32(clipMax.y - clipMin.y) });

				const auto copyPushConst = pushConst;

				// Update push const texture id and font.
				pushConst.textureId = pcmd->TextureId;
				pushConst.bFont = bd->m_fontSet.contains(pcmd->TextureId);

				if (copyPushConst != pushConst)
				{
					pipeline->pushConst(commandBuffer, pushConst);
				}

				vkCmdDrawIndexed(commandBuffer, pcmd->ElemCount, 1, pcmd->IdxOffset + globalIdxOffset, pcmd->VtxOffset + globalVtxOffset, 0);
			}
			globalIdxOffset += cmdList->IdxBuffer.Size;
			globalVtxOffset += cmdList->VtxBuffer.Size;
		}

		// Scissor reset.
		helper::setScissor(commandBuffer, { 0, 0 }, { (uint32)fbWidth, (uint32)fbHeight });
	}

	static void imguiRenderWindow(ImGuiViewport* viewport, void* args)
	{
		auto* vd = (ImGuiViewportData*)viewport->RendererUserData;
		auto& swapchain = vd->swapchain();
		const auto& extent = swapchain.getExtent();

		uint32 backBufferIndex = swapchain.acquireNextPresentImage();

		const ApplicationTickData& tickData = *((const ApplicationTickData*)args);
		vd->tickWithCmds(tickData);

		VkCommandBuffer imguiCmdBuffer = vd->getCommandBuffer(backBufferIndex);
		{
			// SDR mode.
			const bool bCanDrawInBackBuffer =
				swapchain.getFormatType() == Swapchain::EFormatType::sRGB8Bit;

			auto [image, view] = swapchain.getImage(backBufferIndex);
			auto transitionImageLayout = [&](
				VkCommandBuffer cmd, VkImageLayout oldLayout, VkImageLayout newLayout,
				VkAccessFlags srcAccessMask, VkAccessFlags dstAccessMask, VkPipelineStageFlags srcStage, VkPipelineStageFlagBits dstStage)
				{
					VkImageMemoryBarrier barrier{ };
					barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
					barrier.oldLayout = oldLayout;
					barrier.newLayout = newLayout;
					barrier.image = image;
					barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
					barrier.subresourceRange.baseMipLevel = 0;
					barrier.subresourceRange.levelCount = 1;
					barrier.subresourceRange.baseArrayLayer = 0;
					barrier.subresourceRange.layerCount = 1;
					barrier.srcAccessMask = srcAccessMask;
					barrier.dstAccessMask = dstAccessMask;

					vkCmdPipelineBarrier(cmd, srcStage, dstStage, 0, 0, nullptr, 0, nullptr, 1, &barrier);
				};

			helper::resetCommandBuffer(imguiCmdBuffer);
			helper::beginCommandBuffer(imguiCmdBuffer);
			{
				VkFormat uiBackBufferFormat = bCanDrawInBackBuffer
					? swapchain.getSurfaceFormat().format
					: VK_FORMAT_R8G8B8A8_SRGB;

				auto graphicsPipeline = getContext().graphicsPipe<ImGuiDrawVS, ImGuiDrawPS>("ImGuiDraw", { uiBackBufferFormat });

				// Transition image to attachment layout.
				transitionImageLayout(imguiCmdBuffer, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, 0, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, 0, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT);

				auto backBufferAttachment = helper::renderingAttachmentInfo(false);
				{
					backBufferAttachment.imageView = view;
					backBufferAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
					backBufferAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
					backBufferAttachment.clearValue = VkClearValue{ .color = { 57.0f / 255.0f, 197.0f / 255.0f, 187.0f / 255.0f, 1.0f } };
				}

				auto renderInfo = helper::renderingInfo();
				{
					renderInfo.renderArea = VkRect2D{ .offset {0,0}, .extent = extent };
					renderInfo.colorAttachmentCount = 1;
					renderInfo.pColorAttachments = &backBufferAttachment;
				}

				vkCmdBeginRendering(imguiCmdBuffer, &renderInfo);
				{
					// Set general dynamic states.
					helper::dynamicStateGeneralSet(imguiCmdBuffer);

					imguiRenderDrawData(backBufferIndex, imguiCmdBuffer, (void*)viewport->DrawData, graphicsPipeline);

					// Full screen blit render texture to back buffer.

				}
				vkCmdEndRendering(imguiCmdBuffer);

				// Transition image to present layout.
				transitionImageLayout(imguiCmdBuffer, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);
			}
			helper::endCommandBuffer(imguiCmdBuffer);
		}

		// UI cmd submit.
		auto frameStartSemaphore = swapchain.getCurrentFrameWaitSemaphore();

		{
			// Need to wait graphics command list finish before render ui.
			auto graphicsTimeline = swapchain.getCommandList().getGraphicsQueue().getCurrentTimeline();
	
			std::vector<VkSemaphore> imguiWaitSemaphores =
			{
				frameStartSemaphore,
				graphicsTimeline.semaphore,
			};

			std::vector<uint64> waitSemaphoreValues =
			{
				0, // Binary semaphore for frame start.
				graphicsTimeline.waitValue,
			};

			auto graphicsEndTimeline = swapchain.getCommandList().getGraphicsQueue().stepTimeline(VK_PIPELINE_STAGE_ALL_COMMANDS_BIT);

			std::vector<VkSemaphore> imguiSignalSemaphores =
			{
				swapchain.getCurrentFrameFinishSemaphore(),
				graphicsEndTimeline.semaphore,
			};

			std::vector<uint64> signalSemaphoreValues =
			{
				0, // Binary semaphore for frame start.
				graphicsEndTimeline.waitValue
			};

			std::vector<VkPipelineStageFlags> kUiWaitFlags = 
			{
				VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
				VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
			};

			VkTimelineSemaphoreSubmitInfo timelineInfo;
			timelineInfo.sType = VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO;
			timelineInfo.pNext = NULL;
			timelineInfo.waitSemaphoreValueCount = waitSemaphoreValues.size();
			timelineInfo.pWaitSemaphoreValues = waitSemaphoreValues.data();
			timelineInfo.signalSemaphoreValueCount = signalSemaphoreValues.size();
			timelineInfo.pSignalSemaphoreValues = signalSemaphoreValues.data();

			VkSubmitInfo submitInfo;
			submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
			submitInfo.pNext = &timelineInfo;
			submitInfo.waitSemaphoreCount = imguiWaitSemaphores.size();
			submitInfo.pWaitSemaphores = imguiWaitSemaphores.data();
			submitInfo.signalSemaphoreCount = imguiSignalSemaphores.size();
			submitInfo.pSignalSemaphores = imguiSignalSemaphores.data();
			submitInfo.commandBufferCount = 1;
			submitInfo.pCommandBuffers = &imguiCmdBuffer;
			submitInfo.pWaitDstStageMask = kUiWaitFlags.data();

			std::vector<VkSubmitInfo> infosRawSubmit{ submitInfo };
			swapchain.submit((uint32)infosRawSubmit.size(), infosRawSubmit.data());
		}
	}

	static void imguiSwapBuffers(ImGuiViewport* viewport, void*)
	{
		auto* vd = (ImGuiViewportData*)viewport->RendererUserData;
		auto& swapchain = vd->swapchain();

		swapchain.present();
	}


	uint32 getFontSize(float dpiScale)
	{
		return uint32(sUIFontSize * dpiScale);
	}

	static void imguiPushWindowStyle(ImGuiViewport* vp)
	{
		auto* bd = (ImGuiManager*)ImGui::GetIO().BackendRendererUserData;
		float dpiScale = vp->DpiScale;
		if (vp->PlatformHandle)
		{ 
			float yscale;
			glfwGetWindowContentScale((GLFWwindow*)vp->PlatformHandle, &dpiScale, &yscale);
		}

		if (dpiScale == 0.0f)
		{
			LOG_ERROR("Zero dpi in viewport, skiping...");
			dpiScale = 1.0f;
		}

		auto& style  = bd->m_cacheStyle;
		auto* styles = &bd->m_cacheStyle;

		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,     math::floor(math::vec2(style.WindowPadding) * math::vec2(dpiScale)));
		ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding,    ImFloor(styles->WindowRounding * dpiScale));
		ImGui::PushStyleVar(ImGuiStyleVar_WindowMinSize,     ImFloor(math::vec2(styles->WindowMinSize) * dpiScale));
		ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding,     ImFloor(styles->ChildRounding * dpiScale));
		ImGui::PushStyleVar(ImGuiStyleVar_PopupRounding,     ImFloor(styles->PopupRounding * dpiScale));

		ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,      ImFloor(math::vec2(styles->FramePadding) * dpiScale));
		ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding,     ImFloor(styles->FrameRounding * dpiScale));
		ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,       ImFloor(math::vec2(styles->ItemSpacing) * dpiScale));
		ImGui::PushStyleVar(ImGuiStyleVar_ItemInnerSpacing,  ImFloor(math::vec2(styles->ItemInnerSpacing) * dpiScale));
		ImGui::PushStyleVar(ImGuiStyleVar_CellPadding,       ImFloor(math::vec2(styles->CellPadding) * dpiScale));

		ImGui::PushStyleVar(ImGuiStyleVar_IndentSpacing, ImFloor(styles->IndentSpacing * dpiScale));
		ImGui::PushStyleVar(ImGuiStyleVar_ScrollbarSize, ImFloor(styles->ScrollbarSize * dpiScale));
		ImGui::PushStyleVar(ImGuiStyleVar_ScrollbarRounding, ImFloor(styles->ScrollbarRounding * dpiScale));
		ImGui::PushStyleVar(ImGuiStyleVar_GrabMinSize, ImFloor(styles->GrabMinSize * dpiScale));
		ImGui::PushStyleVar(ImGuiStyleVar_GrabRounding, ImFloor(styles->GrabRounding * dpiScale));

		ImGui::PushStyleVar(ImGuiStyleVar_TabRounding,       ImFloor(styles->TabRounding * dpiScale));
		ImGui::PushStyleVar(ImGuiStyleVar_TouchExtraPadding,         math::floor(math::vec2(style.TouchExtraPadding) * dpiScale));
		ImGui::PushStyleVar(ImGuiStyleVar_ColumnsMinSpacing,         math::floor(style.ColumnsMinSpacing * dpiScale));
		ImGui::PushStyleVar(ImGuiStyleVar_LogSliderDeadzone,         math::floor(style.LogSliderDeadzone * dpiScale));
		ImGui::PushStyleVar(ImGuiStyleVar_TabMinWidthForCloseButton, math::floor(style.TabMinWidthForCloseButton * dpiScale));
		
		ImGui::PushStyleVar(ImGuiStyleVar_DisplayWindowPadding,      math::floor(math::vec2(style.DisplayWindowPadding) * dpiScale));
		ImGui::PushStyleVar(ImGuiStyleVar_DisplaySafeAreaPadding,    math::floor(math::vec2(style.DisplaySafeAreaPadding) * dpiScale));
		ImGui::PushStyleVar(ImGuiStyleVar_MouseCursorScale,          math::floor(style.MouseCursorScale * dpiScale));
		// 
		const uint32 fontSize = getFontSize(dpiScale);
		auto* fonts = &bd->m_fontAtlasTextures[fontSize].atlas;
		ImGui::PushFont(fonts->Fonts[0]);
	}

	static void imguiPopWindowStyle(ImGuiViewport* vp)
	{
		ImGui::PopFont();
		ImGui::PopStyleVar(23);
	}

	void styleProfessionalDark()
	{
		ImGui::StyleColorsDark();

		ImGuiStyle& style = ImGui::GetStyle();
		ImVec4* colors    = ImGui::GetStyle().Colors;

		{
			style.Colors[ImGuiCol_Text] = ImVec4(1.00f, 1.00f, 1.00f, 1.00f);
			style.Colors[ImGuiCol_TextDisabled] = ImVec4(0.50f, 0.50f, 0.50f, 1.00f);
			style.Colors[ImGuiCol_Border] = ImVec4(0.43f, 0.43f, 0.50f, 0.50f);
			style.Colors[ImGuiCol_BorderShadow] = ImVec4(0.1f, 0.1f, 0.0f, 0.39f);
			style.Colors[ImGuiCol_FrameBg] = ImVec4(0.25f, 0.25f, 0.25f, 1.00f);
			style.Colors[ImGuiCol_FrameBgHovered] = ImVec4(0.38f, 0.38f, 0.38f, 1.00f);
			style.Colors[ImGuiCol_FrameBgActive] = ImVec4(0.67f, 0.67f, 0.67f, 0.39f);
			style.Colors[ImGuiCol_TitleBg] = ImVec4(0.08f, 0.08f, 0.09f, 1.00f);
			style.Colors[ImGuiCol_TitleBgActive] = ImVec4(0.08f, 0.08f, 0.09f, 1.00f);
			style.Colors[ImGuiCol_TitleBgCollapsed] = ImVec4(0.00f, 0.00f, 0.00f, 0.51f);
			style.Colors[ImGuiCol_MenuBarBg] = ImVec4(0.14f, 0.14f, 0.14f, 1.00f);
			style.Colors[ImGuiCol_ScrollbarBg] = ImVec4(0.02f, 0.02f, 0.02f, 0.53f);
			style.Colors[ImGuiCol_ScrollbarGrab] = ImVec4(0.31f, 0.31f, 0.31f, 1.00f);
			style.Colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.41f, 0.41f, 0.41f, 1.00f);
			style.Colors[ImGuiCol_ScrollbarGrabActive] = ImVec4(0.51f, 0.51f, 0.51f, 1.00f);
			style.Colors[ImGuiCol_CheckMark] = ImVec4(0.11f, 0.64f, 0.92f, 1.00f);
			style.Colors[ImGuiCol_SliderGrab] = ImVec4(0.11f, 0.64f, 0.92f, 0.40f);
			style.Colors[ImGuiCol_SliderGrabActive] = ImVec4(0.08f, 0.50f, 0.72f, 1.00f);
			style.Colors[ImGuiCol_Button] = ImVec4(0.25f, 0.25f, 0.25f, 1.00f);
			style.Colors[ImGuiCol_ButtonHovered] = ImVec4(0.38f, 0.38f, 0.38f, 1.00f);
			style.Colors[ImGuiCol_ButtonActive] = ImVec4(0.67f, 0.67f, 0.67f, 0.39f);

			style.Colors[ImGuiCol_TableRowBgAlt] = ImVec4(0.25f, 0.25f, 0.25f, 0.1f);

			style.Colors[ImGuiCol_Header] = ImVec4(0.25f, 0.25f, 0.25f, 1.00f);
			style.Colors[ImGuiCol_HeaderHovered] = ImVec4(0.4f, 0.4f, 0.4f, 1.00f);
			style.Colors[ImGuiCol_HeaderActive] = ImVec4(0.67f, 0.67f, 0.67f, 0.39f);

			style.Colors[ImGuiCol_Separator] = style.Colors[ImGuiCol_Border];
			style.Colors[ImGuiCol_SeparatorHovered] = ImVec4(0.41f, 0.42f, 0.44f, 1.00f);
			style.Colors[ImGuiCol_SeparatorActive] = ImVec4(0.26f, 0.59f, 0.98f, 0.95f);
			style.Colors[ImGuiCol_ResizeGrip] = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
			style.Colors[ImGuiCol_ResizeGripHovered] = ImVec4(0.29f, 0.30f, 0.31f, 0.67f);
			style.Colors[ImGuiCol_ResizeGripActive] = ImVec4(0.26f, 0.59f, 0.98f, 0.95f);
			style.Colors[ImGuiCol_Tab] = ImVec4(0.08f, 0.08f, 0.09f, 0.83f);
			style.Colors[ImGuiCol_TabHovered] = ImVec4(0.33f, 0.34f, 0.36f, 0.83f);
			style.Colors[ImGuiCol_TabActive] = ImVec4(0.23f, 0.23f, 0.24f, 1.00f);
			style.Colors[ImGuiCol_TabUnfocused] = ImVec4(0.08f, 0.08f, 0.09f, 1.00f);
			style.Colors[ImGuiCol_TabUnfocusedActive] = ImVec4(0.13f, 0.14f, 0.15f, 1.00f);
			style.Colors[ImGuiCol_DockingPreview] = ImVec4(0.26f, 0.59f, 0.98f, 0.70f);
			style.Colors[ImGuiCol_DockingEmptyBg] = ImVec4(0.20f, 0.20f, 0.20f, 1.00f);
			style.Colors[ImGuiCol_PlotLines] = ImVec4(0.61f, 0.61f, 0.61f, 1.00f);
			style.Colors[ImGuiCol_PlotLinesHovered] = ImVec4(1.00f, 0.43f, 0.35f, 1.00f);
			style.Colors[ImGuiCol_PlotHistogram] = ImVec4(0.90f, 0.70f, 0.00f, 1.00f);
			style.Colors[ImGuiCol_PlotHistogramHovered] = ImVec4(1.00f, 0.60f, 0.00f, 1.00f);
			style.Colors[ImGuiCol_TextSelectedBg] = ImVec4(0.26f, 0.59f, 0.98f, 0.35f);
			style.Colors[ImGuiCol_DragDropTarget] = ImVec4(0.11f, 0.64f, 0.92f, 1.00f);
			style.Colors[ImGuiCol_NavHighlight] = ImVec4(0.26f, 0.59f, 0.98f, 1.00f);
			style.Colors[ImGuiCol_NavWindowingHighlight] = ImVec4(1.00f, 1.00f, 1.00f, 0.70f);
			style.Colors[ImGuiCol_NavWindowingDimBg] = ImVec4(0.80f, 0.80f, 0.80f, 0.20f);
			style.Colors[ImGuiCol_ModalWindowDimBg] = ImVec4(0.80f, 0.80f, 0.80f, 0.35f);
			style.Colors[ImGuiCol_CheckMark] = ImVec4(0.96f, 0.96f, 0.96f, 1.00f);
			style.Colors[ImGuiCol_SliderGrab] = ImVec4(1.0f, 1.0f, 1.0f, 0.3f);
			style.Colors[ImGuiCol_SliderGrabActive] = ImVec4(1.0f, 1.0f, 1.0f, 1.00f);
		}


		ImGui::GetIO().ConfigWindowsMoveFromTitleBarOnly = true;
		ImGui::GetIO().ConfigWindowsResizeFromEdges = true;

		style.AntiAliasedLines = true;
		style.WindowMenuButtonPosition = ImGuiDir_Left;
		style.WindowPadding     = ImVec2(1, 1);
		style.FramePadding      = ImVec2(6, 4);
		style.ItemSpacing       = ImVec2(6, 2);
		style.ItemInnerSpacing  = ImVec2(2.0f, 3.0f);
		style.WindowTitleAlign  = ImVec2(0.5f, 0.5f);
		style.ScrollbarSize     = 18;
		style.WindowBorderSize  = 1;
		style.ChildBorderSize   = 1;
		style.PopupBorderSize   = 1;
		style.FrameBorderSize   = 1;
		style.TabBorderSize     = 1;
		style.WindowRounding    = 0;
		style.ChildRounding     = 0;
		style.FrameRounding     = 1;
		style.PopupRounding     = 0;
		style.ScrollbarRounding = 0;
		style.GrabRounding      = 0.0f;
		style.GrabMinSize       = 8;
		style.LogSliderDeadzone = 0;
		style.TabRounding       = 12;
		style.SliderThickness   = 0.3f;
		style.SliderContrast    = 1.0f;
	}

	ImGuiManager::ImGuiManager()
	{
		IMGUI_CHECKVERSION();
		ImGui::CreateContext();

		ImGuiIO& io = ImGui::GetIO();
		{
			io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard; // Enable keyboard controls.
			io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;  // Enable gamepad controls.

			// Configable.
			if (bEnableViewports)
			{
				io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;
				io.ConfigViewportsNoDecoration = bViewportsNoDecoration;
			}
			if (bEnableDocking)
			{
				io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
			}

			if (!std::filesystem::exists(sUIConfigFileSavePath.u16()))
			{
				std::filesystem::create_directory(sUIConfigFileSavePath.u16());
			}

			// Config file store path.
			m_iniFileStorePath = std::format("{0}/{1}-ui.ini", sUIConfigFileSavePath.str(), Application::get().getName());
			io.IniFilename = m_iniFileStorePath.c_str();
		}

		updateStyle();

		// Init glfw backend.
		ImGui_ImplGlfw_InitForVulkan(Application::get().getWindowData().window, true);

		// Add font atlas to fit all.
		{
			m_mainAtlas = io.Fonts; // Just cache main fonts.
			io.Fonts = updateMonitorFonts();
		}

		// Vulkan backend.
		{
			io.BackendRendererName = "ChordImGui";

			// Assign backend renderer user data.
			io.BackendRendererUserData = (void*)this;

			// We can honor the ImDrawCmd::VtxOffset field, allowing for large meshes.
			io.BackendFlags |= ImGuiBackendFlags_RendererHasVtxOffset;  
			io.BackendFlags |= ImGuiBackendFlags_RendererHasViewports;

			// Create render data for main viewport.
			ImGui::GetMainViewport()->RendererUserData = new ImGuiViewportData(ImGui::GetMainViewport());

			if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
			{
				ImGui::GetPlatformIO().Renderer_CreateWindow  = imguiCreateWindow;
				ImGui::GetPlatformIO().Renderer_DestroyWindow = imguiDestroyWindow;
				ImGui::GetPlatformIO().Renderer_SetWindowSize = imguiSetWindowSize;
				ImGui::GetPlatformIO().Renderer_RenderWindow  = imguiRenderWindow;
				ImGui::GetPlatformIO().Renderer_SwapBuffers   = imguiSwapBuffers;

				ImGui::GetPlatformIO().Platform_PushWindowStyle = imguiPushWindowStyle;
				ImGui::GetPlatformIO().Platform_PopWindowStyle = imguiPopWindowStyle;
			}
		}
	}

	// Setup font for IMGUI windows.
	void ImGuiManager::setupFont(uint32 fontSize, float dpiScale)
	{
		if (fontSize == 0 || dpiScale == 0.0f)
		{
			return;
		}

		if (m_fontAtlasTextures[fontSize].texture != nullptr)
		{
			return;
		}

		ImFontAtlas* fonts = &m_fontAtlasTextures[fontSize].atlas;

		// Load font data to memory.
		fonts->AddFontFromFileTTF(sUIFontFilePath.str().c_str(), fontSize, NULL, fonts->GetGlyphRangesChineseFull());

		{
			u16str filePath = u16str("resource/font/fa-solid-900.ttf");

			static const ImWchar iconsRanges[] = { ICON_MIN_FA, ICON_MAX_FA, 0 };
			ImFontConfig iconsConfig;
			iconsConfig.MergeMode  = true;
			iconsConfig.PixelSnapH = true;
			iconsConfig.GlyphOffset = math::vec2(0.0f, 1.0f) * dpiScale;

			fonts->AddFontFromFileTTF(filePath.str().c_str(), fontSize, &iconsConfig, iconsRanges);
		}

		{
			u16str filePath = u16str("resource/font/fa-brands-400.ttf");

			static const ImWchar iconsRanges[] = { ICON_MIN_FAB, ICON_MAX_FAB, 0 };
			ImFontConfig iconsConfig;
			iconsConfig.MergeMode = true;
			iconsConfig.PixelSnapH = true;
			iconsConfig.GlyphOffset = math::vec2(0.0f, 1.0f) * dpiScale;

			fonts->AddFontFromFileTTF(filePath.str().c_str(), fontSize, &iconsConfig, iconsRanges);
		}

		fonts->Build();

		// Upload atlas to GPU, sync.
		GPUTextureRef texture;
		{
			using namespace graphics;
			const std::string fonmtTextureName = std::format("{0}-UIFontAtlas-{1}", Application::get().getName(), fontSize);

			unsigned char* pixels;
			int32 width, height;
			fonts->GetTexDataAsAlpha8(&pixels, &width, &height);

			auto imageCI = helper::buildBasicUploadImageCreateInfo(width, height, VK_FORMAT_R8_UNORM);
			auto uploadVMACI = helper::buildVMAUploadImageAllocationCI();
			texture = std::make_shared<GPUTexture>(fonmtTextureName, imageCI, uploadVMACI);

			SizedBuffer stageSizedBuffer(width * height * sizeof(char), pixels);
			getContext().syncUploadTexture(*texture, stageSizedBuffer);
		}

		m_fontAtlasTextures[fontSize].dpiScale   = dpiScale;
		m_fontAtlasTextures[fontSize].texture    = texture;
		m_fontAtlasTextures[fontSize].styles     = m_cacheStyle;
		m_fontAtlasTextures[fontSize].styles.ScaleAllSizes(dpiScale);

		// Update texture id.
		fonts->TexID = texture->requireView(helper::buildBasicImageSubresource(), VK_IMAGE_VIEW_TYPE_2D, true, false).SRV.get();
		m_fontSet.insert(fonts->TexID);
	}

	ImGuiManager::~ImGuiManager()
	{
		// Clear all font atlas.
		{
			for (auto& fontPair : m_fontAtlasTextures)
			{
				fontPair.second.atlas.Locked = false;
				fontPair.second.atlas.Clear();
			}

			// Assgin back font io.
			ImGui::GetIO().Fonts = m_mainAtlas;
		}

		// Clean main viewport render data.
		if (auto* vd = (ImGuiViewportData*)ImGui::GetMainViewport()->RendererUserData)
		{
			delete vd;
		}
		ImGui::GetMainViewport()->RendererUserData = nullptr;

		// Clear all other windows.
		ImGui::DestroyPlatformWindows();

		// Clear bounding backend renderer user data.
		ImGui::GetIO().BackendRendererName = nullptr;
		ImGui::GetIO().BackendRendererUserData = nullptr;

		// Release glfw backend.
		ImGui_ImplGlfw_Shutdown();

		// Final imgui destroy context.
		ImGui::DestroyContext();
	}

	void ImGuiManager::render(const ApplicationTickData& tickData)
	{
		m_bWidgetDrawing = false;

		ImGui::Render();

		if (!isMainMinimized())
		{
			imguiRenderWindow(ImGui::GetMainViewport(), (void*)&tickData);
			imguiSwapBuffers(ImGui::GetMainViewport(), nullptr);
		}

		if (ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
		{
			ImGui::UpdatePlatformWindows();
			ImGui::RenderPlatformWindowsDefault(nullptr, (void*)&tickData);
		}
	}

	ImFontAtlas* ImGuiManager::updateMonitorFonts()
	{
		ImFontAtlas* result;

		int monitorsCount = 0;
		GLFWmonitor** glfwMonitors = glfwGetMonitors(&monitorsCount);
		for (int32 index = monitorsCount - 1; index >= 0; index--)
		{
			float xScale, yScale;
			glfwGetMonitorContentScale(glfwMonitors[index], &xScale, &yScale);

			int32 fontSize = getFontSize(xScale);
			setupFont(fontSize, xScale);

			if (index == 0)
			{
				result = &m_fontAtlasTextures[fontSize].atlas;
			}
		}

		return result;
	}

	bool ImGuiManager::isMainMinimized()
	{
		ImDrawData* mainDrawData = ImGui::GetDrawData();
		return (mainDrawData->DisplaySize.x <= 0.0f || mainDrawData->DisplaySize.y <= 0.0f);
	}

	void ImGuiManager::newFrame()
	{
		updateMonitorFonts();

		// Switch all style if no multi viewports.
		if (!(ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_ViewportsEnable))
		{
			float xScale, yScale;
			glfwGetWindowContentScale(Application::get().getWindowData().window, &xScale, &yScale);
			int32 fontSize = getFontSize(xScale);

			GImGui->Style = m_fontAtlasTextures[fontSize].styles;
			ImGui::GetIO().Fonts = &m_fontAtlasTextures[fontSize].atlas;
		}

		ImGui_ImplGlfw_NewFrame();
		ImGui::NewFrame();

		m_bWidgetDrawing = true;
	}

	void ImGuiManager::updateStyle()
	{
		ImGuiIO& io = ImGui::GetIO();

		// Style setup.
		styleProfessionalDark();

		// When viewports are enabled we tweak WindowRounding/WindowBg so platform windows can look identical to regular ones.
		ImGuiStyle& style = ImGui::GetStyle();
		if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
		{
			style.WindowRounding = 0.0f;
			style.Colors[ImGuiCol_WindowBg].w = 1.0f;
		}

		m_cacheStyle = style;
	}

	ImGuiViewportData::ImGuiViewportData(ImGuiViewport* viewport)
	{
		// Create swapchain.
		m_swapchain = std::make_unique<Swapchain>((GLFWwindow*)viewport->PlatformHandle);
	}

	ImGuiViewportData::~ImGuiViewportData()
	{
		// Reset swapchain which sync command buffer can ensure all resource are safe to release.
		m_swapchain.reset();

		// Free allocated command buffers.
		vkFreeCommandBuffers(
			graphics::getDevice(),
			graphics::getContext().getGraphicsCommandPool().pool(),
			uint32(m_commandBuffers.size()),
			m_commandBuffers.data());
		m_commandBuffers.clear();
	}

	VkCommandBuffer ImGuiViewportData::getCommandBuffer(uint32 index)
	{
		while (m_commandBuffers.size() < (index + 1))
		{
			auto cmd = graphics::helper::allocateCommandBuffer(graphics::getContext().getGraphicsCommandPool().pool());
			m_commandBuffers.push_back(cmd);
		}
		return m_commandBuffers.at(index);
	}
}