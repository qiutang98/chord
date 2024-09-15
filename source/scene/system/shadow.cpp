#include <scene/scene.h>
#include <scene/system/shadow.h>
#include <fontawsome/IconsFontAwesome6.h>
#include <ui/ui_helper.h>

using namespace chord;
using namespace chord::graphics;




ShadowManager::ShadowManager()
	: ISceneSystem("Shadow", ICON_FA_SUN + std::string("  Shadow"))
{

}

void ShadowManager::onDrawUI(SceneRef scene)
{

}