#pragma once
#include <ui/widget.h>

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

private:
	// Index of content widget.
	size_t m_index;
};