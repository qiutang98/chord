#pragma once
#include <ui/widget.h>

class ViewportCamera : public chord::ICamera
{
public:
	enum class EMoveType
	{
		Forward,
		Backward,
		Left,
		Right,
	};

public:
	// Cache viewport.
	class WidgetViewport* m_viewport = nullptr;

	// worldspace up.
	mutable chord::math::vec3 m_worldUp = { 0.0f, 1.0f, 0.0f };

	// yaw and pitch. in degree.
	float m_yaw = -90.0f;
	float m_pitch = 0.0f;

	// mouse speed.
	float m_moveSpeed = 10.0f;
	float m_mouseSensitivity = 0.1f;
	float m_maxMouseMoveSpeed = 400.0f;
	float m_minMouseMoveSpeed = 1.0f;

	// first time 
	bool  m_bFirstMouse = true;

	// mouse position of prev frame.
	double m_lastX = 0.0f;
	double m_lastY = 0.0f;

	// Cache matrix.
	chord::math::mat4 m_viewMatrix { 1.0f };
	chord::math::mat4 m_projectMatrix { 1.0f };

	bool isControlingCamera() const 
	{ 
		return m_bActiveViewport; 
	}

private:
	bool m_bActiveViewport = false;
	bool m_bActiveViewportLastframe = false;

private:
	void updateCameraVectors();
	void updateMatrixMisc();
	void processKeyboard(EMoveType direction, float deltaTime);
	void processMouseMovement(float xoffset, float yoffset, bool constrainPitch = true);
	void processMouseScroll(float yoffset);


public:
	// return camera view matrix.
	virtual chord::math::mat4 getViewMatrix() const override
	{
		return m_viewMatrix;
	}

	// return camera project matrix.
	virtual chord::math::mat4 getProjectMatrix() const override
	{
		return m_projectMatrix;
	}

	ViewportCamera(class WidgetViewport* inViewport);

	void tick(const chord::ApplicationTickData& tickData, GLFWwindow* window);
};

class WidgetViewport : public chord::IWidget
{
public:
	explicit WidgetViewport(size_t index);


protected:
	// event init.
	virtual void onInit() override;

	// event always tick.
	virtual void onTick(const chord::ApplicationTickData& tickData) override;

	// Event before tick.
	virtual void onBeforeTick(const chord::ApplicationTickData& tickData) override;

	// event when widget visible tick.
	virtual void onVisibleTick(const chord::ApplicationTickData& tickData) override;

	// Event after tick.
	virtual void onAfterTick(const chord::ApplicationTickData& tickData) override;

	// event release.
	virtual void onRelease() override;

public:
	// Get renderer dimension.
	float getRenderWidth() const 
	{
		return m_cacheWidth; 
	}

	float getRenderHeight() const 
	{ 
		return m_cacheHeight; 
	}

	// Mouse in viewport.
	bool isMouseInViewport() const 
	{ 
		return m_bMouseInViewport;
	}

private:
	// Index of content widget.
	size_t m_index;


	// Cache viewport size.
	float m_cacheWidth = 0.0f;
	float m_cacheHeight = 0.0f;

	// State to know mouse in viewport. Warning: On glfw3.3 may cause some error state when set cursor to disabled.
	bool m_bMouseInViewport = false;

	std::unique_ptr<ViewportCamera> m_camera;
};