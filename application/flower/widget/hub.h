#pragma once

#include <ui/ui.h>
#include <graphics/graphics.h>
#include <ui/widget.h>

struct RecentOpenProjects
{
	chord::uint32 validId = 0;
	std::array<chord::RegionString, 10> recentOpenProjects;

	void update(const std::filesystem::path& path);
	void updatePathForView();

	void save();
	void load();
};

class HubWidget : public chord::IWidget
{
public:
	explicit HubWidget();

protected:
	virtual void onInit() override;
	virtual void onTick(const chord::ApplicationTickData& tickData) override;
	virtual void onRelease() override;

	bool loadProject();
	bool newProject();
	void setupProject(const std::filesystem::path& path);

protected:
	// Is project path ready.
	bool m_bProjectPathReady = false;

	// Input of project path.
	static const auto kProjectPathSize = 512;
	char m_projectPath[kProjectPathSize] = "";

	// Recent project lists.
	RecentOpenProjects m_recentProjectList;
};