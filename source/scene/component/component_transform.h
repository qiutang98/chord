#pragma once

#include <scene/component.h>

namespace chord
{
	class Transform : public Component
	{
		REGISTER_BODY_DECLARE(Component);

	public:
		static const UIComponentDrawDetails kComponentUIDrawDetails;

		Transform() = default;
		Transform(SceneNodeRef sceneNode) : Component(sceneNode) { }

		virtual ~Transform() = default;

		// Interface override.
		virtual void tick(const ApplicationTickData& tickData) override;
		// ~Interface override.
		
		// Getter.
		math::dvec3& getTranslation() { return m_translation; }
		const math::dvec3& getTranslation() const { return m_translation; }
		math::dvec3& getRotation() { return m_rotation; }
		const math::dquat& getRotation() const { return m_rotation; }
		math::dvec3& getScale() { return m_scale; }
		const math::dvec3& getScale() const { return m_scale; }

		// Mark world matrix dirty, also change child's dirty state.
		void invalidateWorldMatrix();

		// setter.
		void setTranslation(const math::dvec3& translation);
		void setRotation(const math::dvec3& rotation);
		void setScale(const math::dvec3& scale);
		void setMatrix(const math::dmat4& matrix);

		// Get final world matrix. relate to parent.
		const math::dmat4& getWorldMatrix() const { return m_worldMatrix; }

		// Get last tick world matrix result.
		const math::dmat4& getPrevWorldMatrix() const 
		{ 
			[[unlikely]] 
			if (m_prevWorldMatrix[0][0] == std::numeric_limits<double>::quiet_NaN())
			{ 
				return m_worldMatrix; 
			}

			return m_prevWorldMatrix; 
		}

		void updateWorldTransform();

	private:
		static UIComponentDrawDetails createComponentUIDrawDetails();

	protected:
		// Compute local matrix.
		math::dmat4 computeLocalMatrix() const;

	protected:
		// need update?
		bool m_bUpdateFlag = true;

		// world space matrix.
		math::dmat4 m_worldMatrix = math::dmat4(1.0);

		// Prev-frame world matrix.
		math::dmat4 m_prevWorldMatrix = math::dmat4(std::numeric_limits<double>::quiet_NaN());

	protected:
		math::dvec3 m_translation = { 0.0, 0.0, 0.0 };
		math::dvec3 m_rotation    = { 0.0, 0.0, 0.0 };
		math::dvec3 m_scale       = { 1.0, 1.0, 1.0 };
	};
}
