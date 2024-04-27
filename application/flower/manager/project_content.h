#pragma once

#include <utils/utils.h>
#include <application/application.h>
#include <asset/asset.h>

#include "../selection.h"

class ProjectContentEntryTree;

// Simple VFS map, entry is a file in the disk.
// Also relative to asset sytem.
class ProjectContentEntry final
	: public std::enable_shared_from_this<ProjectContentEntry>
	, chord::NonCopyable
{
	friend ProjectContentEntryTree;

public:
	explicit ProjectContentEntry(
		const chord::u16str& name, 
		bool bFolder, 
		const std::filesystem::path& path, 
		std::shared_ptr<ProjectContentEntry> parent,
		ProjectContentEntryTree& tree);

	~ProjectContentEntry();

	ImTextureID getSet(ImVec2& uv0, ImVec2& uv1);

	bool isFoleder() const 
	{ 
		return m_bFolder; 
	}

	bool isChildrenEmpty() const 
	{ 
		return m_children.empty(); 
	}

	const auto& getChildren() const 
	{ 
		return m_children; 
	}

	auto& getChildren() 
	{ 
		return m_children; 
	}

	const auto& getParent() 
	{
		return m_parent; 
	}

	const chord::u16str& getName() const 
	{ 
		return m_name; 
	}

	const auto& getPath() const 
	{ 
		return m_path;
	}

	const auto& getHashId() const 
	{ 
		return m_hashId;
	}

	void markDirty()
	{
		m_bDirty = true;
	}

private:
	void build(bool bRecursive);

	void update();

private:
	ProjectContentEntryTree& m_tree;

	// Entry hash id.
	const chord::uint64 m_hashId;

	// Entry path.
	chord::u16str m_path;

	// Entry name.
	chord::u16str m_name;

	// Current entry is folder or not.
	bool m_bFolder : 1;     
	bool m_bDirty  : 1; 

	// Hierarchy structural.
	std::weak_ptr<ProjectContentEntry> m_parent;
	std::vector<std::shared_ptr<ProjectContentEntry>> m_children;

	// Cache asset system reference.

};
using ProjectContentEntryRef  = std::shared_ptr<ProjectContentEntry>;
using ProjectContentEntryWeak = std::weak_ptr<ProjectContentEntry>;

class ProjectContentEntryTree : chord::NonCopyable
{
	friend ProjectContentEntry;

public:
	explicit ProjectContentEntryTree() = default;
	~ProjectContentEntryTree();

	// Get root of current content.
	auto getRoot() const
	{ 
		return m_root;
	}

	// Get entry in map.
	ProjectContentEntry& getEntry(const std::filesystem::path& path)
	{
		return *m_entryMap.at(path);
	}

	const ProjectContentEntry& getEntry(const std::filesystem::path& path) const
	{
		return *m_entryMap.at(path);
	}

	// Fully build.
	void build();

	void update();

	ProjectContentEntry* getClosetFolder(chord::AssetRef asset) const;

private:
	void release();

private:
	// Root of folder map.
	ProjectContentEntryRef m_root;

	// Cache all entry map, hashed by path, auto registered in tree node construction and deconstruction.
	std::map<std::filesystem::path, ProjectContentEntry*> m_entryMap;
};

struct DragAndDropAssets
{
	void clear()
	{
		selectAssets.clear();
	}

	std::set<std::filesystem::path> selectAssets;
};

using SnapshotCache = chord::LRUCache<chord::graphics::GPUTextureAsset, std::filesystem::path>;

class ProjectContentManager final : chord::NonCopyable
{
public:
	// Init project content.
	explicit ProjectContentManager();
	~ProjectContentManager();

	// Event on asset tree update.
	chord::Events<ProjectContentManager, const ProjectContentEntryTree&> onTreeUpdate;

	const auto& getTree() const
	{
		return m_assetTree;
	}

	auto& getDragDropAssets() 
	{ 
		return m_dragDropAssets; 
	}

	const auto& getDragDropAssets() const 
	{ 
		return m_dragDropAssets; 
	}

	void clearDragDropAssets() 
	{
		m_dragDropAssets.clear(); 
	}

	static const char* getDragDropAssetsName() 
	{
		return "flower_ContentAssetDragDrops";
	}

	const SnapshotCache& getSnapshotCache() const
	{
		return *m_snapshots;
	}

	SnapshotCache& getSnapshotCache()
	{
		return *m_snapshots;
	}

	bool existDirtyAsset() const
	{
		return !m_dirtyAssets.empty();
	}

	template<typename T>
	std::vector<std::shared_ptr<T>> getDirtyAsset() const
	{
		std::vector<std::shared_ptr<T>> result{ };
		for (const auto& id : m_dirtyAssets)
		{
			if (auto sp = std::dynamic_pointer_cast<T>(m_assetManager->at(id)))
			{
				result.push_back(sp);
			}
		}
		return result;
	}

private:
	chord::AssetManager* m_assetManager;

	// Project asset tree.
	ProjectContentEntryTree m_assetTree;

	// Drag and droping assets.
	DragAndDropAssets m_dragDropAssets;

	// Lru cache.
	SnapshotCache* m_snapshots = nullptr;

	// Cache all dirty asset.
	chord::EventHandle m_onAssetSavedHandle;
	chord::EventHandle m_onAssetNewlySavedHandle;
	chord::EventHandle m_onAssetDirtyHandle;

	chord::EventHandle m_onAssetRemoveHandle;
	chord::EventHandle m_onAssetInsertHandle;

	std::unordered_set<chord::uint64> m_dirtyAssets;
};