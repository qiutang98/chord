#pragma once

#include <ui/widget.h>
#include <utils/utils.h>
#include <utils/cvar.h>


class CVarCommandContext
{
public:
	void init()
	{
		memset(inputBuffer, 0, sizeof(inputBuffer));
		historyPos = -1;
	}

	void execCommand(const char* command, std::function<void(const std::string&)>&& printLog);

	void drawItems(ImVec2 tipPos);

public:
	// Command input buffer.
	char inputBuffer[256];

	// -1 is new line, [0, m_historyCommands.size() - 1] is browsing history.
	chord::int32 historyPos;

	// Command item pop states.
	bool bCommandSelectPop = false;
	chord::int32 selectedCommandIndex = -1;

	// History commands, per console widget.
	std::vector<std::string> historyCommands;

	// Current active commands.
	std::vector<chord::CVarSystem::CacheCommand> activeCommands;

public:
	static int textEditCallbackStub(ImGuiInputTextCallbackData* data);

private:
	chord::int32 textEditCallback(ImGuiInputTextCallbackData* data);
	void inputCallbackOnComplete(ImGuiInputTextCallbackData* data);
	void inputCallbackOnEdit(ImGuiInputTextCallbackData* data);
	void inputCallbackOnHistory(ImGuiInputTextCallbackData* data);
};

class WidgetConsole : public chord::IWidget
{
public:
	explicit WidgetConsole();

	// Get widget show name.
	static const std::string& getShowName();

	// Add single log in widget.
	void addLog(const std::string& info, chord::ELogLevel type);

protected:
	// event init.
	virtual void onInit() override;

	// event always tick.
	virtual void onTick(const chord::ApplicationTickData& tickData) override;

	// event when widget visible tick.
	virtual void onVisibleTick(const chord::ApplicationTickData& tickData) override;

	// event release.
	virtual void onRelease() override;

private:
	// Clear all logs item.
	void clearLog();

private:
	chord::EventHandle m_logCacheHandle;

	// CVar command context.
	CVarCommandContext m_cvarCommandCtx;

	// filter for log items.
	ImGuiTextFilter m_filter;

	// whether auto scroll if log items full.
	bool m_bAutoScroll = true;
	bool m_bScrollToBottom;

	// Log items deque.
	std::mutex m_asyncLogItemslock;
	std::deque<std::pair<std::string, chord::ELogLevel>> m_asyncLogItems;
	std::deque<std::pair<std::string, chord::ELogLevel>> m_logItems;

	// Max log item store.
	static const chord::uint32 kMaxLogsItemNum = 200;

	// Current hover item index.
	chord::int32 m_hoverItem = -1;

	// Popup menu state.
	bool m_bLogItemMenuPopup = false;

	// Log visibility.
	bool m_logVisible[(size_t)chord::ELogLevel::COUNT] =
	{
		true,  // Trace
		true,  // Info
		true,  // Warn
		true,  // Error
		true,  // Fatal
	};

	chord::uint32 m_logTypeCount[(size_t)chord::ELogLevel::COUNT] =
	{
		0, // Trace
		0, // Info
		0, // Warn
		0, // Error
		0, // Fatal
	};
};