#pragma once

#include <utils/utils.h>
#include <utils/delegate.h>

// Generic selection.
template<typename T>
class Selection
{
public:
	const auto& getSelections() const 
	{ 
		return m_selections; 
	}

	const T& getElem(size_t i) const
	{
		return m_selections.at(i);
	}

	bool isSelected(const T& t) const
	{
		return std::binary_search(m_selections.begin(), m_selections.end(), t);
	}

	auto num() const 
	{ 
		return m_selections.size(); 
	}

	bool empty() const
	{
		return m_selections.empty();
	}

	void clear()
	{
		m_selections.clear();
		onChanged.broadcast(*this);
	}

	void add(const T& in)
	{
		m_selections.push_back(in);
		sortSelection();

		onChanged.broadcast(*this);
	}

	void remove(const T& t)
	{
		std::erase_if(m_selections, [&t](const T& v)
		{
			return t == v;
		});

		sortSelection();
		onChanged.broadcast(*this);
	}

	// On selection content change event.
	chord::Events<Selection<T>, Selection<T>&> onChanged;

private:
	void sortSelection()
	{
		std::sort(m_selections.begin(), m_selections.end());
	}

private:
	std::vector<T> m_selections;
};