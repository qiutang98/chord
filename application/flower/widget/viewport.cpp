#include "viewport.h"

#include <ui/ui_helper.h>
#include "../flower.h"

using namespace chord::graphics;
using namespace chord;
using namespace chord::ui;

const static std::string kIconViewport = ICON_FA_EARTH_ASIA;

constexpr size_t kViewportMinRenderDim = 64;

static float sViewportScreenPercentage = 1.0f;
static AutoCVarRef<float> cVarScreenPercentage(
	"r.viewport.screenpercentage",
	sViewportScreenPercentage,
	"set all deferred renderer screen percentage.",
	EConsoleVarFlags::Scalability);

static uint32 sEnableStatUnit = 1;
static AutoCVarRef<uint32> cVarEnableStatUnit(
	"r.viewport.stat.unit",
	sEnableStatUnit,
	"Enable stat unit frame.");

static uint32 sEnableStatFrame = 1;
static AutoCVarRef<uint32> cVarEnableStatFrame(
	"r.viewport.stat.frame",
	sEnableStatFrame,
	"Enable stat frame detail.");

WidgetViewport::WidgetViewport(size_t index)
	: IWidget(
		combineIcon("Viewport", kIconViewport).c_str(),
		combineIcon(combineIndex("Viewport", index), kIconViewport).c_str())
	, m_index(index)
{
	m_flags = ImGuiWindowFlags_NoScrollWithMouse;
}

void WidgetViewport::onInit()
{
	// Camera prepare.
	m_camera = std::make_unique<ViewportCamera>(this);

	// Viewport renderer.
	m_deferredRenderer = std::make_unique<DeferredRenderer>(std::format("Viewport#{} DeferredRenderer", m_index));

	
}

void WidgetViewport::onBeforeTick(const ApplicationTickData& tickData)
{
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
}

void WidgetViewport::onTick(const ApplicationTickData& tickData)
{

}

void WidgetViewport::onVisibleTick(const ApplicationTickData& tickData)
{
	float width = math::ceil(ImGui::GetContentRegionAvail().x);
	float height = math::ceil(ImGui::GetContentRegionAvail().y);
	ImGui::BeginChild("ViewportChild", { width, height }, false, ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoDecoration);
	{
		ImVec2 startPos = ImGui::GetCursorPos();

		{
			const auto& builtinResources = Flower::get().getBuiltinTextures();
			ui::drawImage(m_deferredRenderer->getOutput(), kDefaultImageSubresourceRange, ImVec2(width, height));
		}

		bool bClickViewport = ImGui::IsItemClicked();
		const auto minPos   = ImGui::GetItemRectMin();
		const auto maxPos   = ImGui::GetItemRectMax();
		const auto mousePos = ImGui::GetMousePos();

		m_bMouseInViewport = ImGui::IsItemHovered();

		auto prevCamPos = m_camera->getPosition();
		{
			m_camera->tick(tickData, (GLFWwindow*)ImGui::GetCurrentWindow()->Viewport->PlatformHandle);
		}

		// If camera move, current viewport camera is active, so notify flower know.
		if (m_camera->getPosition() != prevCamPos)
		{
			Flower::get().setActiveViewportCamera(m_camera.get());
		}

		ImGui::SetCursorPos(startPos);
		ImGui::NewLine();

		// Draw profile viewer.
		drawProfileViewer(width, height);
	
		// Change viewport size, need notify renderer change render size.
		if (m_cacheWidth != width ||
		    m_cacheHeight != height || 
			sViewportScreenPercentage != m_cacheScreenpercentage)
		{
			if (!ImGui::IsMouseDragging(0)) // Dragging meaning may still resizing.
			{
				m_cacheWidth = width;
				m_cacheHeight = height;

				// Scale size from 100% - 25%.
				m_cacheScreenpercentage   = math::clamp(sViewportScreenPercentage, 1.0f, 4.0f);
				sViewportScreenPercentage = m_cacheScreenpercentage;

				m_deferredRenderer->updateDimension(uint32(width), uint32(height), m_cacheScreenpercentage, 1.0f);
			}
		}
	}
	ImGui::EndChild();
}

void WidgetViewport::onVisibleTickCmd(const ApplicationTickData& tickData, chord::graphics::CommandList& cmd)
{
	m_deferredRenderer->render(tickData, cmd, m_camera.get());
}

void WidgetViewport::onAfterTick(const ApplicationTickData& tickData)
{
	ImGui::PopStyleVar(1);
}

void WidgetViewport::onRelease()
{
	m_deferredRenderer.reset();
}

void WidgetViewport::drawProfileViewer(uint32_t width, uint32_t height)
{
	ImGui::Indent(2.0f);
	if (sEnableStatUnit > 0)
	{
		const auto& timeStamps = m_deferredRenderer->getTimingValues();
		const bool bTimeStampsAvailable = timeStamps.size() > 0;
		if (bTimeStampsAvailable)
		{
			m_profileViewer.recentHighestFrameTime = 0;

			m_profileViewer.frameTimeArray[m_profileViewer.kNumFrames - 1] = timeStamps.back().microseconds;
			for (uint32_t i = 0; i < m_profileViewer.kNumFrames - 1; i++)
			{
				m_profileViewer.frameTimeArray[i] = m_profileViewer.frameTimeArray[i + 1];
			}
			m_profileViewer.recentHighestFrameTime =
				std::max(m_profileViewer.recentHighestFrameTime, m_profileViewer.frameTimeArray[m_profileViewer.kNumFrames - 1]);
		}
		const float& frameTime_us = m_profileViewer.frameTimeArray[m_profileViewer.kNumFrames - 1];
		const float  frameTime_ms = frameTime_us * 0.001f;
		const int fps = bTimeStampsAvailable ? static_cast<int>(1000000.0f / frameTime_us) : 0;
		static const char* textFormat = "%s : %.2f %s";

		auto profileUI = [&]()
		{
			ui::beginGroupPanel("Profiler");
			{
				ImGui::Text("Resolution : %ix%i", (int32_t)width, (int32_t)height);
				ImGui::Text("FPS : %d (%.2f ms)", fps, frameTime_ms);

				for (uint32_t i = 0; i < timeStamps.size(); i++)
				{
					float value = m_profileViewer.bShowMilliseconds ? timeStamps[i].microseconds / 1000.0f : timeStamps[i].microseconds;
					const char* pStrUnit = m_profileViewer.bShowMilliseconds ? "ms" : "us";
					ImGui::Text(textFormat, timeStamps[i].label.c_str(), value, pStrUnit);
				}
			}
			ImGui::Spacing();
			ui::endGroupPanel();
		};

		const auto srcPos = ImGui::GetCursorPos();
		ImGui::PushStyleVar(ImGuiStyleVar_Alpha, 0.0f);
		ImGui::BeginDisabled();
		profileUI();
		ImGui::EndDisabled();
		ImGui::PopStyleVar();
		ImGui::GetWindowDrawList()->AddRectFilled(ImGui::GetItemRectMin(), ImGui::GetItemRectMax(), IM_COL32(0, 0, 0, 139), 2.0f);

		ImGui::SetCursorPos(srcPos);
		profileUI();
	}

	if (sEnableStatFrame > 0)
	{
		size_t iFrameTimeGraphMaxValue = 0;
		size_t iFrameTimeGraphMinValue = 0;
		for (int i = 0; i < m_profileViewer.kCountNum; ++i)
		{
			if (m_profileViewer.recentHighestFrameTime < m_profileViewer.frameTimeGraphMaxValues[i])
			{
				iFrameTimeGraphMaxValue = std::min(int(m_profileViewer.kCountNum - 1), i + 1);
				break;
			}
		}

		auto frameGraphView = [&]()
		{
			ui::beginGroupPanel("GPU frame time (us)");
			{
				ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
				ImGui::PushStyleColor(ImGuiCol_FrameBg, { 0,0,0,0 });
				ImGui::PlotLines("",
					m_profileViewer.frameTimeArray,
					m_profileViewer.kNumFrames,
					0,
					0,
					0.0f,
					m_profileViewer.frameTimeGraphMaxValues[iFrameTimeGraphMaxValue],
					ImVec2(200, 80));
				ImGui::PopStyleColor();
				ImGui::PopStyleVar();
			}
			ui::endGroupPanel();
		};

		const auto srcPos = ImGui::GetCursorPos();
		ImGui::PushStyleVar(ImGuiStyleVar_Alpha, 0.0f);
		ImGui::BeginDisabled();
		frameGraphView();
		ImGui::EndDisabled();
		ImGui::PopStyleVar();
		ImGui::GetWindowDrawList()->AddRectFilled(ImGui::GetItemRectMin(), ImGui::GetItemRectMax(), IM_COL32(0, 0, 0, 139), 2.0f);
		ImGui::SetCursorPos(srcPos);
		frameGraphView();
	}
	ImGui::Unindent();
}

void ViewportCamera::updateCameraVectors()
{
	// Get front vector from yaw and pitch angel.
	math::dvec3 front;

	front.x = cos(math::radians(m_yaw)) * cos(math::radians(m_pitch));
	front.y = sin(math::radians(m_pitch));
	front.z = sin(math::radians(m_yaw)) * cos(math::radians(m_pitch));

	m_front = math::normalize(front);

	// Double cross to get camera true up and right vector.
	m_right = math::normalize(math::cross(m_front, m_worldUp));
	m_up = math::normalize(math::cross(m_right, m_front));
}

void ViewportCamera::processKeyboard(EMoveType direction, double deltaTime)
{
	double velocity = m_moveSpeed * deltaTime;

	if (direction == EMoveType::Forward)
	{
		m_position += m_front * velocity;
	}
	else if (direction == EMoveType::Backward)
	{
		m_position -= m_front * velocity;
	}
	else if (direction == EMoveType::Left)
	{
		m_position -= m_right * velocity;
	}
	else if (direction == EMoveType::Right)
	{
		m_position += m_right * velocity;
	}
	else
	{
		checkEntry();
	}
}

void ViewportCamera::processMouseMovement(double xoffset, double yoffset, bool constrainPitch)
{
	xoffset *= m_mouseSensitivity;
	yoffset *= m_mouseSensitivity;

	m_yaw += xoffset;
	m_pitch += yoffset;

	if (constrainPitch)
	{
		// Do some clamp avoid overlap with world up.
		// Also avoid view flip.
		if (m_pitch > 89.0)
		{
			m_pitch = 89.0;
		}
		if (m_pitch < -89.0)
		{
			m_pitch = -89.0;
		}
	}

	updateCameraVectors();
}

void ViewportCamera::processMouseScroll(double yoffset)
{
	m_moveSpeed += yoffset;
	m_moveSpeed = math::clamp(m_moveSpeed, m_minMouseMoveSpeed, m_maxMouseMoveSpeed);
}

ViewportCamera::ViewportCamera(WidgetViewport* inViewport)
	: m_viewport(inViewport)
{
	updateCameraVectors();
}

void ViewportCamera::tick(const ApplicationTickData& tickData, GLFWwindow* window)
{
	m_fovy = math::radians(60.0f);
	// Skip non window viewport.
	if (window == nullptr)
	{
		return;
	}

	// Update position of last frame.
	{
		m_positionLast = m_position;
	}


	size_t renderWidth  = size_t(m_viewport->getRenderWidth());
	size_t renderHeight = size_t(m_viewport->getRenderHeight());
	const double dt = tickData.dt;

	// prepare view size.
	if (m_width != renderWidth)
	{
		m_width = std::max(kViewportMinRenderDim, renderWidth);
	}

	if (m_height != renderHeight)
	{
		m_height = std::max(kViewportMinRenderDim, renderHeight);
	}

	// handle first input.
	if (m_bFirstMouse)
	{
		glfwGetCursorPos(window, &m_lastX, &m_lastY);
		m_bFirstMouse = false;
	}

	// handle active viewport state.
	m_bActiveViewport = false;
	if (glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_RIGHT))
	{
		if (m_viewport->isMouseInViewport())
		{
			m_bActiveViewport = true;
		}
	}

	// active viewport. disable cursor.
	if (m_bActiveViewport)
	{
		ImGui::SetMouseCursor(ImGuiMouseCursor_Disabled);
	}

	// first time un-active viewport.
	if (m_bActiveViewportLastframe && !m_bActiveViewport)
	{
		glfwGetCursorPos(window, &m_lastX, &m_lastY);
	}

	// continue active viewport.
	double xoffset = 0.0;
	double yoffset = 0.0;
	if (m_bActiveViewportLastframe && m_bActiveViewport)
	{
		glfwGetCursorPos(window, &xoffset, &yoffset);

		xoffset = xoffset - m_lastX;
		yoffset = m_lastY - yoffset;
	}

	// update state.
	m_bActiveViewportLastframe = m_bActiveViewport;
	if (m_bActiveViewport)
	{
		glfwGetCursorPos(window, &m_lastX, &m_lastY);

		processMouseMovement(xoffset, yoffset);
		processMouseScroll(ImGui::GetIO().MouseWheel);
		
		if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS)
		{
			processKeyboard(EMoveType::Forward, dt);
		}
		if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS)
		{
			processKeyboard(EMoveType::Backward, dt);
		}
		if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS)
		{
			processKeyboard(EMoveType::Left, dt);
		}
		if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS)
		{
			processKeyboard(EMoveType::Right, dt);
		}
	}

	updateMatrixMisc();
}

// Update camera view matrix and project matrix.
// We use reverse z projection.
void ViewportCamera::updateMatrixMisc()
{
	// update view matrix, relative camera view.
	m_relativeCameraViewMatrix = math::lookAt(math::vec3(0.0f), math::vec3(m_front), math::vec3(m_up));

	// Reset z far to zero ensure we use infinite invert z.
	m_projectMatrix = chord::infiniteInvertZPerspectiveRH_ZO(getAspect(), m_fovy, m_zNear);

	// Still reverse z.
	m_projectMatrixExistZFar = math::perspectiveRH_ZO(m_fovy, getAspect(), float(m_zFar), float(m_zNear));
}

ProfilerViewer::ProfilerViewer()
{
	for (int i = 0; i < kCountNum; ++i)
	{
		frameTimeGraphMaxValues[i] = 1000000.f / frameTimeGraphMaxFps[i];
	}
}