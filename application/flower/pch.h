#pragma once

#include <utils/utils.h>
#include <utils/noncopyable.h>
#include <application/application.h>
#include <utils/delegate.h>
#include <utils/job_system.h>
#include <iostream>
#include <utils/cvar.h>
#include <utils/log.h>
#include <graphics/graphics.h>
#include <shader_compiler/shader.h>
#include <ui/widget.h>
#include <graphics/graphics.h>
#include <graphics/helper.h>
#include <utils/lru.h>
#include <graphics/resource.h>
#include <utils/camera.h>
#include <scene/scene.h>
#include <scene/scene_subsystem.h>
#include <scene/scene_node.h>
#include <scene/component/component_gltf_mesh.h>
#include <scene/component/component_transform.h>
#include <project.h>
#include <asset/pmx/asset_pmx_importer.h>
#include <asset/gltf/asset_gltf.h>
#include <asset/gltf/asset_gltf_helper.h>
#include <asset/texture/asset_texture.h>
#include <renderer/renderer.h>
#include <utils/allocator.h>
#include <scene/component/component_gltf_material.h>
#include <scene/component/component_sky.h>

constexpr chord::uint32 kMultiWidgetMaxNum = 4;