#pragma once

#include <utils/utils.h>
#include <asset/asset_common.h>
#include <asset/asset.h>

namespace chord::pmx
{
	static_assert(sizeof(math::vec3) == sizeof(float) * 3);

	struct Globals
	{
		char textEncoding; // 0, 1	Byte encoding for the "text" type, 0 = UTF16LE, 1 = UTF8
		char additionalVec4Count; // 0..4	Additional vec4 values are added to each vertex
		char vertexIndexSize; // 1, 2, 4	The index type for vertices (See Index Types above)
		char textureIndexSize; //	1, 2, 4	The index type for textures(See Index Types above)
		char materialIndexSize; //	1, 2, 4	The index type for materials(See Index Types above)
		char boneIndexSize; //	1, 2, 4	The index type for bones(See Index Types above)
		char morphIndexSize; //	1, 2, 4	The index type for morphs(See Index Types above)
		char rigidbodyIndexSize; //	1, 2, 4	The index type for rigid bodies(See Index Types above)
	};

	struct Header
	{
		char signature[4]; // "PMX " [0x50, 0x4D, 0x58, 0x20]	Notice the space at the end
		float version;     // 2.0, 2.1	Compare as floating point values

		char globalCount;
		Globals globals;

		std::string modelNameLocal; //	text	Name of model	Usually in Japanese
		std::string modelNameUniversal; //	text	Name of model	Usually in English
		std::string comments;// local	text	Additional information(license)	Usually in Japanese
		std::string commentsUniversal;//	text	Additional information(license)	Usually in English
	};

	union WeightDeform
	{
		struct
		{
			int32 boneIndex; // weight = 1.0f
		} BDEF1;

		struct
		{
			int32 boneIndices[2];
			float bone0Weight; // Bone1Weight = 1.0 - Bone0Weight
		} BDEF2;

		struct
		{
			int32 boneIndices[4];
			float boneWeights[4]; // Sum is 1.0f.
		} BDEF4;

		struct
		{
			int32 boneIndices[2];
			float bone0Weight; // Bone1Weight = 1.0 - Bone0Weight

			math::vec3 C; 
			math::vec3 R0;
			math::vec3 R1;
		} SDEF; // Spherical deform blending

		struct
		{
			int32 boneIndices[4];
			float boneWeights[4]; // Sum is 1.0f.
		} QDEF; // Dual quaternion deform blending
	};

	struct VertexData
	{
		math::vec3 position;
		math::vec3 normal;
		math::vec2 uv; // -> x8.

		math::vec4 additionalUV[4];

		enum class EDeformType : uint8
		{
			BDEF1 = 0,
			BDEF2 = 1,
			BDEF4 = 2,
			SDEF  = 3,
			QDEF  = 4,
		};
		EDeformType weightDeformType; // 0 = BDEF1, 1 = BDEF2, 2 = BDEF4, 3 = SDEF, 4 = QDEF
		WeightDeform weightDeform;
		float edgeScale; // Pencil-outline scale (1.0 should be around 1 pixel)
	};

	// Triangle facing is defined in clockwise winding order 
	using SurfaceData = math::ivec3;



	struct Material
	{
		/*
			The point-drawing flag will override the line-drawing flag if both are set.

			A standard triangle is rendered with vertices [A->B->C].
			With point drawing each vertex is rendered as individual points [A, B, C] resulting in 3 points.
			Line drawing will render 3 lines [A->B, B->C, C->A] resulting in 3 lines.

			When point rendering is enabled the edge scale value from the material (Not the vertices) will control the size of the point.

			It is undefined if point or lines will have edges if the edge flag is set.
		*/
		enum class EFlags : uint8
		{
			NoCull = 0x01 << 0, // Disables back-face culling
			GroundShadow = 0x01 << 1, // Projects a shadow onto the geometry
			DrawShadow = 0x01 << 2, // Renders to the shadow map
			ReceiveShadow = 0x01 << 3, // Receives a shadow from the shadow map
			HasEdge = 0x01 << 4, // 	Has "pencil" outline
			VertexColor = 0x01 << 5, // Uses additional vec4 1 for vertex colour
			PointDrawing = 0x01 << 6, // Each of the 3 vertices are points
			LineDrawing = 0x01 << 7, // The triangle is rendered as lines
		};

		enum class EEnvironmentBlendMode : uint8
		{
			Disabled = 0,
			Multiply = 1,
			Additive = 2,

			/*
			* Environment blend mode 3 will use the first additional vec4 to map the environment texture,
			  using just the X and Y values as the texture UV. It is mapped as an additional texture layer.
			  This may conflict with other uses for the first additional vec4.
			*/
			AdditionalVec4 = 3 
		};

		enum class EToonReference : uint8
		{
			Texture = 0,
			Internal = 1, // toon01.bmp to toon10.bmp
		};

		std::string materialNameLocal;
		std::string materialNameUniversal;

		math::vec4 diffuseColor; // RGBA colour (Alpha will set a semi-transparent material)
		math::vec3 specularColor; // RGB colour of the reflected light
		float specularStrength; // The "size" of the specular highlight
		math::vec3 ambientColor; // RGB colour of the material shadow (When out of light)

		EFlags flags;
		math::vec4 edgeColor; // RGBA colour of the pencil-outline edge (Alpha for semi-transparent)
		float edgeScale; // Pencil-outline scale (1.0 should be around 1 pixel)
		
		int32 textureIdex; // See Index Types, this is from the texture table by default
		int32 environmentIndex; // Same as texture index, but for environment mapping
		EEnvironmentBlendMode environmentBlendMode;// 

		/*
		** Toon value will be a texture index much like the standard texture and environment texture indexes 
		   unless the Toon reference byte is equal to 1, 
		   in which case Toon value will be a byte that references a set of 10 internal toon textures 
		   (Most implementations will use "toon01.bmp" to "toon10.bmp" as the internal textures, see the reserved names for Textures above).
		*/
		EToonReference toonReference;
		int32 toonValue;

		std::string metaData;
		/*
			Surface count will always be a multiple of 3. 
			It is based on the offset of the previous material through to the size of the current material. 
			If you add up all the surface counts for all materials you should end up with the total number of surfaces.
		*/
		int32 surfaceCount; // 
	};

	struct Bone
	{
		enum  class EFlags : uint16
		{
			IndexedTailPosition = 0x01 << 0, // Is the tail position a vec3 or bone index	
			Rotatable = 0x01 << 1, // Enables rotation
			Translatable = 0x01 << 2, // Enables translation (shear)
			IsVisible = 0x01 << 3, // 
			Enabled = 0x01 << 4,
			IK = 0x01 << 5, // Use inverse kinematics (physics)
			InheritedLocal = 0x01 << 7, //
			InheritRotation = 0x01 << 8, // 	Rotation inherits from another bone
			InheritTranslation = 0x01 << 9, // Translation inherits from another bone
			FixedAxis = 0x01 << 10, // The bone's shaft is fixed in a direction	
			LocalCoordinate = 0x01 << 11,
			PhysicsAfterDeform = 0x01 << 12,
			ExternalParentDeform = 0x01 << 13,
		};

		struct InheritBone
		{
			int32 parentIndex; // 
			float parentInfluence; // Weight of how much influence this parent has on this bone
		};

		using BoneFixedAxis = math::vec3; // Direction this bone points

		struct BoneLocalCoordinate
		{
			math::vec3 x;
			math::vec3 z;
		};

		using BoneExternalParent = int32;

		struct IKAngleLimit
		{
			math::vec3 min; // Minimum angle (radians)
			math::vec3 max; // Maximum angle (radians)
		};

		struct IKLinks
		{
			int32 boneIndex;
			char hasLimit; // When equal to 1, use angle limits
			IKAngleLimit ikAngleLimit;
		};

		struct BoneIK
		{
			int32 targetIndex;
			int32 loopCount;
			float limitRadian;
			
			std::vector<IKLinks> ikLinks;
		};

		std::string boneNameLocal;
		std::string boneNameUniversal;
		math::vec3 position; // 	The local translation of the bone
		int32 parentBoneIndex;
		int32 layer; // Deformation hierarchy
		EFlags flags;

		/*
			If indexed tail position flag is set then this is a bone index
		*/
		union 
		{
			math::vec3 position;
			int32 boneIndex;
		} tail;

		InheritBone inheritBone; // Used if either of the inherit flags are set. See Inherit Bone
		BoneFixedAxis fixedAxis; // Used if fixed axis flag is set. See Bone Fixed Axis
		BoneLocalCoordinate localCoordinate; // Used if local co-ordinate flag is set. See Bone Local Co-ordinate
		BoneExternalParent externalParent;// Used if external parent deform flag is set. See Bone External Parent
		BoneIK ik; // Used if IK flag is set. See Bone IK
	};

	struct Morph
	{
		enum class EMorphType : uint8
		{
			Group = 0,
			Vertex = 1,
			Bone = 2,
			UV = 3,
			UVext0 = 4,
			UVext1 = 5,
			UVext2 = 6,
			UVext3 = 7,
			Material = 8,
			Flip = 9,
			Impulse = 10,
		};

		struct Group
		{
			int32 morphIndex;
			float influence;
		};

		struct Vertex
		{
			int32 vertexIndex;
			math::vec3 translation;
		};

		struct Bone
		{
			int32 boneIndex;
			math::vec3 translation;
			math::vec4 rotation;
		};

		struct UV
		{
			int32 vertexIndex;
			math::vec4 floats;
		};

		struct Material
		{
			int32 materialIndex; // -1 all material.

			enum class OpType : uint8
			{
				Mul,
				Add,
			};
			OpType opType;

			math::vec4 diffuse;
			math::vec3 specular;
			float specularity;
			math::vec3 ambient;
			math::vec4 edgeColor;
			float egdeSize;
			math::vec4 textureTint;
			math::vec4 environmentTint;
			math::vec4 toonTint;
		};

		struct Flip
		{
			int32 morphIndex;
			float influence;
		};

		struct Impulse
		{
			int32 rigidBodyIndex;
			uint8 localFlag; // 0 off, 1 on
			math::vec3 movementSpeed;
			math::vec3 rotationTorque;
		};

		std::string nameLocal;
		std::string nameGlobal;

		/*
			Value	Group	Panel in MMD
			  0	    Hidden	  None
			  1	    Eyebrows  Bottom left
			  2	    Eyes	  Top left
			  3	    Mouth	  Top right
			  4	    Other	  Bottom right
		*/
		enum class EPannelType : uint8
		{
			None = 0, // Hidden 
			BottomLeft = 1, // Eyebrows 
			TopLeft = 2, // Eyes 
			TopRight = 3, // Mouth 
			BottomRight = 4, // Other 
		};
		EPannelType pannelType;
		EMorphType morphType;

		std::vector<Group>	    group;
		std::vector<Vertex>		vertex;
		std::vector<Bone>		bone;
		std::vector<UV>			uv;
		std::vector<Material>	material;
		std::vector<Flip>		flip;
		std::vector<Impulse>	impulse;
	};

	struct DisplayFrame
	{
		std::string nameLocal;
		std::string nameGlobal;

		char specialFlag; // 0 = normal frame, 1 = special frame

		struct FrameData
		{
			char frameType; // 0 = bone, 1 = morph.
			int32 index; // bone index or morph index.
		};
		std::vector<FrameData> frameData;
	};

	struct RigidBody
	{
		enum class EShapeType : uint8
		{
			Sphere = 0,
			Box = 1,
			Capsule = 2,
		};

		enum class EPhysicsMode : uint8
		{
			FollowBNone = 0, // Rigid body sticks to bone
			Physics = 1, // 	Rigid body uses gravity
			PhysicsBone = 2, // Rigid body uses gravity pivoted to bone
		};

		std::string nameLocal;
		std::string nameUniversal;

		int32 relatedBoneIndex;
		char groupId;
		int16 nonCollisionGroup;
		EShapeType shapeType;
		math::vec3 shapeSize;
		math::vec3 shapePosition; 
		math::vec3 shapeRotation;
		float mass;
		float moveAttenuation;
		float rotationDamping;
		float repulsion;
		float frictionforce;
		EPhysicsMode physicsMode;
	};

	struct Joint
	{
		enum class EType : uint8
		{
			SpringDOF6 = 0,
			DOF6 = 1,
			P2P = 2,
			ConeTwist = 3,
			Slider = 4,
			Hinge = 5,
		};

		std::string nameLocal;
		std::string nameUniversal;
		EType type;
		int32 rigidBodyIndexA;
		int32 rigidBodyIndexB;
		math::vec3 position;
		math::vec3 rotation; // 	Rotation angle radian
		math::vec3 positionMin;
		math::vec3 positionMax;
		math::vec3 rotationMin;
		math::vec3 rotationMax;
		math::vec3 positionSpring;
		math::vec3 rotationSpring;
	};

	struct Softbody
	{
		std::string	nameLocal;
		std::string	nameUniversal;

		enum class ESoftbodyType : uint8
		{
			TriMesh,
			Rope,
		};
		ESoftbodyType type;

		int32	materialIndex;

		char group; // Group id
		int16 noCollisionMask; // Non collision mask

		enum class ESoftbodyFlags : uint8
		{
			BLink = 0x01,
			Cluster = 0x02,
			HybridLink = 0x04,
		};
		ESoftbodyFlags flag;

		int32	bLinkLength;
		int32	numClusters;

		float	totalMass;
		float	collisionMargin;

		enum class AeroModel : int32
		{
			kAeroModelV_TwoSided,
			kAeroModelV_OneSided,
			kAeroModelF_TwoSided,
			kAeroModelF_OneSided,
		};
		AeroModel aeroModel;

		//config
		float c_VCF; // Velocities correction factor (Baumgarte)
		float c_DP; // Damping coefficient
		float c_DG; // Drag coefficient
		float c_LF; // Lift coefficient
		float c_PR; // 	Pressure coefficient
		float c_VC; // 	Volume conversation coefficient
		float c_DF; // Dynamic friction coefficient
		float c_MT; // 	Pose matching coefficient
		float c_CHR; // Rigid contacts hardness
		float c_KHR; // Kinetic contacts hardness
		float c_SHR; // Soft contacts hardness
		float c_AHR; // Anchors hardness

		//cluster
		float cluster_SRHR_CL; // Soft vs rigid hardness
		float cluster_SKHR_CL; // Soft vs kinetic hardness
		float cluster_SSHR_CL; // Soft vs soft hardness
		float cluster_SR_SPLT_CL; // Soft vs rigid impulse split
		float cluster_SK_SPLT_CL; // Soft vs kinetic impulse split
		float cluster_SS_SPLT_CL; // Soft vs soft impulse split

		//interation
		int32 interation_V_IT; // Velocities solver iterations
		int32 interation_P_IT; // Positions solver iterations
		int32 interation_D_IT; // Drift solver iterations
		int32 interation_C_IT; // Cluster solver iterations

		//material
		float material_LST; // Linear stiffness coefficient
		float material_AST; // Area / Angular stiffness coefficient
		float material_VST; // Volume stiffness coefficient

		struct AnchorRigidbody
		{
			int32	rigidBodyIndex;
			int32	vertexIndex;
			uint8	nearMode; //0:FF 1:ON
		};
		std::vector<AnchorRigidbody> anchorRigidbodies;

		std::vector<int32> pinVertexIndices;
	};

	struct PMXRawData
	{
		Header header;

		std::vector<VertexData> vertices;
		std::vector<SurfaceData> surfaces;
		std::vector<std::filesystem::path> textures;

		std::vector<Material> materials;
		std::vector<Bone> bones;
		std::vector<Morph> morphes;
		std::vector<DisplayFrame> displayframe;
		std::vector<RigidBody> rigidbody;
		std::vector<Joint> joints;
		std::vector<Softbody> softbodies;
	};

	extern bool importPMX(PMXRawData& outModel, const std::filesystem::path& pmxFilePath);
}