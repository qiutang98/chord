#pragma once

#include <utils/utils.h>
#include <utils/noncopyable.h>
#include <application/application.h>
#include <utils/delegate.h>
#include <iostream>
#include <utils/cvar.h>
#include <utils/log.h>
#include <graphics/graphics.h>
#include <shader/shader.h>
#include <ui/widget.h>
#include <graphics/graphics.h>
#include <graphics/helper.h>
#include <utils/lru.h>
#include <graphics/resource.h>

#include <scene/scene.h>
#include <scene/scene_manager.h>
#include <scene/scene_node.h>
#include <scene/component/transform.h>
#include <project.h>

constexpr chord::uint32 kMultiWidgetMaxNum = 4;