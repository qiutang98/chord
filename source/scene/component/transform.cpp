#include <scene/scene_node.h>
#include <scene/component/transform.h>
#include <ui/ui_helper.h>

namespace chord
{
	UIComponentDrawDetails Transform::createComponentUIDrawDetails()
	{
		UIComponentDrawDetails result {};

		result.name = "Transform";
		result.bOptionalCreated = false; // Transform not optional create for node.
		result.decoratedName = ICON_FA_ASTERISK + std::string("  Transform");
		result.factory = []() { return std::make_shared<Transform>(); };
		result.onDrawUI = [](ComponentRef component) -> bool
		{
			auto transform = std::static_pointer_cast<Transform>(component);

			const float sizeLable = ImGui::GetFontSize() * 1.5f;

			bool bChangeTransform = false;
			math::vec3 anglesRotate = math::degrees(transform->getRotation());

			bChangeTransform |= ui::drawVector3("  P  ", transform->getTranslation(), math::vec3(0.0f), sizeLable);
			bChangeTransform |= ui::drawVector3("  R  ", anglesRotate, math::vec3(0.0f), sizeLable);
			bChangeTransform |= ui::drawVector3("  S  ", transform->getScale(), math::vec3(1.0f), sizeLable);

			if (bChangeTransform)
			{
				transform->getRotation() = math::radians(anglesRotate);
				transform->invalidateWorldMatrix();
			}

			return bChangeTransform;
		};

		return result;
	}

	const UIComponentDrawDetails Transform::kComponentUIDrawDetails = Transform::createComponentUIDrawDetails();

	void Transform::tick(const ApplicationTickData& tickData)
	{
		m_prevWorldMatrix = m_worldMatrix;
		updateWorldTransform();
	}

	void Transform::invalidateWorldMatrix()
	{
		m_bUpdateFlag = true;

		// also notify all children their transform dirty.
		auto& children = getNode()->getChildren();
		for (auto& child : children)
		{
			child->getTransform()->invalidateWorldMatrix();
		}
	}

	void Transform::setTranslation(const glm::vec3& translation)
	{
		m_translation = translation;
		invalidateWorldMatrix();

		markDirty();
	}

	void Transform::setRotation(const glm::vec3& rotation)
	{
		m_rotation = rotation;
		invalidateWorldMatrix();

		markDirty();
	}

	void Transform::setScale(const glm::vec3& scale)
	{
		m_scale = scale;
		invalidateWorldMatrix();

		markDirty();
	}

	void Transform::setMatrix(const glm::mat4& matrix)
	{
		if (getNode()->getParent()->isRoot())
		{
			decomposeTransform(matrix, m_translation, m_rotation, m_scale);
			invalidateWorldMatrix();
		}
		else
		{
			const math::mat4 parentInverse = math::inverse(getNode()->getParent()->getTransform()->getWorldMatrix());
			const math::mat4 localNew = parentInverse * matrix;

			decomposeTransform(localNew, m_translation, m_rotation, m_scale);
			invalidateWorldMatrix();
		}

		markDirty();
	}

	glm::mat4 Transform::computeLocalMatrix() const
	{
		// TRS - style.
		return
			math::translate(glm::mat4(1.0f), m_translation) *
			math::toMat4(glm::quat(m_rotation)) *
			math::scale(glm::mat4(1.0f), m_scale);
	}


	void Transform::updateWorldTransform()
	{
		if (m_bUpdateFlag)
		{
			m_worldMatrix = computeLocalMatrix();
			auto parent = getNode()->getParent();

			// recursive multiply all parent's world matrix.
			if (parent)
			{
				auto transform = parent->getComponent<Transform>();
				m_worldMatrix = transform->getWorldMatrix() * m_worldMatrix;
			}

			m_bUpdateFlag = !m_bUpdateFlag;
		}
	}
}