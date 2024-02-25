#pragma once

#include <utils/utils.h>
#include <utils/noncopyable.h>

namespace chord
{
	class EngineInterface;

	class RuntimeModuleTickData
	{

	};

	class IRuntimeModule : NonCopyable
	{
	public:
		explicit IRuntimeModule() = default;
		virtual ~IRuntimeModule() = default;

		// Used to check module dependency.
		virtual void registerCheck(EngineInterface* engine) { }

		virtual bool init() = 0;
		virtual bool tick(const RuntimeModuleTickData& tickData) = 0;

		virtual bool beforeRelease() { return true; }
		virtual bool release() = 0;

		const EngineInterface& getEngine() const { return m_engine; }

	protected:
		EngineInterface& m_engine;
	};
}