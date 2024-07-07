#include <utils/camera.h>

namespace chord
{
    void ICamera::fillViewUniformParameter(PerframeCameraView& outUB) const
    {
        const math::mat4 view = math::mat4(getViewMatrix());
        const math::mat4 projection = getProjectMatrix();
        const math::mat4 viewProjection = projection * view;

        outUB.translatedWorldToView = view;
        outUB.viewToTranslatedWorld = math::inverse(view);

        outUB.viewToClip = projection;
        outUB.clipToView = math::inverse(projection);

        outUB.translatedWorldToClip = viewProjection;
        outUB.clipToTranslatedWorld = math::inverse(viewProjection);
    }
}