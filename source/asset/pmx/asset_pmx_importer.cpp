#include "asset_pmx_importer.h"

namespace chord::pmx
{
    bool importPMX(PMXRawData& outModel, const std::filesystem::path& pmxFilePath)
    {
        std::ifstream inStreaming(pmxFilePath, std::ifstream::binary);
        if (!inStreaming)
        {
            LOG_ERROR("Fail to open file {}.", utf8::utf16to8(pmxFilePath.u16string()));
            return false;
        }

        std::filesystem::path pmxFolderPath = pmxFilePath.parent_path();
        pmxFolderPath = std::filesystem::absolute(pmxFolderPath);

        inStreaming.seekg(0, inStreaming.end);
        int32 length = inStreaming.tellg();
        inStreaming.seekg(0, inStreaming.beg);

        std::vector<char> buffer(length);
        inStreaming.read(buffer.data(), length);
        inStreaming.close();

        int32 ptrPos = 0;
        char* ptr = buffer.data();

        auto stepPtr = [&](int32 size) { ptr += size; ptrPos += size; };
        auto eof = [&]() { return ptrPos >= length; };

        auto stepCopy = [&](auto& dest, int32 count = 1)
        {
            memcpy(&dest, ptr, sizeof(dest) * count);
            stepPtr(sizeof(dest) * count);
        };

        auto stepCopy2 = [&](void* dest, int32 size)
        {
            memcpy(dest, ptr, size);
            stepPtr(size);
        };
        
        auto& header = outModel.header;

        stepCopy(header.signature[0], 4);
        stepCopy(header.version);

        stepCopy(header.globalCount);
        check(header.globalCount == 8);

        stepCopy(header.globals);

        // 
        check(header.globals.additionalVec4Count <= 4);

        auto stepString = [&](std::string& name)
        {
            int32 size;
            stepCopy(size);

            if (size > 0)
            {
                const bool bUTF8Encode = (header.globals.textEncoding == 1);
                if (bUTF8Encode)
                {
                    std::string readingString(size, '\0');
                    stepCopy2(readingString.data(), size);
                    name = std::move(readingString);
                }
                else
                {
                    std::u16string utf16Str(size / 2, u'\0');
                    check(size % 2 == 0);
                    stepCopy2(utf16Str.data(), size);
                    name = utf8::utf16to8(utf16Str);
                }
            }
        };

        stepString(header.modelNameLocal);
        stepString(header.modelNameUniversal);
        stepString(header.comments);
        stepString(header.commentsUniversal);

        auto stepVertexIndex = [&](int32& outIndex)
        {
            if (header.globals.vertexIndexSize == 1) { uint8 id; stepCopy(id); outIndex = int32(id); return; }
            if (header.globals.vertexIndexSize == 2) { uint16 id; stepCopy(id); outIndex = int32(id); return; }
            if (header.globals.vertexIndexSize == 4) { stepCopy(outIndex); return; }
            checkEntry();
        };

        auto stepBoneIndex = [&](int32& outIndex)
        {
            // The bone index of -1 is a nil value, the bone should be ignored.
            if (header.globals.boneIndexSize == 1) {  int8 id; stepCopy(id); outIndex = int32(id); return; }
            if (header.globals.boneIndexSize == 2) { int16 id; stepCopy(id); outIndex = int32(id); return; }
            if (header.globals.boneIndexSize == 4) { stepCopy(outIndex); return; }
            checkEntry();
        };

        // -1 meaning no texture.
        auto stepTextureIndex = [&](int32& outIndex)
        {
            if (header.globals.textureIndexSize == 1) { int8 id; stepCopy(id); outIndex = int32(id); return; }
            if (header.globals.textureIndexSize == 2) { int16 id; stepCopy(id); outIndex = int32(id); return; }
            if (header.globals.textureIndexSize == 4) { stepCopy(outIndex); return; }
            checkEntry();
        };

        // -1 meaning no material.
        auto stepMaterialIndex = [&](int32& outIndex)
        {
            if (header.globals.materialIndexSize == 1) { int8 id; stepCopy(id); outIndex = int32(id); return; }
            if (header.globals.materialIndexSize == 2) { int16 id; stepCopy(id); outIndex = int32(id); return; }
            if (header.globals.materialIndexSize == 4) { stepCopy(outIndex); return; }
            checkEntry();
        };

        auto stepMorphIndex = [&](int32& outIndex)
        {
            if (header.globals.morphIndexSize == 1) { int8 id; stepCopy(id); outIndex = int32(id); return; }
            if (header.globals.morphIndexSize == 2) { int16 id; stepCopy(id); outIndex = int32(id); return; }
            if (header.globals.morphIndexSize == 4) { stepCopy(outIndex); return; }
            checkEntry();
        };

        auto stepRigdbodyIndex = [&](int32& outIndex)
        {
            if (header.globals.rigidbodyIndexSize == 1) { int8 id; stepCopy(id); outIndex = int32(id); return; }
            if (header.globals.rigidbodyIndexSize == 2) { int16 id; stepCopy(id); outIndex = int32(id); return; }
            if (header.globals.rigidbodyIndexSize == 4) { stepCopy(outIndex); return; }
            checkEntry();
        };

        int32 verticesCount;
        stepCopy(verticesCount);
        outModel.vertices.resize(verticesCount);
        auto& vertices = outModel.vertices;
        auto& globals = header.globals;
        for (auto& vertex : vertices)
        {
            stepCopy(vertex.position);
            stepCopy(vertex.normal);
            stepCopy(vertex.uv);

            for (int32 i = 0; i < globals.additionalVec4Count; i ++)
            {
                stepCopy(vertex.additionalUV[i]);
            }

            stepCopy(vertex.weightDeformType);
            switch (vertex.weightDeformType)
            {
            case VertexData::EDeformType::BDEF1:
            {
                stepBoneIndex(vertex.weightDeform.BDEF1.boneIndex);
                break;
            }
            case VertexData::EDeformType::BDEF2:
            {
                stepBoneIndex(vertex.weightDeform.BDEF2.boneIndices[0]);
                stepBoneIndex(vertex.weightDeform.BDEF2.boneIndices[1]);
                stepCopy(vertex.weightDeform.BDEF2.bone0Weight);
                break;
            }
            case VertexData::EDeformType::BDEF4:
            {
                stepBoneIndex(vertex.weightDeform.BDEF4.boneIndices[0]);
                stepBoneIndex(vertex.weightDeform.BDEF4.boneIndices[1]);
                stepBoneIndex(vertex.weightDeform.BDEF4.boneIndices[2]);
                stepBoneIndex(vertex.weightDeform.BDEF4.boneIndices[3]);
                stepCopy(vertex.weightDeform.BDEF4.boneWeights[0], 4);
                break;
            }
            case VertexData::EDeformType::SDEF:
            {
                stepBoneIndex(vertex.weightDeform.SDEF.boneIndices[0]);
                stepBoneIndex(vertex.weightDeform.SDEF.boneIndices[1]);
                stepCopy(vertex.weightDeform.SDEF.bone0Weight);

                stepCopy(vertex.weightDeform.SDEF.C[0],  3);
                stepCopy(vertex.weightDeform.SDEF.R0[0], 3);
                stepCopy(vertex.weightDeform.SDEF.R1[0], 3);
                break;
            }
            case VertexData::EDeformType::QDEF:
            {
                stepBoneIndex(vertex.weightDeform.QDEF.boneIndices[0]);
                stepBoneIndex(vertex.weightDeform.QDEF.boneIndices[1]);
                stepBoneIndex(vertex.weightDeform.QDEF.boneIndices[2]);
                stepBoneIndex(vertex.weightDeform.QDEF.boneIndices[3]);
                stepCopy(vertex.weightDeform.QDEF.boneWeights[0], 4);
                break;
            }
            default: checkEntry();
            }

            stepCopy(vertex.edgeScale);
        }

        int32 surfaceVerticesCount;
        stepCopy(surfaceVerticesCount);
        outModel.surfaces.resize(surfaceVerticesCount / 3);

        auto stepSurfaces = [&]<typename T>()
        {
            if constexpr (std::is_same_v<T, int32>)
            {
                for (auto& surface : outModel.surfaces)
                {
                    stepCopy(surface);
                }
            }
            else
            {
                T ids[3];
                for (auto& surface : outModel.surfaces)
                {
                    stepCopy(ids[0], 3);
                    surface[0] = int32(ids[0]);
                    surface[1] = int32(ids[1]);
                    surface[2] = int32(ids[2]);
                }
            }
        };
        if (header.globals.vertexIndexSize == 1)
        {
            stepSurfaces.operator()<uint8>();
        }
        else if (header.globals.vertexIndexSize == 2)
        {
            stepSurfaces.operator()<uint16>();
        }
        else if (header.globals.vertexIndexSize == 4)
        {
            stepSurfaces.operator()<int32>();
        }
        else
        {
            checkEntry();
        }

        int32 textureCount;
        stepCopy(textureCount);
        if (textureCount > 0)
        {
            outModel.textures.resize(textureCount);
            std::string u8str;

            for (auto& texture : outModel.textures)
            {
                stepString(u8str);
                texture = utf8::utf8to16(u8str);

                if (!texture.is_absolute())
                {
                    texture = pmxFolderPath / texture;
                }
                check(std::filesystem::exists(texture));
            }
        }

        int32 materialCount;
        stepCopy(materialCount);
        if (materialCount > 0)
        {
            outModel.materials.resize(materialCount);
            std::string u8str;

            for (auto& material : outModel.materials)
            {
                stepString(u8str);
                material.materialNameLocal = u8str;

                stepString(u8str);
                material.materialNameUniversal = u8str;

                stepCopy(material.diffuseColor);
                stepCopy(material.specularColor);
                stepCopy(material.specularStrength);
                stepCopy(material.ambientColor);

                stepCopy(material.flags);
                stepCopy(material.edgeColor);
                stepCopy(material.edgeScale);

                stepTextureIndex(material.textureIdex);
                stepTextureIndex(material.environmentIndex);

                stepCopy(material.environmentBlendMode);

                stepCopy(material.toonReference);
                if (material.toonReference == Material::EToonReference::Internal)
                {
                    int8 index;
                    stepCopy(index);
                    material.toonValue = index;
                }
                else
                {
                    stepTextureIndex(material.toonValue);
                }

                stepString(material.metaData);
                stepCopy(material.surfaceCount);
            }
        }

        int32 boneCount;
        stepCopy(boneCount);
        if (boneCount > 0)
        {
            outModel.bones.resize(boneCount);

            std::string u8str;

            for (auto& bone : outModel.bones)
            {
                stepString(u8str);
                bone.boneNameLocal = u8str;

                stepString(u8str);
                bone.boneNameUniversal = u8str;

                stepCopy(bone.position);
                stepBoneIndex(bone.parentBoneIndex);
                stepCopy(bone.layer);
                stepCopy(bone.flags);

                if (uint16(bone.flags) & uint16(Bone::EFlags::IndexedTailPosition))
                {
                    stepBoneIndex(bone.tail.boneIndex);
                }
                else
                {
                    stepCopy(bone.tail.position);
                }

                if ((uint16(bone.flags) & uint16(Bone::EFlags::InheritRotation)) || 
                    (uint16(bone.flags) & uint16(Bone::EFlags::InheritTranslation)))
                {
                    stepBoneIndex(bone.inheritBone.parentIndex);
                    stepCopy(bone.inheritBone.parentInfluence);
                }


                if (uint16(bone.flags) & uint16(Bone::EFlags::FixedAxis))
                {
                    stepCopy(bone.fixedAxis);
                }

                if (uint16(bone.flags) & uint16(Bone::EFlags::LocalCoordinate))
                {
                    stepCopy(bone.localCoordinate.x);
                    stepCopy(bone.localCoordinate.z);
                }

                if (uint16(bone.flags) & uint16(Bone::EFlags::ExternalParentDeform))
                {
                    stepBoneIndex(bone.externalParent);
                }

                if (uint16(bone.flags) & uint16(Bone::EFlags::IK))
                {
                    stepBoneIndex(bone.ik.targetIndex);
                    stepCopy(bone.ik.loopCount);
                    stepCopy(bone.ik.limitRadian);

                    int32 linkCount;
                    stepCopy(linkCount);
                    bone.ik.ikLinks.resize(linkCount);

                    for (auto& link : bone.ik.ikLinks)
                    {
                        stepBoneIndex(link.boneIndex);
                        stepCopy(link.hasLimit);
                        if (link.hasLimit)
                        {
                            stepCopy(link.ikAngleLimit.min);
                            stepCopy(link.ikAngleLimit.max);
                        }
                    }
                }
            }
        }

        int32 morphCount;
        stepCopy(morphCount);
        if (morphCount > 0)
        {
            outModel.morphes.resize(morphCount);
            std::string u8str;
            for (auto& morph : outModel.morphes)
            {
                stepString(u8str);
                morph.nameLocal = u8str;

                stepString(u8str);
                morph.nameGlobal = u8str;

                stepCopy(morph.pannelType);
                stepCopy(morph.morphType);

                int32 offsetCount;
                stepCopy(offsetCount);
                switch (morph.morphType)
                {
                case Morph::EMorphType::Vertex:
                {
                    morph.vertex.resize(offsetCount);
                    for (auto& vertex : morph.vertex)
                    {
                        stepVertexIndex(vertex.vertexIndex);
                        stepCopy(vertex.translation);
                    }
                }
                break;
                case Morph::EMorphType::UV:
                case Morph::EMorphType::UVext0:
                case Morph::EMorphType::UVext1:
                case Morph::EMorphType::UVext2:
                case Morph::EMorphType::UVext3:
                {
                    morph.uv.resize(offsetCount);
                    for (auto& uv : morph.uv)
                    {
                        stepVertexIndex(uv.vertexIndex);
                        stepCopy(uv.floats);
                    }
                }
                break;
                case Morph::EMorphType::Material:
                {
                    morph.material.resize(offsetCount);
                    for (auto& material : morph.material)
                    {
                        stepMaterialIndex(material.materialIndex);
                        stepCopy(material.opType);
                        stepCopy(material.diffuse);
                        stepCopy(material.specular);
                        stepCopy(material.specularity);
                        stepCopy(material.ambient);
                        stepCopy(material.edgeColor);
                        stepCopy(material.egdeSize);
                        stepCopy(material.textureTint);
                        stepCopy(material.environmentTint);
                        stepCopy(material.toonTint);
                    }
                }
                break;
                case Morph::EMorphType::Group:
                {
                    morph.group.resize(offsetCount);
                    for (auto& group : morph.group)
                    {
                        stepMorphIndex(group.morphIndex);
                        stepCopy(group.influence);
                    }
                }
                break;
                case Morph::EMorphType::Flip:
                {
                    morph.flip.resize(offsetCount);
                    for (auto& flip : morph.flip)
                    {
                        stepMorphIndex(flip.morphIndex);
                        stepCopy(flip.influence);
                    }
                }
                break;
                case Morph::EMorphType::Impulse:
                {
                    morph.impulse.resize(offsetCount);
                    for (auto& impulse : morph.impulse)
                    {
                        stepRigdbodyIndex(impulse.rigidBodyIndex);
                        stepCopy(impulse.localFlag);
                        stepCopy(impulse.movementSpeed);
                        stepCopy(impulse.rotationTorque);
                    }
                }
                break;
                case Morph::EMorphType::Bone:
                {
                    morph.bone.resize(offsetCount);
                    for (auto& bone : morph.bone)
                    {
                        stepBoneIndex(bone.boneIndex);
                        stepCopy(bone.translation);
                        stepCopy(bone.rotation);
                    }
                }
                break;
                default: checkEntry();
                }
            }
        }

        int32 displayCount;
        stepCopy(displayCount);
        if (displayCount > 0)
        {
            outModel.displayframe.resize(displayCount);
            std::string u8str;

            for (auto& displayFrame : outModel.displayframe)
            {
                stepString(u8str);
                displayFrame.nameLocal = u8str;

                stepString(u8str);
                displayFrame.nameGlobal = u8str;

                stepCopy(displayFrame.specialFlag);

                int32 frameDataCount;
                stepCopy(frameDataCount);
                displayFrame.frameData.resize(frameDataCount);
                for (auto& frameData : displayFrame.frameData)
                {
                    stepCopy(frameData.frameType);
                    if (frameData.frameType == 0)
                    {
                        stepBoneIndex(frameData.index);
                    }
                    else
                    {
                        check(frameData.frameType == 1);
                        stepMorphIndex(frameData.index);
                    }
                }
            }
        }

        int32 rigidbodyCount;
        stepCopy(rigidbodyCount);
        if (rigidbodyCount > 0)
        {
            outModel.rigidbody.resize(rigidbodyCount);
            std::string u8str;
            for (auto& rigidbody : outModel.rigidbody)
            {
                stepString(u8str);
                rigidbody.nameLocal = u8str;

                stepString(u8str);
                rigidbody.nameUniversal = u8str;

                stepBoneIndex(rigidbody.relatedBoneIndex);
                stepCopy(rigidbody.groupId);
                stepCopy(rigidbody.nonCollisionGroup);
                stepCopy(rigidbody.shapeType);
                stepCopy(rigidbody.shapeSize);
                stepCopy(rigidbody.shapePosition);
                stepCopy(rigidbody.shapeRotation);
                stepCopy(rigidbody.mass);
                stepCopy(rigidbody.moveAttenuation);
                stepCopy(rigidbody.rotationDamping);
                stepCopy(rigidbody.repulsion);
                stepCopy(rigidbody.frictionforce);
                stepCopy(rigidbody.physicsMode);
            }
        }

        int32 jointCount;
        stepCopy(jointCount);
        if (jointCount > 0)
        {
            outModel.joints.resize(jointCount);
            std::string u8str;

            for (auto& joint : outModel.joints)
            {
                stepString(u8str);
                joint.nameLocal = u8str;

                stepString(u8str);
                joint.nameUniversal = u8str;

                stepCopy(joint.type);

                stepRigdbodyIndex(joint.rigidBodyIndexA);
                stepRigdbodyIndex(joint.rigidBodyIndexB);

                stepCopy(joint.position);
                stepCopy(joint.rotation);
                stepCopy(joint.positionMin);
                stepCopy(joint.positionMax);
                stepCopy(joint.rotationMin);
                stepCopy(joint.rotationMax);
                stepCopy(joint.positionSpring);
                stepCopy(joint.rotationSpring);
            }
        }

        if (eof())
        {
            return true;
        }

        int32 softbodiesCount;
        stepCopy(softbodiesCount);
        if (softbodiesCount > 0)
        {
            outModel.softbodies.resize(softbodiesCount);
            std::string u8str;

            for (auto& softbody : outModel.softbodies)
            {
                stepString(u8str);
                softbody.nameLocal = u8str;

                stepString(u8str);
                softbody.nameUniversal = u8str;

                stepCopy(softbody.type);

                stepMaterialIndex(softbody.materialIndex);
                stepCopy(softbody.group);

                stepCopy(softbody.noCollisionMask);
                stepCopy(softbody.flag);
                stepCopy(softbody.bLinkLength);
                stepCopy(softbody.numClusters);
                stepCopy(softbody.totalMass);
                stepCopy(softbody.collisionMargin);
                stepCopy(softbody.aeroModel);
                stepCopy(softbody.c_VCF);
                stepCopy(softbody.c_DP);
                stepCopy(softbody.c_DG);
                stepCopy(softbody.c_LF);
                stepCopy(softbody.c_PR);
                stepCopy(softbody.c_VC);
                stepCopy(softbody.c_DF);
                stepCopy(softbody.c_MT);
                stepCopy(softbody.c_CHR);
                stepCopy(softbody.c_KHR);
                stepCopy(softbody.c_SHR);
                stepCopy(softbody.c_AHR);

                stepCopy(softbody.cluster_SRHR_CL);
                stepCopy(softbody.cluster_SKHR_CL);
                stepCopy(softbody.cluster_SSHR_CL);
                stepCopy(softbody.cluster_SR_SPLT_CL);
                stepCopy(softbody.cluster_SK_SPLT_CL);
                stepCopy(softbody.cluster_SS_SPLT_CL);

                stepCopy(softbody.interation_V_IT);
                stepCopy(softbody.interation_P_IT);
                stepCopy(softbody.interation_D_IT);
                stepCopy(softbody.interation_C_IT);

                stepCopy(softbody.material_LST);
                stepCopy(softbody.material_AST);
                stepCopy(softbody.material_VST);

                int32 count;
                stepCopy(count);
                softbody.anchorRigidbodies.resize(count);
                for (auto& anchor : softbody.anchorRigidbodies)
                {
                    stepRigdbodyIndex(anchor.rigidBodyIndex);
                    stepVertexIndex(anchor.vertexIndex);
                    stepCopy(anchor.nearMode);
                }

                stepCopy(count);
                softbody.pinVertexIndices.resize(count);
                for (auto& index : softbody.pinVertexIndices)
                {
                    stepVertexIndex(index);
                }
            }
        }

        return true;
    }
}