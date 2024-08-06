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
	mutable chord::math::dvec3 m_worldUp = { 0.0, 1.0, 0.0 };

	// yaw and pitch. in degree.
	double m_yaw = -90.0;
	double m_pitch = 0.0;

	// mouse speed.
	double m_moveSpeed = 10.0f;
	double m_mouseSensitivity = 0.1f;
	double m_maxMouseMoveSpeed = 400.0f;
	double m_minMouseMoveSpeed = 1.0f;

	// first time 
	bool  m_bFirstMouse = true;

	// mouse position of prev frame.
	double m_lastX = 0.0f;
	double m_lastY = 0.0f;

	// Cache matrix.
	chord::math::mat4 m_relativeCameraViewMatrix { 1.0f };
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
	void processKeyboard(EMoveType direction, double deltaTime);
	void processMouseMovement(double xoffset, double yoffset, bool constrainPitch = true);
	void processMouseScroll(double yoffset);


public:
	// return camera view matrix.
	virtual const chord::math::mat4& getRelativeCameraViewMatrix() const override
	{
		return m_relativeCameraViewMatrix;
	}

	// return camera project matrix.
	virtual const chord::math::mat4& getProjectMatrix() const override
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

	// Tick with graphics command, only run when widget is visible.
	virtual void onVisibleTickCmd(const chord::ApplicationTickData& tickData, chord::graphics::CommandList& cmd) override;

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
	float m_cacheScreenpercentage = 1.0f;

	// State to know mouse in viewport. Warning: On glfw3.3 may cause some error state when set cursor to disabled.
	bool m_bMouseInViewport = false;

	std::unique_ptr<chord::DeferredRenderer> m_deferredRenderer;
	std::unique_ptr<ViewportCamera> m_camera;
};