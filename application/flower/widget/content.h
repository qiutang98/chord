#pragma once

#include <ui/widget.h>
#include "../manager/project_content.h"

// Select of content asset entry.
class ProjectContentEntrySelect
{
public:
	ProjectContentEntryWeak entry;

	ProjectContentEntrySelect(ProjectContentEntryRef inEntry)
		: entry(inEntry)
	{

	}

	bool isValid() const
	{
		return entry.lock() != nullptr;
	}

	operator bool() const
	{
		return isValid();
	}

	bool operator==(const ProjectContentEntrySelect& rhs) const
	{
		return entry.lock() == rhs.entry.lock();
	}

	bool operator!=(const ProjectContentEntrySelect& rhs) const
	{
		return !(*this == rhs);
	}

	bool operator<(const ProjectContentEntrySelect& rhs) const
	{
		if (entry.lock() && rhs.entry.lock())
		{
			return entry.lock()->getPath().u16() < rhs.entry.lock()->getPath().u16();
		}
		return false;
	}
};

class WidgetContent : public chord::IWidget
{
public:
	explicit WidgetContent(size_t index);

protected:
	// event init.
	virtual void onInit() override;

	// event always tick.
	virtual void onTick(const chord::ApplicationTickData& tickData) override;

	// event when widget visible tick.
	virtual void onVisibleTick(const chord::ApplicationTickData& tickData) override;

	// event release.
	virtual void onRelease() override;


public:
	const auto& getSelections() const 
	{ 
		return m_selections;
	}

	auto& getSelections() 
	{ 
		return m_selections; 
	}

private:
	// On tree update.
	void onTreeUpdate(const ProjectContentEntryTree& tree);

	// Setup project content.
	void setupProject();

	// Set current active entry, used for content view inspect.
	void setActiveEntry(ProjectContentEntryRef entry);

private:
	// Draw functions.
	void drawContent(const chord::ApplicationTickData& tickData);

	void drawMenu(const chord::ApplicationTickData& tickData);
	void drawContentTreeView(ProjectContentEntryRef entry);
	void drawContentSnapShot(ProjectContentEntryRef entry);
	void drawItemSnapshot(float drawDimSize, ProjectContentEntryRef entry);
	void drawRightClickedMenu();
	void drawAssetImport();

private:
	// Index of content widget.
	size_t m_index;

	// Current content active folder.
	ProjectContentEntryWeak m_activeFolder;

	// Filter to show entry.
	ImGuiTextFilter m_filter;

	// Tree view hovering entry.
	ProjectContentEntryWeak m_treeviewHoverEntry;

	// Asset selection state.
	Selection<ProjectContentEntrySelect> m_selections;

	// Event handle on tree update.
	chord::EventHandle m_onTreeUpdateHandle;

	// Open entry id.
	std::set<chord::uint64> m_openedEntry;

	// UI draw relatives.
	float m_snapshotItemIconSize;
};