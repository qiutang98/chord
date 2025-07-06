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
	chord::math::mat4 m_projectMatrixExistZFar { 1.0f };

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

	virtual const chord::math::mat4& getProjectMatrixExistZFar() const override
	{
		return m_projectMatrixExistZFar;
	}

	ViewportCamera(class WidgetViewport* inViewport);

	void tick(const chord::ApplicationTickData& tickData, GLFWwindow* window);
};

struct ProfilerViewer
{
	ProfilerViewer();

	bool bShowProfilerWindow = true;
	bool bShowMilliseconds = true;

	static const size_t kNumFrames = 256;
	float frameTimeArray[kNumFrames] = { 0.0f };

	float recentHighestFrameTime = 0.0f;

	const static size_t kCountNum = 14;
	const int frameTimeGraphMaxFps[kCountNum] = { 800, 240, 120, 90, 60, 45, 30, 15, 10, 5, 4, 3, 2, 1 };
	float frameTimeGraphMaxValues[kCountNum]  = { 0.0f };
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

	// Event on widget visible state change from show to hide. sync on tick function first.
	virtual void onHide(const chord::ApplicationTickData& tickData) override;

	// Event on widget show state change from hide to show.
	virtual void onShow(const chord::ApplicationTickData& tickData) override;

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
	void drawProfileViewer(uint32_t width, uint32_t height);

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
	ProfilerViewer m_profileViewer;
};