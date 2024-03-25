#include <utils/engine.h>
#include <application/application.h>
#include <graphics/graphics.h>

namespace chord
{
	Engine::Engine(const Application& application)
		: m_application(application)
	{

	}

	Engine::~Engine()
	{
		// Subsystem release.
		for (size_t i = m_subsystems.size(); i > 0; i--)
		{
			m_subsystems[i - 1]->release();
		}

		// Clear map.
		m_registeredSubsystemMap.clear();
		m_subsystems.clear();
	}

	bool Engine::init(const EngineInitConfig& config)
	{
		bool bResult = true;
		if(bResult)
		{

		}

		return bResult;
	}

	bool Engine::tick(const ApplicationTickData& tickData)
	{
		// Pre-return if no subsystem alive.
		if (m_subsystems.empty())
		{
			return false;
		}

		// Engine should continue or not.
		bool bContinue = true;

		// Loop all subsystems.
		for (auto& subsystem : m_subsystems)
		{
			if (!subsystem->tick(m_subsystemTickData))
			{
				bContinue = false;
				break;
			}
		}

		// Update tick data.
		m_subsystemTickData.tickCount ++;

		return bContinue;
	}

	void Engine::beforeRelease()
	{
		// Subsystem before release.
		for (size_t i = m_subsystems.size(); i > 0; i--)
		{
			m_subsystems[i - 1]->beforeRelease();
		}
	}
}

