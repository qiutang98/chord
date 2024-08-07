#include "console.h"

#include <regex>
#include <ui/ui_helper.h>
using namespace chord::graphics;

using namespace chord;
using namespace chord::ui;

#pragma warning(disable: 4996)

constexpr const char* kIconConsoleClone  = ICON_FA_COPY;
constexpr const char* kIconConsoleClear  = ICON_FA_NONE;
constexpr const char* kIconConsoleTitle  = ICON_FA_COMMENT;
constexpr const char* kIconConsoleFilter = ICON_FA_MAGNIFYING_GLASS;

static inline bool isConsoleEditable(EConsoleVarFlags flags)
{
	const bool bProjectIni = uint32(flags) & uint32(EConsoleVarFlags::ProjectIni);
	const bool bReadOnly   = uint32(flags) & uint32(EConsoleVarFlags::ReadOnly);

	return (!bProjectIni) && (!bReadOnly);
}

static inline int myStricmp(const char* s1, const char* s2)
{
	int d;
	while ((d = toupper(*s2) - toupper(*s1)) == 0 && *s1)
	{
		s1++;
		s2++;
	}
	return d;
}

static inline int myStrnicmp(const char* s1, const char* s2, int n)
{
	int d = 0;
	while (n > 0 && (d = toupper(*s2) - toupper(*s1)) == 0 && *s1)
	{
		s1++;
		s2++;
		n--;
	}
	return d;
}

static inline void myStrtrim(char* s)
{
	char* strEnd = s + strlen(s);
	while (strEnd > s && strEnd[-1] == ' ')
	{
		strEnd--;
	}

	*strEnd = 0;
}

static inline std::string myStrdup(const char* s)
{
	auto len = strlen(s) + 1;
	std::string result;
	result.resize(len);
	memcpy(result.data(), s, len);
	return result;
}

// Widget console.

WidgetConsole::WidgetConsole()
	: IWidget(combineIcon("Console", kIconConsoleTitle).c_str(), combineIcon("Console", kIconConsoleTitle).c_str())
{

}

const std::string& WidgetConsole::getShowName()
{
	static const auto name = combineIcon("Console", kIconConsoleTitle);
	return name;
}

void WidgetConsole::onInit()
{
	m_cvarCommandCtx.init();

	clearLog();

	m_bScrollToBottom = false;
	m_bAutoScroll = true;

	m_logCacheHandle = LoggerSystem::get().pushCallback([this](const std::string& info, ELogType type)
	{
		this->addLog(info, type);
	});
}

void WidgetConsole::onRelease()
{
	LoggerSystem::get().popCallback(m_logCacheHandle);
	clearLog();
}

void WidgetConsole::onTick(const ApplicationTickData& tickData)
{
	m_name = getShowName();
}

void WidgetConsole::onVisibleTick(const ApplicationTickData& tickData)
{
	ImGui::Separator();

	// Log type visibility toggle.
	{
		const auto buttonLogTypeVisibilityToggle = [this](ELogType index, std::string Name)
		{
			bool& visibility = m_logVisible[(size_t)index];
			std::string numCount = (m_logTypeCount[(size_t)index] <= 99) ? std::format("{}", m_logTypeCount[(size_t)index]) : "99+";
			ImGui::Checkbox(std::format(" {} [{}] ", Name, numCount).c_str(), &visibility);
		};

		const std::string sFilterName = combineIcon(utf8::utf16to8(u"查找"), kIconConsoleFilter);

		m_filter.Draw(sFilterName.c_str(), 180);
		ImGui::SameLine();

		buttonLogTypeVisibilityToggle(ELogType::Trace, utf8::utf16to8(u"通知"));
		ImGui::SameLine();

		buttonLogTypeVisibilityToggle(ELogType::Info, utf8::utf16to8(u"讯息"));
		ImGui::SameLine();

		buttonLogTypeVisibilityToggle(ELogType::Warn, utf8::utf16to8(u"警告"));
		ImGui::SameLine();

		buttonLogTypeVisibilityToggle(ELogType::Error, utf8::utf16to8(u"错误"));
		ImGui::SameLine();

		buttonLogTypeVisibilityToggle(ELogType::Other, utf8::utf16to8(u"其他"));
	}

	ImGui::Separator();

	// Reserve enough left-over height for 1 separator + 1 input text
	const float footerHeightToReserve = ImGui::GetStyle().ItemSpacing.y + ImGui::GetFrameHeightWithSpacing();
	ImGui::BeginChild("ScrollingRegion", ImVec2(0, -footerHeightToReserve), false, ImGuiWindowFlags_HorizontalScrollbar);

	if (!ImGui::IsItemHovered() && !m_bLogItemMenuPopup)
	{
		m_hoverItem = -1;
	}

	// Tighten spacing
	ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4, 1));

	// Print log items.
	for (int i = 0; i < m_logItems.size(); i++)
	{
		const char* item = m_logItems[i].first.c_str();
		if (item == "" || !m_filter.PassFilter(item))
		{
			continue;
		}

		ImVec4 color;
		bool bHasColor = false;
		bool bOutSeparate = false;
		if (m_logItems[i].second == ELogType::Error)
		{
			if (!m_logVisible[size_t(ELogType::Error)])
			{
				continue;
			}

			color = ImVec4(1.0f, 0.08f, 0.08f, 1.0f);
			bHasColor = true;
		}
		else if (m_logItems[i].second == ELogType::Warn)
		{
			if (!m_logVisible[size_t(ELogType::Warn)])
			{
				continue;
			}

			color = ImVec4(1.0f, 1.0f, 0.1f, 1.0f);
			bHasColor = true;
		}
		else if (m_logItems[i].second == ELogType::Trace)
		{
			if (!m_logVisible[size_t(ELogType::Trace)])
			{
				continue;
			}
		}
		else if (m_logItems[i].second == ELogType::Info)
		{
			if (!m_logVisible[size_t(ELogType::Info)])
			{
				continue;
			}
		}
		else if (strncmp(item, "# ", 2) == 0)
		{
			if (!m_logVisible[size_t(ELogType::Other)])
			{
				continue;
			}
			bOutSeparate = true;
			color = ImVec4(1.0f, 0.8f, 0.6f, 1.0f);
			bHasColor = true;
		}
		else if (strncmp(item, "Help: ", 5) == 0)
		{
			if (!m_logVisible[size_t(ELogType::Other)])
			{
				continue;
			}

			color = ImVec4(1.0f, 0.6f, 0.0f, 1.0f);
			bHasColor = true;
		}
		else
		{
			if (!m_logVisible[size_t(ELogType::Other)])
			{
				continue;
			}
		}

		if (bOutSeparate)
		{
			ImGui::Separator();
		}

		if (bHasColor) ImGui::PushStyleColor(ImGuiCol_Text, color);
		{
			ImGui::Selectable(item, m_hoverItem == i);
		}
		if (bHasColor) ImGui::PopStyleColor();

		if (ImGui::IsItemHovered(ImGuiHoveredFlags_RectOnly) && !m_bLogItemMenuPopup)
		{
			m_hoverItem = i;
		}
	}

	if (ImGui::BeginPopupContextWindow())
	{
		m_bLogItemMenuPopup = true;

		std::string sCopyName  = combineIcon("Copy  Item ", kIconConsoleClone);
		std::string sClearName = combineIcon("Clear  All ", kIconConsoleClear);
		if (ImGui::Selectable(sClearName.c_str()))
		{
			clearLog();
		}
		ImGui::Spacing();

		if (m_hoverItem >= 0 && m_hoverItem < kMaxLogsItemNum)
		{
			if (ImGui::Selectable(sCopyName.c_str()))
			{
				ImGui::SetClipboardText(m_logItems[m_hoverItem].first.c_str());
			}
		}
		else
		{
			ImGui::TextDisabled(sCopyName.c_str());
		}
		ImGui::EndPopup();
	}
	else
	{
		m_bLogItemMenuPopup = false;
	}

	if (m_bScrollToBottom || (m_bAutoScroll && ImGui::GetScrollY() >= ImGui::GetScrollMaxY()))
	{
		ImGui::SetScrollHereY(1.0f);
	}

	m_bScrollToBottom = false;

	ImGui::PopStyleVar();
	ImGui::EndChild();
	ImGui::Separator();

	// Command-line
	bool bReclaimFocus = false;
	static const ImGuiInputTextFlags inputTextFlags =
		ImGuiInputTextFlags_EnterReturnsTrue |
		ImGuiInputTextFlags_CallbackEdit |
		ImGuiInputTextFlags_CallbackCompletion |
		ImGuiInputTextFlags_CallbackHistory;

	auto tipPos = ImGui::GetWindowPos();
	tipPos.x += ImGui::GetStyle().WindowPadding.x;
	tipPos.y += ImGui::GetWindowHeight() - (m_cvarCommandCtx.activeCommands.size() + 2.25f) * ImGui::GetTextLineHeightWithSpacing();

	// CVar Input.
	if (ImGui::InputText("CVar Input", m_cvarCommandCtx.inputBuffer, countof(m_cvarCommandCtx.inputBuffer), inputTextFlags, &CVarCommandContext::textEditCallbackStub, (void*)&m_cvarCommandCtx))
	{
		char* s = m_cvarCommandCtx.inputBuffer;
		myStrtrim(s);
		if (s[0])
		{
			m_cvarCommandCtx.execCommand(s, [this](const std::string& log) { addLog(log, ELogType::Other);  });
			m_cvarCommandCtx.activeCommands.clear();

			// On command input, we scroll to bottom even if AutoScroll==false
			m_bScrollToBottom = true;
		}
		strcpy(s, "");
		bReclaimFocus = true;
	}

	m_cvarCommandCtx.drawItems(tipPos);

	// Auto-focus on window apparition
	ImGui::SetItemDefaultFocus();
	if (bReclaimFocus)
	{
		ImGui::SetKeyboardFocusHere(-1); // Auto focus previous widget
	}
}

void WidgetConsole::clearLog()
{
	m_logItems.clear();
	for (auto& i : m_logTypeCount)
	{
		i = 0;
	}
}

void WidgetConsole::addLog(const std::string& info, ELogType type)
{
	if (static_cast<uint32>(m_logItems.size()) == kMaxLogsItemNum - 1)
	{
		m_logItems.pop_front();
	}

	m_logTypeCount[size_t(type)]++;
	m_logItems.push_back({ info, type });
}

///////////////////////////////////////////////////////////////////////////////////////
// CVar context.

void CVarCommandContext::execCommand(const char* command, std::function<void(const std::string&)>&& printLog)
{
	printLog(std::format("# {}\n", command));

	// Insert into history. First find match and delete it so it can be pushed to the back.
	// This isn't trying to be smart or optimal.
	historyPos = -1;
	for (int i = historyCommands.size() - 1; i >= 0; i--)
	{
		if (myStricmp(historyCommands[i].data(), command) == 0)
		{
			historyCommands.erase(historyCommands.begin() + i);
			break;
		}
	}
	historyCommands.push_back(myStrdup(command));

	std::string cmd_info = command;
	std::regex ws_re("\\s+"); // whitespace

	std::vector<std::string> tokens(
		std::sregex_token_iterator(cmd_info.begin(), cmd_info.end(), ws_re, -1),
		std::sregex_token_iterator()
	);

	auto cVar = CVarSystem::get().getCVarIfExistGeneric(tokens[0]);
	if (cVar != nullptr)
	{
		// Try to get command value.
		if (tokens.size() == 1)
		{
			static const std::string cmdBeginStr = "cmd.";
			if (tokens[0].rfind(cmdBeginStr, 0) == 0)
			{
				CVarSystem::get().getCVarCheck<bool>(tokens[0])->set(true);
			}
			else
			{
				// Query value.
				std::string val = CVarSystem::get().getValueIfExistGeneric(tokens[0]);
				if (val.empty())
				{
					printLog(std::format("Failed to query '{}' current value.", tokens[0]));
				}
				else
				{
					printLog(std::format("Help: {}", std::string(cVar->getDescription())));
					printLog("Current value: " + tokens[0] + " " + val);
				}
			}
		}
		// Try to set command value.
		else if (tokens.size() == 2)
		{
			bool bEditable = isConsoleEditable(cVar->getFlags());
			if (bEditable)
			{
				if (!CVarSystem::get().setValueIfExistGeneric(tokens[0], tokens[1]))
				{
					printLog(std::format("Set '{0}' with value '{1}' failed.", tokens[0], tokens[1]));
				}
			}
			else
			{
				printLog(std::format("{} is a readonly value, can't change on console!", tokens[0]));
			}
		}
		else
		{
			printLog(std::format("Error command parameters, all CVar only keep one parameter, but '{}' paramters input this time.", tokens.size() - 1));
		}
	}
	else
	{
		printLog(std::format("Unkonwn command: '{}'!", command));
	}
}

void CVarCommandContext::drawItems(ImVec2 tipPos)
{
	if (activeCommands.size() > 0 && ImGui::IsWindowFocused())
	{
		ImGui::SetNextWindowPos(tipPos);
		ImGui::BeginTooltip();
		for (size_t i = 0; i < activeCommands.size(); i++)
		{
			const bool bEditable = isConsoleEditable(activeCommands[i].storage->getFlags());
			if (!bEditable) ImGui::PushStyleColor(ImGuiCol_Text, math::vec4{ 1.0f, 1.0f, 1.0f, 0.25f });
			{
				ImGui::Selectable(activeCommands[i].name.data(), selectedCommandIndex == i);
			}
			if (!bEditable) ImGui::PopStyleColor();
		}
		ImGui::EndTooltip();
		bCommandSelectPop = true;
	}
	else
	{
		bCommandSelectPop = false;
		selectedCommandIndex = -1;
	}
}


void CVarCommandContext::inputCallbackOnHistory(ImGuiInputTextCallbackData* data)
{
	if (bCommandSelectPop)
	{
		if (data->EventKey == ImGuiKey_UpArrow)
		{
			if (selectedCommandIndex <= 0)
			{
				selectedCommandIndex = int(activeCommands.size()) - 1;
			}
			else
			{
				selectedCommandIndex--;
			}
		}
		else if (data->EventKey == ImGuiKey_DownArrow)
		{
			if (++selectedCommandIndex >= activeCommands.size())
			{
				selectedCommandIndex = 0;
			}
		}

		if (bCommandSelectPop && selectedCommandIndex >= 0 && selectedCommandIndex < activeCommands.size())
		{
			data->DeleteChars(0, data->BufTextLen);
			data->InsertChars(0, activeCommands[selectedCommandIndex].name.data());
		}

		return;
	}

	const int prevHistoryPos = historyPos;
	if (data->EventKey == ImGuiKey_UpArrow)
	{
		if (historyPos == -1)
		{
			historyPos = historyCommands.size() - 1;
		}
		else if (historyPos > 0)
		{
			historyPos--;
		}
	}
	else if (data->EventKey == ImGuiKey_DownArrow)
	{
		if (historyPos != -1)
		{
			if (++historyPos >= historyCommands.size())
			{
				historyPos = -1;
			}
		}
	}

	// A better implementation would preserve the data on the current input line along with cursor position.
	if (prevHistoryPos != historyPos)
	{
		const char* historyStr = (historyPos >= 0) ? historyCommands[historyPos].data() : "";
		data->DeleteChars(0, data->BufTextLen);
		data->InsertChars(0, historyStr);
	}
}

void CVarCommandContext::inputCallbackOnEdit(ImGuiInputTextCallbackData* data)
{
	// locate beginning of current word
	const char* wordEnd = data->Buf + data->CursorPos;
	const char* wordStart = wordEnd;
	while (wordStart > data->Buf)
	{
		const char c = wordStart[-1];
		if (c == ' ' || c == '\t' || c == ',' || c == ';')
		{
			break;
		}
		wordStart--;
	}

	// build a list of candidates
	std::vector<CVarSystem::CacheCommand> candidates { };
	const auto& cacheCommands = CVarSystem::get().getCacheCommands();
	for (auto i = 0; i < cacheCommands.size(); i++)
	{
		int size = (int)(wordEnd - wordStart);

		if (size > 0 && myStrnicmp(cacheCommands[i].name.data(), wordStart, size) == 0)
		{
			candidates.push_back(cacheCommands[i]);
		}
		else
		{
			std::string lowerCommand = cacheCommands[i].name.data();
			std::transform(lowerCommand.begin(), lowerCommand.end(), lowerCommand.begin(), ::tolower);

			if (size > 0 && myStrnicmp(lowerCommand.c_str(), wordStart, size) == 0)
			{
				candidates.push_back(cacheCommands[i]);
			}
		}
	}

	activeCommands.resize(candidates.size());
	for (auto i = 0; i < candidates.size(); i++)
	{
		activeCommands[i] = candidates[i];
	}
}

void CVarCommandContext::inputCallbackOnComplete(ImGuiInputTextCallbackData* data)
{
	// locate beginning of current word
	const char* wordEnd = data->Buf + data->CursorPos;
	const char* wordStart = wordEnd;
	while (wordStart > data->Buf)
	{
		const char c = wordStart[-1];
		if (c == ' ' || c == '\t' || c == ',' || c == ';')
		{
			break;
		}
		wordStart--;
	}

	// build a list of candidates
	std::vector<CVarSystem::CacheCommand> candidates;
	const auto& cacheCommands = CVarSystem::get().getCacheCommands();
	for (int i = 0; i < cacheCommands.size(); i++)
	{
		if (myStrnicmp(cacheCommands[i].name.data(), wordStart, (int)(wordEnd - wordStart)) == 0)
		{
			candidates.push_back(cacheCommands[i]);
		}
		else
		{
			std::string lowerCommand = cacheCommands[i].name.data();
			std::transform(lowerCommand.begin(), lowerCommand.end(), lowerCommand.begin(), ::tolower);

			if (myStrnicmp(lowerCommand.c_str(), wordStart, (int)(wordEnd - wordStart)) == 0)
			{
				candidates.push_back(cacheCommands[i]);
			}
		}
	}

	if (candidates.size() == 0)
	{
		// No match
	}
	else if (candidates.size() == 1)
	{
		// Single match. Delete the beginning of the word and replace it entirely so we've got nice casing.
		data->DeleteChars((int)(wordStart - data->Buf), (int)(wordEnd - wordStart));
		data->InsertChars(data->CursorPos, candidates[0].name.data());
		data->InsertChars(data->CursorPos, " ");
	}
	else
	{
		// Multiple matches. Complete as much as we can..
		// So inputing "C"+Tab will complete to "CL" then display "CLEAR" and "CLASSIFY" as matches.
		int matchLen = (int)(wordEnd - wordStart);
		for (;;)
		{
			int c = 0;
			bool bAllCandidatesMatches = true;
			for (int i = 0; i < candidates.size() && bAllCandidatesMatches; i++)
			{
				if (i == 0)
				{
					c = toupper(candidates[i].name[matchLen]);
				}
				else if (c == 0 || c != toupper(candidates[i].name[matchLen]))
				{
					bAllCandidatesMatches = false;
				}
			}

			if (!bAllCandidatesMatches)
			{
				break;
			}

			matchLen++;
		}

		if (matchLen > 0)
		{
			data->DeleteChars((int)(wordStart - data->Buf), (int)(wordEnd - wordStart));
			data->InsertChars(data->CursorPos, candidates[0].name.data(), candidates[0].name.data() + matchLen);
		}
	}
}

int32 CVarCommandContext::textEditCallback(ImGuiInputTextCallbackData* data)
{
	switch (data->EventFlag)
	{
	case ImGuiInputTextFlags_CallbackCompletion:
	{
		inputCallbackOnComplete(data);
		break;
	}
	case ImGuiInputTextFlags_CallbackEdit:
	{
		inputCallbackOnEdit(data);
		break;
	}
	case ImGuiInputTextFlags_CallbackHistory:
	{
		inputCallbackOnHistory(data);
		break;
	}
	}
	return 0;
}

int CVarCommandContext::textEditCallbackStub(ImGuiInputTextCallbackData* data)
{
	CVarCommandContext* ctx = (CVarCommandContext*)data->UserData;
	return ctx->textEditCallback(data);
}