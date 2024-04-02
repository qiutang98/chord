#include <ui/ui.h>
#include <utils/cvar.h>
#include <ui/imgui/imgui.h>
#include <ui/imgui/imgui_impl_glfw.h>
#include <application/application.h>
#include <graphics/graphics.h>
#include <graphics/swapchain.h>
#include <graphics/helper.h>
#include <shader/imgui.hlsl>
#include <shader/shader.h>
#include <shader/compiler.h>
#include <graphics/pipeline.h>
#include <ui/imgui/imgui_internal.h>
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

	static std::string sUIConfigFileSavePath = "config";
	static AutoCVarRef<std::string> cVarUIConfigFileSavePath(
		"r.ui.configPath", 
		sUIConfigFileSavePath, 
		"UI config file path saved path relative application.", 
		EConsoleVarFlags::ReadOnly);

	static std::string sUIFontFilePath = "resource/font/微软雅黑.ttf";
	static AutoCVarRef<std::string> cVarsUIFontFilePath(
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

	class ImGuiDrawVS : public GlobalShader
	{
	public:
		DECLARE_SUPER_TYPE(GlobalShader);
	};

	class ImGuiDrawPS : public GlobalShader
	{
	public:
		DECLARE_SUPER_TYPE(GlobalShader);
	};

	IMPLEMENT_GLOBAL_SHADER(ImGuiDrawVS, "resource/shader/imgui.hlsl", "mainVS", EShaderStage::Vertex);
	IMPLEMENT_GLOBAL_SHADER(ImGuiDrawPS, "resource/shader/imgui.hlsl", "mainPS", EShaderStage::Pixel);

	void styleProfessionalDark()
	{
		ImGui::StyleColorsDark();

		ImGuiStyle& style = ImGui::GetStyle();
		ImVec4* colors = ImGui::GetStyle().Colors;

		colors[ImGuiCol_BorderShadow] = ImVec4(0.1f, 0.1f, 0.0f, 0.39f);
		{
			style.Colors[ImGuiCol_Text] = ImVec4(1.00f, 1.00f, 1.00f, 1.00f);
			style.Colors[ImGuiCol_TextDisabled] = ImVec4(0.50f, 0.50f, 0.50f, 1.00f);
			style.Colors[ImGuiCol_Border] = ImVec4(0.43f, 0.43f, 0.50f, 0.50f);
			style.Colors[ImGuiCol_BorderShadow] = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
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
			style.Colors[ImGuiCol_Header] = ImVec4(0.22f, 0.22f, 0.22f, 1.00f);
			style.Colors[ImGuiCol_HeaderHovered] = ImVec4(0.25f, 0.25f, 0.25f, 1.00f);
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


		ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(6.0f, -1.0f));
		ImGui::PushStyleVar(ImGuiStyleVar_ItemInnerSpacing, ImVec2(2.0f, 3.0f));
		ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(4.0f, 1.0f));
		ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 0.0f);
		ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);
		ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
		ImGui::PushStyleVar(ImGuiStyleVar_WindowTitleAlign, ImVec2(0.5f, 0.5f));
		ImGui::PushStyleVar(ImGuiStyleVar_GrabRounding, 0.0f);
		ImGui::PushStyleVar(ImGuiStyleVar_ScrollbarRounding, 0.0f);
		ImGui::PushStyleVar(ImGuiStyleVar_TabRounding, 0.0f);

		colors[ImGuiCol_BorderShadow] = ImVec4(0.1f, 0.1f, 0.0f, 0.39f);

		ImGui::GetIO().ConfigWindowsMoveFromTitleBarOnly = true;
		ImGui::GetIO().ConfigWindowsResizeFromEdges = true;

		style.AntiAliasedLines = true;
		style.WindowMenuButtonPosition = ImGuiDir_Left;

		style.WindowPadding = ImVec2(4, 4);
		style.FramePadding  = ImVec2(6, 4);
		style.ItemSpacing   = ImVec2(6, 2);

		style.ScrollbarSize = 18;

		colors[ImGuiCol_BorderShadow] = ImVec4(0.1f, 0.1f, 0.0f, 0.39f);
		style.WindowBorderSize = 1;
		style.ChildBorderSize = 1;
		style.PopupBorderSize = 1;
		style.FrameBorderSize = 1;
		style.TabBorderSize = 1;
		style.WindowRounding = 0;
		style.ChildRounding = 0;
		style.FrameRounding = 1;
		style.PopupRounding = 0;
		style.ScrollbarRounding = 0;
		style.GrabRounding = 2;
		style.GrabMinSize = 8;
		style.LogSliderDeadzone = 0;
		style.TabRounding = 12;
		style.SliderThickness = 0.3f;
		style.SliderContrast = 1.0f;
	}

	// Imgui draw relative.
	struct ImGuiViewportData
	{
		// Frame index.
		uint32 index;

		struct RenderBuffers
		{
			GPUBufferRef verticesBuffer;
			GPUBufferRef indicesBuffer;
		};
		std::vector<RenderBuffers> frameRenderBuffers;
	};

	uint32 getFontSize(float dpiScale)
	{
		return uint32(sUIFontSize * dpiScale);
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

			if (!std::filesystem::exists(sUIConfigFileSavePath))
			{
				std::filesystem::create_directory(sUIConfigFileSavePath);
			}

			// Config file store path.
			m_iniFileStorePath = std::format("{0}/{1}-ui.ini", sUIConfigFileSavePath, Application::get().getName());
			io.IniFilename = m_iniFileStorePath.c_str();
		}

		updateStyle();

		// Copy main style.
		m_mainStyle = ImGui::GetStyle();

		// Init glfw backend.
		ImGui_ImplGlfw_InitForVulkan(Application::get().getWindowData().window, true);

		// Add font atlas to fit all.
		{
			int monitorsCount = 0;
			GLFWmonitor** glfwMonitors = glfwGetMonitors(&monitorsCount);
			{
				float xScale, yScale;
				glfwGetMonitorContentScale(glfwMonitors[0], &xScale, &yScale);

				int32 fontSize = getFontSize(xScale);
				setupFont(fontSize, xScale);

				m_mainAtlas = io.Fonts; // Just cache main fonts.
				io.Fonts = &m_fontAtlasTextures[fontSize].atlas;
			}
		}

		// Vulkan backend.
		{
			io.BackendRendererName = "ChordImGui";

			// We can honor the ImDrawCmd::VtxOffset field, allowing for large meshes.
			io.BackendFlags |= ImGuiBackendFlags_RendererHasVtxOffset;  

			// Register main viewport data.
			ImGui::GetMainViewport()->RendererUserData = new ImGuiViewportData();
		}
	}

	VkCommandBuffer ImGuiManager::getCommandBuffer(uint32 index)
	{
		while (m_commandBuffers.size() < (index + 1))
		{
			auto cmd = graphics::helper::allocateCommandBuffer(graphics::getContext().getGraphicsCommandPool().pool());
			m_commandBuffers.push_back(cmd);
		}
		return m_commandBuffers.at(index);
	}

	// Setup font for IMGUI windows.
	void ImGuiManager::setupFont(uint32 fontSize, float dpiScale)
	{
		check(m_fontAtlasTextures[fontSize].texture == nullptr);
		ImFontAtlas* fonts = &m_fontAtlasTextures[fontSize].atlas;

		// Load font data to memory.
		fonts->AddFontFromFileTTF(sUIFontFilePath.c_str(), fontSize, NULL, fonts->GetGlyphRangesChineseFull());
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
		m_fontAtlasTextures[fontSize].style      = m_mainStyle;
		m_fontAtlasTextures[fontSize].style.ScaleAllSizes(dpiScale);

		// Update texture id.
		fonts->TexID = texture->requireView(helper::buildBasicImageSubresource(), VK_IMAGE_VIEW_TYPE_2D, true, false).SRV.get();
	}

	ImGuiManager::~ImGuiManager()
	{
		// Clear all font atlas.
		{
			for (auto& fontPair : m_fontAtlasTextures)
			{
				fontPair.second.atlas.Clear();
			}

			// Assgin back font io.
			ImGui::GetIO().Fonts = m_mainAtlas;
		}

		// Free allocated command buffers.
		vkFreeCommandBuffers(
			graphics::getDevice(),
			graphics::getContext().getGraphicsCommandPool().pool(),
			uint32(m_commandBuffers.size()),
			m_commandBuffers.data());
		m_commandBuffers.clear();

		// Cleanup main viewport data.
		{
			auto* vd = (ImGuiViewportData*)ImGui::GetMainViewport()->RendererUserData;
			delete vd;
			ImGui::GetMainViewport()->RendererUserData = nullptr;
		}

		// Release glfw backend.
		ImGui_ImplGlfw_Shutdown();

		ImGui::DestroyContext();
	}

	void ImGuiManager::render()
	{
		ImGui::Render();
	}

	void ImGuiManager::renderFrame(uint32 backBufferIndex)
	{
		// No meaning for minimized window record command buffer.
		check(!isMainMinimized());

		auto& swapchain = getContext().getSwapchain();
		const auto& extent = swapchain.getExtent();

		// SDR mode.
		const bool bCanDrawInBackBuffer = 
			swapchain.getFormatType() == Swapchain::EFormatType::sRGB8Bit;

		auto [image, view] = swapchain.getImage(backBufferIndex);
		auto transitionImageLayout = [&](
			VkCommandBuffer cmd, VkImageLayout oldLayout, VkImageLayout newLayout,
			VkAccessFlags srcAccessMask, VkAccessFlags dstAccessMask, VkPipelineStageFlags srcStage, VkPipelineStageFlagBits dstStage)
		{
			VkImageMemoryBarrier barrier { };
			barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
			barrier.oldLayout = oldLayout;
			barrier.newLayout = newLayout;
			barrier.image = image;
			barrier.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
			barrier.subresourceRange.baseMipLevel   = 0;
			barrier.subresourceRange.levelCount     = 1;
			barrier.subresourceRange.baseArrayLayer = 0;
			barrier.subresourceRange.layerCount     = 1;
			barrier.srcAccessMask = srcAccessMask;
			barrier.dstAccessMask = dstAccessMask;

			vkCmdPipelineBarrier(cmd, srcStage, dstStage, 0, 0, nullptr, 0, nullptr, 1, &barrier);
		};

		VkCommandBuffer cmd = getCommandBuffer(backBufferIndex);
		helper::resetCommandBuffer(cmd);
		helper::beginCommandBuffer(cmd);
		{
			VkFormat uiBackBufferFormat = bCanDrawInBackBuffer
				? swapchain.getSurfaceFormat().format
				: VK_FORMAT_R8G8B8A8_SRGB;

			const auto pipelineCI = GraphicsPipelineCreateInfo::build<ImGuiDrawVS, ImGuiDrawPS>({ uiBackBufferFormat });
			auto graphicsPipeline = getContext().getPipelineContainer().graphics("ImGuiDraw", pipelineCI);

			// Transition image to attachment layout.
			transitionImageLayout(cmd, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, 0, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, 0, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT);

			auto backBufferAttachment = helper::renderingAttachmentInfo(false);
			{
				backBufferAttachment.imageView   = view;
				backBufferAttachment.loadOp      = VK_ATTACHMENT_LOAD_OP_CLEAR;
				backBufferAttachment.storeOp     = VK_ATTACHMENT_STORE_OP_STORE;
				backBufferAttachment.clearValue  = VkClearValue{ .color = { 57.0f / 255.0f, 197.0f / 255.0f, 187.0f / 255.0f, 1.0f } };
			}

			auto renderInfo = helper::renderingInfo();
			renderInfo.renderArea = VkRect2D{ .offset {0,0}, .extent = extent };
			renderInfo.colorAttachmentCount = 1;
			renderInfo.pColorAttachments    = &backBufferAttachment;

			vkCmdBeginRendering(cmd, &renderInfo);
			{
				renderDrawData(cmd, (void*)ImGui::GetDrawData(), graphicsPipeline);
			}
			vkCmdEndRendering(cmd);

			// Transition image to present layout.
			transitionImageLayout(cmd, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);
		}
		helper::endCommandBuffer(cmd);
	}

	bool ImGuiManager::isMainMinimized()
	{
		ImDrawData* mainDrawData = ImGui::GetDrawData();
		return (mainDrawData->DisplaySize.x <= 0.0f || mainDrawData->DisplaySize.y <= 0.0f);
	}

	void ImGuiManager::newFrame()
	{
		{
			float xscale, yscale;
			glfwGetWindowContentScale((GLFWwindow*)ImGui::GetMainViewport()->PlatformHandle, &xscale, &yscale);

			// Still no produce, need create new one.
			uint32 fontSize = getFontSize(xscale);
			if (m_fontAtlasTextures[fontSize].texture == nullptr)
			{
				setupFont(fontSize, xscale);
			}

			ImGui::GetIO().Fonts = &m_fontAtlasTextures[fontSize].atlas;
			ImGui::GetStyle() = m_fontAtlasTextures[fontSize].style;
		}

		ImGui_ImplGlfw_NewFrame();
		ImGui::NewFrame();
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
	}

	static inline VkDeviceSize alignImGuiBufferSize(VkDeviceSize size, VkDeviceSize alignment)
	{
		return (size + alignment - 1) & ~(alignment - 1);
	}

	void ImGuiManager::renderDrawData(VkCommandBuffer commandBuffer, void* drawDataInput, IPipelineRef pipeline)
	{
		auto* drawData = (ImDrawData*)drawDataInput;

		auto& swapchain = getContext().getSwapchain();
		const auto imageCount = swapchain.getBackbufferCount();

		// Avoid rendering when minimized, scale coordinates for retina displays (screen coordinates != framebuffer coordinates)
		int32 fbWidth  = (int32)(drawData->DisplaySize.x * drawData->FramebufferScale.x);
		int32 fbHeight = (int32)(drawData->DisplaySize.y * drawData->FramebufferScale.y);
		if (fbWidth <= 0 || fbHeight <= 0)
		{
			return;
		}

		// Allocate array to store enough vertex/index buffers. Each unique viewport gets its own storage.
		auto* vRD = (ImGuiViewportData*)drawData->OwnerViewport->RendererUserData;
		if (vRD->frameRenderBuffers.empty())
		{
			vRD->index = 0;
			vRD->frameRenderBuffers.resize(imageCount);
		}
		check(vRD->frameRenderBuffers.size() == imageCount);

		// frame loop increment.
		vRD->index = (vRD->index + 1) % imageCount;
		auto& rb = vRD->frameRenderBuffers[vRD->index];

		// Upload vertex/index buffer.
		if (drawData->TotalVtxCount > 0)
		{
			auto vertexSize = alignImGuiBufferSize(drawData->TotalVtxCount * sizeof(ImDrawVert), 256);
			auto indexSize  = alignImGuiBufferSize(drawData->TotalIdxCount * sizeof(ImDrawIdx),  256);
			{
				VkBufferCreateInfo bufferCI{ };
				bufferCI.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
				bufferCI.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

				VmaAllocationCreateInfo allocCI{};
				allocCI.usage = VMA_MEMORY_USAGE_AUTO;
				allocCI.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;

				// Create or resize the vertex/index buffers.
				if (!rb.verticesBuffer || rb.verticesBuffer->getSize() < vertexSize)
				{
					bufferCI.size = vertexSize;
					bufferCI.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
					rb.verticesBuffer = std::make_shared<GPUBuffer>("ImGuiVertices", bufferCI, allocCI);
				}

				if (!rb.indicesBuffer || rb.indicesBuffer->getSize() < indexSize)
				{
					bufferCI.size = indexSize;
					bufferCI.usage = VK_BUFFER_USAGE_INDEX_BUFFER_BIT;

					rb.indicesBuffer = std::make_shared<GPUBuffer>("ImGuiIndices", bufferCI, allocCI);
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

				rb.verticesBuffer->flush(vertexSize, 0);
				rb.indicesBuffer->flush(indexSize, 0);
			}
			rb.verticesBuffer->unmap();
			rb.indicesBuffer->unmap();
		}

		ImGuiDrawPushConsts pushConst{ };
		{
			pipeline->bind(commandBuffer);

			// Set general dynamic states.
			helper::dynamicStateGeneralSet(commandBuffer);

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
			VkColorBlendEquationEXT ext{} ;
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

			pushConst.textureId = ImGui::GetIO().Fonts->TexID;
			pushConst.bFont = true;
			pushConst.samplerId = getSamplers().linearClampEdge().index.get();

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
				pushConst.bFont = (pcmd->TextureId == ImGui::GetIO().Fonts->TexID);

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
}

