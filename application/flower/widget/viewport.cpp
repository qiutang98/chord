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
			ui::drawImage(m_deferredRenderer->getOutput()->get(), kDefaultImageSubresourceRange, ImVec2(width, height));
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
		if (m_camera->getPosition() != prevCamPos)
		{
			// TODO:
			// Editor::get()->setActiveViewportCameraPos(m_camera->getPosition());
		}

		ImGui::SetCursorPos(startPos);
		ImGui::NewLine();

	
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
	m_deferredRenderer->render(tickData, cmd);
}

void WidgetViewport::onAfterTick(const ApplicationTickData& tickData)
{
	ImGui::PopStyleVar(1);
}

void WidgetViewport::onRelease()
{
	m_deferredRenderer.reset();
}

void ViewportCamera::updateCameraVectors()
{
	// Get front vector from yaw and pitch angel.
	math::vec3 front;
	front.x = cos(math::radians(m_yaw)) * cos(math::radians(m_pitch));
	front.y = sin(math::radians(m_pitch));
	front.z = sin(math::radians(m_yaw)) * cos(math::radians(m_pitch));

	m_front = math::normalize(front);

	// Double cross to get camera true up and right vector.
	m_right = math::normalize(math::cross(m_front, m_worldUp));
	m_up = math::normalize(math::cross(m_right, m_front));
}

void ViewportCamera::processKeyboard(EMoveType direction, float deltaTime)
{
	float velocity = m_moveSpeed * deltaTime;

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

void ViewportCamera::processMouseMovement(float xoffset, float yoffset, bool constrainPitch)
{
	xoffset *= m_mouseSensitivity;
	yoffset *= m_mouseSensitivity;

	m_yaw += xoffset;
	m_pitch += yoffset;

	if (constrainPitch)
	{
		// Do some clamp avoid overlap with world up.
		// Also avoid view flip.
		if (m_pitch > 89.0f)
		{
			m_pitch = 89.0f;
		}
		if (m_pitch < -89.0f)
		{
			m_pitch = -89.0f;
		}
	}

	updateCameraVectors();
}

void ViewportCamera::processMouseScroll(float yoffset)
{
	m_moveSpeed += (float)yoffset;
	m_moveSpeed = math::clamp(m_moveSpeed, m_minMouseMoveSpeed, m_maxMouseMoveSpeed);
}

ViewportCamera::ViewportCamera(WidgetViewport* inViewport)
	: m_viewport(inViewport)
{
	updateCameraVectors();
}

void ViewportCamera::tick(const ApplicationTickData& tickData, GLFWwindow* window)
{
	// Skip non window viewport.
	if (window == nullptr)
	{
		return;
	}

	size_t renderWidth  = size_t(m_viewport->getRenderWidth());
	size_t renderHeight = size_t(m_viewport->getRenderHeight());
	float dt = tickData.dt;

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
		ImGui::SetMouseCursor(ImGuiMouseCursor_None);
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


// Scene depth to linear viewspace z is: lz = near / z;
math::mat4 infiniteInvertZPerspectiveRH_ZO(float aspect, float fovy, float zNear)
{
	check(abs(aspect - std::numeric_limits<float>::epsilon()) > 0.0f);

	math::mat4 result = math::zero<math::mat4>();

	const float tanHalfFovy = tan(fovy * 0.5f);
	result[0][0] =  1.0f / (aspect * tanHalfFovy);
	result[1][1] =  1.0f / (tanHalfFovy);
	result[2][3] = -1.0f;
	result[3][2] =  zNear;

	return result;
}

// Update camera view matrix and project matrix.
// We use reverse z projection.
void ViewportCamera::updateMatrixMisc()
{
	// update view matrix.
	m_viewMatrix = math::lookAt(m_position, m_position + m_front, m_up);

	// Reset z far to zero ensure we use infinite invert z.
	m_zFar = 0.0f;
	m_projectMatrix = infiniteInvertZPerspectiveRH_ZO(getAspect(), m_fovy, m_zNear);
}