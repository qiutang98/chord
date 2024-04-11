#include "project_content.h"
#include "../flower.h"

#include <project.h>

using namespace chord;
using namespace chord::graphics;

static uint64 requireProjectContentEntryId()
{
	static uint64 uniqueId = 0;
	return uniqueId++;
}

ProjectContentEntry::ProjectContentEntry(
	const chord::u16str& name,
	bool bFolder, 
	const std::filesystem::path& path, 
	std::shared_ptr<ProjectContentEntry> parent,
	ProjectContentEntryTree& tree)
	: m_hashId(requireProjectContentEntryId())
	, m_name(name)
	, m_bFolder(bFolder)
	, m_path(path.u16string())
	, m_parent(parent)
	, m_tree(tree)
{
	// Add in hash map.
	tree.m_entryMap[path] = this;
}

ProjectContentEntry::~ProjectContentEntry()
{
	// Remove self node in tree when release.
	m_tree.m_entryMap.erase(m_path.u16());
}

ImTextureID ProjectContentEntry::getSet(ImVec2& outUv0, ImVec2& outUv1)
{
	ImTextureID result;
	ImVec2 uv0, uv1;
	{
		uv0 = { 0.0f, 0.0f };
		uv1 = { 1.0f, 1.0f };
		math::vec2 kUvScale = { 0.02f, 0.02f };

		const auto& builtinResources = Flower::get().getBuiltinTextures();
		if (m_bFolder)
		{
			result = builtinResources.folderImage->requireView(kDefaultImageSubresourceRange, VK_IMAGE_VIEW_TYPE_2D, true, false).SRV.get();
		}
		else
		{
			kUvScale.x = 0.2f;
			kUvScale.y = 0.15f;
			result = builtinResources.fileImage->requireView(kDefaultImageSubresourceRange, VK_IMAGE_VIEW_TYPE_2D, true, false).SRV.get();
		}

		uv0 = -kUvScale;
		uv1 = 1.0f + kUvScale;
	}

	outUv0 = uv0;
	outUv1 = uv1;
	return result;
}

void ProjectContentEntry::build(bool bRecursive)
{
	// Only folder node need loop.
	if (m_bFolder)
	{
		for (const auto& entry : std::filesystem::directory_iterator(m_path.u16()))
		{
			const bool bFolder = std::filesystem::is_directory(entry);

			// Only scan meta file.
			const bool bSkip = (!bFolder) && (!entry.path().extension().string().starts_with(".meta"));
			if (!bSkip)
			{
				auto u16FileNameString = entry.path().filename().replace_extension().u16string();

				auto child = std::make_shared<ProjectContentEntry>(
					u16FileNameString,
					bFolder,
					entry.path(),
					shared_from_this(),
					m_tree);

				// Add in children.
				m_children.push_back(child);

				if (bRecursive)
				{
					// Recursive build.
					child->build(bRecursive);
				}
			}
		}

		// Sort: folder first, then sort by name.
		std::sort(m_children.begin(), m_children.end(), [](const auto& A, const auto& B)
		{
			if ((A->isFoleder() && B->isFoleder()) || (!A->isFoleder() && !B->isFoleder()))
			{
				return A->getName().u8() < B->getName().u8();
			}
			return A->isFoleder();
		});
	}
}

ProjectContentEntryTree::~ProjectContentEntryTree()
{
	release();
}

void ProjectContentEntryTree::release()
{
	m_root = nullptr;
	ensureMsgf(m_entryMap.empty(), "All entry should auto unregistered before rlease.");
}

void ProjectContentEntryTree::build()
{
	check(Project::get().isSetup());

	// Always clear entry when call build.
	release();

	const auto& projectConfig = Project::get().getPath();
	m_root = std::make_shared<ProjectContentEntry>(
		u16str("Asset"), 
		true, 
		projectConfig.assetPath.u16(),
		nullptr,
		*this);

	m_root->build(true);
}

ProjectContentManager::ProjectContentManager()
{
	// Build asset tree.
	m_assetTree.build();

	// Call event after build.
	onTreeUpdate.broadcast(m_assetTree);
}

ProjectContentManager::~ProjectContentManager()
{
	
}

void ProjectContentManager::tick(const chord::ApplicationTickData& tickData)
{


}