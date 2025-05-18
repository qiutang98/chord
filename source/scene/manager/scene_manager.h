#pragma once

#include <scene/scene_common.h>
#include <utils/string_table.h>

namespace chord
{
    class ISceneManager
    {
    public:
        // NOTE: name is rodata. decorated name can be dynamic data.
        explicit ISceneManager(const char* name, const std::string& decoratedName);
        void drawUI(SceneRef scene);

    protected:
        virtual void onDrawUI(SceneRef scene) = 0;

    private:
        FName m_name;
        std::string m_decoratedName;
    };
}