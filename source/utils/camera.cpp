#include <utils/camera.h>

namespace chord
{
    void ICamera::fillViewUniformParameter(PerframeCameraView& view) const
    {
        const math::dmat4x4 cameraViewMatrix = getViewMatrix();

        // TODO: translate world.
        view.translatedWorldToView = math::mat4(cameraViewMatrix);
        view.viewToTranslatedWorld = math::inverse(view.translatedWorldToView);

        view.viewToClip = getProjectMatrix();
        view.clipToView = math::inverse(view.viewToClip);

        view.translatedWorldToClip = view.viewToClip * view.translatedWorldToView;
        view.clipToTranslatedWorld = math::inverse(view.translatedWorldToClip);
    }
}