#pragma once
#include <utils/utils.h>

namespace chord
{
	class ICamera
	{
	public:
		// return camera view matrix.
		virtual math::mat4 getViewMatrix() const = 0;

		// return camera project matrix.
		virtual math::mat4 getProjectMatrix() const = 0;

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
		inline float getZNear() const 
		{ 
			return m_zNear; 
		}

		// return camera z far plane.
		inline float getZFar() const 
		{ 
			// Z far, <= 0.0f meaning infinite z.
			return m_zFar; 
		}

		// return camera worldspcae position.
		inline const math::vec3& getPosition() const 
		{ 
			return m_position; 
		}

	protected:
		// world space position.
		math::vec3 m_position = { 25.0f, 25.0f, 25.0f };

		// fov y.
		float m_fovy = math::radians(45.0f);

		// z near.
		float m_zNear = 0.01f;

		// z far, 0.0f meaning infinite z far.
		float m_zFar = 0.0f;

		// render width.
		size_t m_width;

		// render height.
		size_t m_height;

		// camera front direction.
		math::vec3 m_front = { -0.5f, -0.6f, 0.6f };

		// camera up direction.
		math::vec3 m_up;

		// camera right direction;
		math::vec3 m_right;
	};
}