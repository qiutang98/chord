#include <utils/camera.h>

namespace chord
{
    void ICamera::fillViewUniformParameter(PerframeCameraView& outUB) const
    {
        const math::mat4& realtiveView = getRelativeCameraViewMatrix();

        const math::mat4& projection = getProjectMatrix();
        const math::mat4 viewProjection = projection * realtiveView;

        outUB.translatedWorldToView = realtiveView;
        outUB.viewToTranslatedWorld = math::inverse(realtiveView);

        outUB.viewToClip = projection;
        outUB.clipToView = math::inverse(projection);

        outUB.translatedWorldToClip = viewProjection;
        outUB.clipToTranslatedWorld = math::inverse(viewProjection);

		const Frustum frustum = computeRelativeWorldFrustum();
		outUB.frustumPlane[0] = frustum.planes[0];
		outUB.frustumPlane[1] = frustum.planes[1];
		outUB.frustumPlane[2] = frustum.planes[2];
		outUB.frustumPlane[3] = frustum.planes[3];
		outUB.frustumPlane[4] = frustum.planes[4];
		outUB.frustumPlane[5] = frustum.planes[5];

		outUB.camMiscInfo.x = m_fovy;
    }

	Frustum ICamera::computeRelativeWorldFrustum() const
	{
        const math::vec3 forwardVector = math::normalize(m_front);
		const math::vec3 upVector = math::normalize(m_up);
		const math::vec3 rightVector = math::normalize(m_right);

        const math::vec3 camWorldPos = math::vec3(0.0f);
        const float tanFovyHalf = math::tan(getFovY() * 0.5f);
        const float aspect = getAspect();

        const math::vec3 nearC = camWorldPos + forwardVector * float(m_zNear);
        const math::vec3 farC = camWorldPos + forwardVector * float(m_zFar);

		const float yNearHalf = m_zNear * tanFovyHalf;
		const float yFarHalf = m_zFar * tanFovyHalf;

		const math::vec3 yNearHalfV = yNearHalf * upVector;
		const math::vec3 xNearHalfV = yNearHalf * aspect * rightVector;

		const math::vec3 yFarHalfV = yFarHalf * upVector;
		const math::vec3 xFarHalfV = yFarHalf * aspect * rightVector;

		const math::vec3 nrt = nearC + xNearHalfV + yNearHalfV;
		const math::vec3 nrd = nearC + xNearHalfV - yNearHalfV;
		const math::vec3 nlt = nearC - xNearHalfV + yNearHalfV;
		const math::vec3 nld = nearC - xNearHalfV - yNearHalfV;

		const math::vec3 frt = farC + xFarHalfV + yFarHalfV;
		const math::vec3 frd = farC + xFarHalfV - yFarHalfV;
		const math::vec3 flt = farC - xFarHalfV + yFarHalfV;
		const math::vec3 fld = farC - xFarHalfV - yFarHalfV;

		Frustum ret { };

		// p1 X p2, center is pC.
		auto getNormal = [](const math::vec3& pC, const math::vec3& p1, const math::vec3& p2)
		{
			const math::vec3 dir0 = p1 - pC;
			const math::vec3 dir1 = p2 - pC;
			const math::vec3 crossDir = math::cross(dir0, dir1);
			return math::normalize(crossDir);
		};

		// left 
		const math::vec3 leftN = getNormal(fld, flt, nld);
		ret.planes[Frustum::eLeft] = math::vec4(leftN, -math::dot(leftN, fld));

		// down
		const math::vec3 downN = getNormal(frd, fld, nrd);
		ret.planes[Frustum::eDown] = math::vec4(downN, -math::dot(downN, frd));

		// right
		const math::vec3 rightN = getNormal(frt, frd, nrt);
		ret.planes[Frustum::eRight] = math::vec4(rightN, -math::dot(rightN, frt));

		// top
		const math::vec3 topN = getNormal(flt, frt, nlt);
		ret.planes[Frustum::eTop] = math::vec4(topN, -math::dot(topN, flt));

		// front
		const math::vec3 frontN = getNormal(nrt, nrd, nlt);
		ret.planes[Frustum::eFront] = math::vec4(frontN, -math::dot(frontN, nrt));

		// back
		const math::vec3 backN = getNormal(frt, flt, frd);
		ret.planes[Frustum::eBack] = math::vec4(backN, -math::dot(backN, frt));

		return ret;
	}
}