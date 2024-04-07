#pragma once
#include <utils/utils.h>

namespace chord
{
    // Timer which work in seconds unit.
    class Timer
    {
    private:
        std::chrono::system_clock::time_point m_timePoint  { };
        std::chrono::system_clock::time_point m_startPoint { };
        std::chrono::duration<double> m_deltaTime { 0.0 };

        // Total tickcount.
        uint64 m_tickCount = 0;

        // Immediate Fps.
        double m_fps = 0.0;

        // Smooth fps, which update per-second.
        double m_fpsUpdatedPerSecond = 0.0;

        // Delta time.
        double m_dt = 0.0;

        // 
        uint32 m_passFrame = 0;

        // 
        double m_passTime = 0.0;

    public:
        void init()
        {
            m_startPoint = std::chrono::system_clock::now();
            m_timePoint  = std::chrono::system_clock::now();
        }

        // return true if smooth fps update.
        bool tick()
        {
            m_deltaTime = std::chrono::system_clock::now() - m_timePoint;
            m_timePoint = std::chrono::system_clock::now();

            // When first time tick we set 60fps budget, otherwise use counter.
            m_dt = (m_tickCount == 0) ? 0.0166 : m_deltaTime.count();

            // Count plus.
            m_tickCount ++;

            // Update smooth fps per-second.
            m_passFrame ++;
            m_passTime += m_dt;

            const bool bFpsUpdatePerSecond = m_passTime >= 1.0;
            if (bFpsUpdatePerSecond)
            {
                m_fpsUpdatedPerSecond = m_passFrame / m_passTime;
                m_passTime  = 0.0;
                m_passFrame = 0U;
            }

            // Update fps every frame.
            m_fps = 1.0 / m_dt;

            return bFpsUpdatePerSecond;
        }

    public:
        // Realtime fps.
        auto getFps() const 
        { 
            return m_fps; 
        }

        // Get smooth fps, which update per seconds. update per-seconds.
        auto getFpsUpdatedPerSecond() const 
        { 
            return m_fpsUpdatedPerSecond; 
        }

        // Realtime delta time from last frame use time in seconds unit.
        auto getDt() const 
        { 
            return m_dt; 
        }

        // Get tick count.
        auto getTickCount() const 
        { 
            return m_tickCount; 
        }

        // Get app total runtime in seconds.
        auto getRuntime() const
        {
            std::chrono::duration<double> dd = std::chrono::system_clock::now() - m_startPoint;
            return dd.count(); 
        }
    };
}