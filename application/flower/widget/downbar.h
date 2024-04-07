#pragma once
#include <ui/widget.h>

class DownbarWidget : public chord::IWidget
{
public:
	explicit DownbarWidget();

	virtual void onTick(const chord::ApplicationTickData& tickData) override;
};