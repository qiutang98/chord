#pragma once
#include <utils/utils.h>

namespace chord
{
    class ICamera : NonCopyable
    {
    public:


    protected:
        // Position.
        math::dvec3 m_position;

        // fov y.
        float m_fovy;

        // 
        double m_zNear;

    };
}