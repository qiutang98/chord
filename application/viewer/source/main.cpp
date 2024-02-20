#include <application/application.h>
#include <utils/delegate.h>
#include <iostream>
#include <utils/cvar.h>
#include <utils/log.h>
#include <graphics/graphics.h>

using namespace chord;

AutoCVar<int32> cVarTest("r.test", 1, "hello r.test", EConsoleVarFlags::ReadOnly, [](const auto& oldValue, const auto& newValue)
{
	std::cout << "old:" << oldValue << ", new:" << newValue <<std::endl;
});

static float gSfsd = 6.6f;
AutoCVarRef<float> cVarSdfRef("r.ref", gSfsd, "hello ref", EConsoleVarFlags::ReadOnly);

int main(int argc, const char** argv)
{
	auto& app = chord::Application::get();
	 
	cVarTest.set(44);
	cVarTest.reset();

	cVarSdfRef.set(7.4f);
	std::cout << gSfsd << std::endl;

	cVarSdfRef.reset();
	std::cout << cVarSdfRef.get() << std::endl;

	std::cout << cVarSdfRef.getPtr()->getName() << "..." << cVarSdfRef.getPtr()->getDescription() << std::endl;


	static auto* cVarTest2 = CVarSystem::get().getCVarCheck<int32>("r.test");
	if (cVarTest2)
	{
		std::cout << cVarTest2->get() << std::endl;
		std::cout << cVarTest2->getName() << std::endl;
		std::cout << cVarTest2->getDescription() << std::endl;
	}

	LOG_GRAPHICS_ERROR("Error graphics test.");

	for (uint32 i = 2; i < 10; i++)
	{
		ENSURE_GRAPHICS(false, "Hello {0}.", i);
		LOG_ERROR("Hello Error {0}.", i);
	}
	LOG_WARN("Warning!");  
	return 0;
}