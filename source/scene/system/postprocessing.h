#pragma once

#include <utils/engine.h>
#include <graphics/graphics.h>
#include <renderer/compute_pass.h>
#include <renderer/graphics_pass.h>
#include <scene/scene_common.h>
#include <renderer/render_helper.h>

namespace chord
{
    class PostProcessingManager : NonCopyable, public ISceneSystem
    {
    public:
        ARCHIVE_DECLARE;

        explicit PostProcessingManager();
        virtual ~PostProcessingManager() = default;

        // 
        const PostprocessConfig& getConfig() const { return m_config; }

    protected:
        virtual void onDrawUI(SceneRef scene) override;


    private:
        PostprocessConfig m_config;
    };
}