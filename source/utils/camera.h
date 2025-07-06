#pragma once
#include <utils/utils.h>
#include <shader/base.h>

namespace chord
{
	struct Frustum
	{
		std::array<math::vec4, 6> planes;
		enum ESide
		{
			eLeft,
			eDown,
			eRight,
			eTop,
			eFront,
			eBack
		};
	};


	// UNIT: Meter.
	// 
	class ICamera
	{
	public:
		// return camera view matrix.
		virtual const math::mat4& getRelativeCameraViewMatrix() const = 0;

		// return camera project matrix.
		virtual const math::mat4& getProjectMatrix() const = 0;

		// Default project matrix use infinite z, this function provide matrix exist valid zfar.
		virtual const math::mat4& getProjectMatrixExistZFar() const = 0;

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

		// Get last frame position.
		inline const math::dvec3& getLastPosition() const
		{
			return m_positionLast;
		}

		void fillViewUniformParameter(PerframeCameraView& view) const;

		Frustum computeRelativeWorldFrustum() const;

		const auto& getFront() const { return m_front; }

		void markVisible() { m_bVisible = true; }
		void markInvisible() { m_bVisible = false; }
		bool isVisible() const { return m_bVisible; }

	protected:
		bool m_bVisible = false;

		// world space position.
		math::dvec3 m_position = { 25.0f, 25.0f, 25.0f };
		math::dvec3 m_positionLast = { 25.0f, 25.0f, 25.0f };

		// fov y.
		float m_fovy = math::radians(45.0f);

		// z near.
		double m_zNear = 0.001; //

		// z far.
		double m_zFar = 20000.0; // z far use for distance culling or other things.

		// render width.
		size_t m_width = 1;

		// render height.
		size_t m_height = 1;

		// camera front direction.
		math::dvec3 m_front = { -0.5, -0.6, 0.6 };

		// camera up direction.
		math::dvec3 m_up;

		// camera right direction;
		math::dvec3 m_right;
	};
}