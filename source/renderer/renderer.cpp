#include <graphics/graphics.h>
#include <renderer/renderer.h>
#include <graphics/helper.h>

namespace chord
{
	Renderer::Renderer(const graphics::Context& context)
		: ISubsystem("Renderer")
		, m_context(context)
	{

	}



	bool Renderer::onInit()
	{
		return true;
	}

	bool Renderer::onTick(const SubsystemTickData& tickData)
	{
		return true;
	}

	void Renderer::beforeRelease()
	{

	}

	void Renderer::onRelease()
	{

	}


}
