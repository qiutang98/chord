#include <utils/camera.h>

namespace chord
{
	Frustum Frustum::build(const math::mat4& viewprojMatrix)
	{
		Frustum res{};

		res.planes[eLeft].x = viewprojMatrix[0].w + viewprojMatrix[0].x;
		res.planes[eLeft].y = viewprojMatrix[1].w + viewprojMatrix[1].x;
		res.planes[eLeft].z = viewprojMatrix[2].w + viewprojMatrix[2].x;
		res.planes[eLeft].w = viewprojMatrix[3].w + viewprojMatrix[3].x;

		res.planes[eRight].x = viewprojMatrix[0].w - viewprojMatrix[0].x;
		res.planes[eRight].y = viewprojMatrix[1].w - viewprojMatrix[1].x;
		res.planes[eRight].z = viewprojMatrix[2].w - viewprojMatrix[2].x;
		res.planes[eRight].w = viewprojMatrix[3].w - viewprojMatrix[3].x;

		res.planes[eTop].x = viewprojMatrix[0].w - viewprojMatrix[0].y;
		res.planes[eTop].y = viewprojMatrix[1].w - viewprojMatrix[1].y;
		res.planes[eTop].z = viewprojMatrix[2].w - viewprojMatrix[2].y;
		res.planes[eTop].w = viewprojMatrix[3].w - viewprojMatrix[3].y;

		res.planes[eDown].x = viewprojMatrix[0].w + viewprojMatrix[0].y;
		res.planes[eDown].y = viewprojMatrix[1].w + viewprojMatrix[1].y;
		res.planes[eDown].z = viewprojMatrix[2].w + viewprojMatrix[2].y;
		res.planes[eDown].w = viewprojMatrix[3].w + viewprojMatrix[3].y;

		res.planes[eBack].x = viewprojMatrix[0].w + viewprojMatrix[0].z;
		res.planes[eBack].y = viewprojMatrix[1].w + viewprojMatrix[1].z;
		res.planes[eBack].z = viewprojMatrix[2].w + viewprojMatrix[2].z;
		res.planes[eBack].w = viewprojMatrix[3].w + viewprojMatrix[3].z;

		res.planes[eFront].x = viewprojMatrix[0].w - viewprojMatrix[0].z;
		res.planes[eFront].y = viewprojMatrix[1].w - viewprojMatrix[1].z;
		res.planes[eFront].z = viewprojMatrix[2].w - viewprojMatrix[2].z;
		res.planes[eFront].w = viewprojMatrix[3].w - viewprojMatrix[3].z;

		for (auto i = 0; i < res.planes.size(); i++)
		{
			float length = math::sqrt(res.planes[i].x * res.planes[i].x + res.planes[i].y * res.planes[i].y + res.planes[i].z * res.planes[i].z);
			res.planes[i] /= length;
		}

		return res;
	}

	Frustum ICamera::computeWorldFrustum() const
	{
		const math::vec3 forwardVector = math::normalize(m_front);
		const math::vec3 camWorldPos = m_position;

		const math::vec3 nearC = camWorldPos + forwardVector * m_zNear;
		const math::vec3 farC = camWorldPos + forwardVector * m_zFar;

		const float tanFovyHalf = math::tan(getFovY() * 0.5f);
		const float aspect = getAspect();

		const float yNearHalf = m_zNear * tanFovyHalf;
		const float yFarHalf = m_zFar * tanFovyHalf;

		const math::vec3 yNearHalfV = yNearHalf * m_up;
		const math::vec3 xNearHalfV = yNearHalf * aspect * m_right;

		const math::vec3 yFarHalfV = yFarHalf * m_up;
		const math::vec3 xFarHalfV = yFarHalf * aspect * m_right;

		const math::vec3 nrt = nearC + xNearHalfV + yNearHalfV;
		const math::vec3 nrd = nearC + xNearHalfV - yNearHalfV;
		const math::vec3 nlt = nearC - xNearHalfV + yNearHalfV;
		const math::vec3 nld = nearC - xNearHalfV - yNearHalfV;

		const math::vec3 frt = farC + xFarHalfV + yFarHalfV;
		const math::vec3 frd = farC + xFarHalfV - yFarHalfV;
		const math::vec3 flt = farC - xFarHalfV + yFarHalfV;
		const math::vec3 fld = farC - xFarHalfV - yFarHalfV;

		Frustum ret{ };

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