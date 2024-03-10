#pragma once

#include <utils/utils.h>
#include <utils/noncopyable.h>

namespace chord
{
	namespace graphics
	{
		class Context;
		class Swapchain;
	}

	class Renderer : public ISubsystem
	{
	public:
		explicit Renderer(const graphics::Context& context);

	protected:
		virtual void beforeRelease() override;
		virtual bool onInit() override;
		virtual bool onTick(const SubsystemTickData& tickData) override;
		virtual void onRelease() override;

	private:
		const graphics::Context& m_context;
	};
}