#include <utils/camera.h>
#include <astrophysics/atmosphere.h>

namespace chord
{
	static inline GPUStorageDouble4 fillDouble3(const double3& v)
	{
		GPUStorageDouble4 r { };

		asuint(v.x, r.x.x, r.x.y);
		asuint(v.y, r.y.x, r.y.y);
		asuint(v.z, r.z.x, r.z.y);

		return r;
	}

    void ICamera::fillViewUniformParameter(PerframeCameraView& outUB) const
    {
		const float4x4& projectionWithZFar = getProjectMatrixExistZFar();

        const math::mat4& relativeView = getRelativeCameraViewMatrix();
        const math::mat4& projection   = getProjectMatrix();

        const math::mat4 viewProjection = projection * relativeView;

		outUB.translatedWorldToClip_NoJitter = viewProjection;

        outUB.translatedWorldToView = relativeView;
        outUB.viewToClip = projection;
        outUB.translatedWorldToClip = viewProjection;

        outUB.viewToTranslatedWorld = math::inverse(outUB.translatedWorldToView);
        outUB.clipToView = math::inverse(outUB.viewToClip);
        outUB.clipToTranslatedWorld = math::inverse(outUB.translatedWorldToClip);

		const Frustum frustum = computeRelativeWorldFrustum();
		outUB.frustumPlane[0] = frustum.planes[0];
		outUB.frustumPlane[1] = frustum.planes[1];
		outUB.frustumPlane[2] = frustum.planes[2];
		outUB.frustumPlane[3] = frustum.planes[3];
		outUB.frustumPlane[4] = frustum.planes[4];
		outUB.frustumPlane[5] = frustum.planes[5];

		double3 cameraPositionKm;
		double3 cameraToEarthCenterKm = getCameraToEarthCenterKm(m_position, cameraPositionKm);

		outUB.cameraPositionWS_km = fillDouble3(cameraPositionKm);
		outUB.cameraToEarthCenter_km = fillDouble3(cameraToEarthCenterKm);
		
		//
		outUB.cameraFovy = m_fovy;
		outUB.zNear = float(m_zNear);
		outUB.zFar  = float(m_zFar);

		//
		const float4x4 relativeViewProjectionWithZFar = projectionWithZFar * relativeView;
		outUB.clipToTranslatedWorldWithZFar = math::inverse(relativeViewProjectionWithZFar);

		outUB.cameraWorldPos = fillDouble3(m_position);
    }

	Frustum ICamera::computeRelativeWorldFrustum() const
	{
		// We always set camera position is zero to make relative rendering.
		constexpr math::vec3 camWorldPos = math::vec3(0.0f);

		//  
        const math::vec3 forwardVector = math::normalize(m_front);
		const math::vec3 upVector      = math::normalize(m_up);
		const math::vec3 rightVector   = math::normalize(m_right);

        const float tanFovyHalf = math::tan(getFovY() * 0.5f);
        const float aspect = getAspect();

		// zFar still working here.
        const math::vec3 nearC = camWorldPos + forwardVector * float(m_zNear);
        const math::vec3 farC  = camWorldPos + forwardVector * float(m_zFar);

		const float yNearHalf = m_zNear * tanFovyHalf;
		const float yFarHalf  = m_zFar * tanFovyHalf;

		const math::vec3 yNearHalfV = yNearHalf * upVector;
		const math::vec3 xNearHalfV = yNearHalf * aspect * rightVector;

		const math::vec3 yFarHalfV = yFarHalf * upVector;
		const math::vec3 xFarHalfV = yFarHalf * aspect * rightVector;

		// Near plane corner.
		const math::vec3 nrt = nearC + xNearHalfV + yNearHalfV;
		const math::vec3 nrd = nearC + xNearHalfV - yNearHalfV;
		const math::vec3 nlt = nearC - xNearHalfV + yNearHalfV;
		const math::vec3 nld = nearC - xNearHalfV - yNearHalfV;

		// Far plane corner.
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