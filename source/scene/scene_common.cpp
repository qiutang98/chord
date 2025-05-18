#include <scene/scene_common.h>
#include <ui/ui_helper.h>

namespace chord
{


    void PerframeCollected::drawLowSphere(const ICamera* camera, const double3& position, float scale, const float3& color)
    {
        BuiltinMeshDrawInstance sphere{};

        sphere.mesh   = graphics::getContext().getBuiltinResources().lowSphere.get();
        sphere.offset = float3(position - camera->getPosition());
        sphere.scale  = scale;
        sphere.color  = color;

        builtinMeshInstances.push_back(std::move(sphere));
    }
}