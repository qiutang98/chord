#pragma once
#include <utils/utils.h>
#include <shader/base.h>

namespace chord
{
	// UNIT: Meter.
	// 
	class ICamera
	{
	public:
		// return camera view matrix.
		virtual const math::dmat4& getViewMatrix() const = 0;

		// return camera project matrix.
		virtual const math::mat4& getProjectMatrix() const = 0;

	public:
		// return camera aspect.
		inline float getAspect() const 
		{
			return (float)m_width / (float)m_height; 
		}

		// return camera fov y.
		inline float getFovY() const 
		{
			return m_fovy;
		}

		// Set fov y value in radians.
		inline bool setFovY(float v)
		{
			if (v != m_fovy)
			{
				m_fovy = v;
				return true;
			}

			return false;
		}

		// return camera z near plane.
		inline double getZNear() const
		{ 
			return m_zNear; 
		}

		// return camera z far plane.
		inline double getZFar() const 
		{ 
			// Z far, <= 0.0f meaning infinite z.
			return m_zFar; 
		}

		// return camera worldspcae position.
		inline const math::dvec3& getPosition() const 
		{ 
			return m_position; 
		}

		void fillViewUniformParameter(PerframeCameraView& view) const;

	protected:
		// world space position.
		math::dvec3 m_position = { 25.0f, 25.0f, 25.0f };

		// fov y.
		float m_fovy = math::radians(45.0f);

		// z near.
		double m_zNear = 0.01; // 1 CM.

		// z far, 0.0 meaning infinite z far.
		double m_zFar = 0.0;

		// render width.
		size_t m_width;

		// render height.
		size_t m_height;

		// camera front direction.
		math::dvec3 m_front = { -0.5f, -0.6f, 0.6f };

		// camera up direction.
		math::dvec3 m_up;

		// camera right direction;
		math::dvec3 m_right;
	};
}