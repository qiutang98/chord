#include <shader/shader.h>
#include <graphics/graphics.h>
#include <graphics/helper.h>
#include <utils/threadpool.h>
#include <shader/compiler.h>
#include <utils/thread.h>

#ifdef _WIN32
	#include <wrl.h>
	#include <dxc/dxcapi.h>
	#include <dxc/d3d12shader.h>
#else
	#error "Shader compiler current only support windows."
#endif 

namespace chord::graphics
{
	IPlatformShaderCompiler::IPlatformShaderCompiler()
	{

	}

	IPlatformShaderCompiler::~IPlatformShaderCompiler()
	{

	}

	void ShaderCompileEnvironment::buildArgs(ShaderCompileArguments& out) const
	{
		// Spv.
		out.push_back("-spirv");
		out.push_back("-fvk-allow-rwstructuredbuffer-arrays");

		// Included path.
		out.push_back("-I"); out.push_back("resource/shader");

		// Entry.
		out.push_back("-E"); out.push_back(m_metaInfo.entry);

		// Target profile.
		out.push_back("-T");
		switch (m_metaInfo.stage)
		{
		case EShaderStage::Vertex:  out.push_back("vs_6_8"); break;
		case EShaderStage::Pixel:   out.push_back("ps_6_8"); break;
		case EShaderStage::Compute: out.push_back("cs_6_8"); break;
		case EShaderStage::Mesh:    out.push_back("ms_6_8"); break;
		case EShaderStage::Amplify: out.push_back("as_6_8"); break;
		default: checkEntry(); break;
		}

		// Shader special define.
		for (const auto& pair : m_definitions.getMap())
		{
			out.push_back("-D");

			if (pair.second.empty())
			{
				out.push_back(pair.first);
			}
			else
			{
				out.push_back(std::format("{0}={1}", pair.first, pair.second));
			}
		}

		// Shader instruction define.
		for (const auto& instruction : m_instructions.getInstructions())
		{
			out.push_back(instruction);
		}
	}

#ifdef _WIN32
	class Win32DxcShaderCompiler final : public IPlatformShaderCompiler
	{
	public:
		Microsoft::WRL::ComPtr<IDxcCompiler3> dxcompiler;
		Microsoft::WRL::ComPtr<IDxcUtils> utils;
		Microsoft::WRL::ComPtr<IDxcIncludeHandler> includeHandler;

		explicit Win32DxcShaderCompiler()
			: IPlatformShaderCompiler()
		{
			// Create dxc utils.
			checkGraphics(!FAILED(::DxcCreateInstance(CLSID_DxcUtils, IID_PPV_ARGS(&utils))));

			// Create include handler.
			checkGraphics(!FAILED(utils->CreateDefaultIncludeHandler(&includeHandler)));

			// Create dxc compiler.
			checkGraphics(!FAILED(::DxcCreateInstance(CLSID_DxcCompiler, IID_PPV_ARGS(&dxcompiler))));
		}

		~Win32DxcShaderCompiler()
		{

		}

		virtual void compileShader(
			SizedBuffer shaderData,
			const std::vector<std::string>& args,
			ShaderCompileResult& result) override
		{
			// Default we think it will sucess.
			result.bSuccess = true;

			DxcBuffer sourceBuffer
			{
				.Ptr  = shaderData.ptr,
				.Size = shaderData.size,
				.Encoding = 0u,
			};

			Microsoft::WRL::ComPtr<IDxcResult> compiledShaderBuffer{};

			bool bStripDebug = false;
			bool bStripRelfection = false;

			std::vector<std::wstring> wstrArguments(args.size());
			for (auto i = 0; i < args.size(); i++)
			{
				wstrArguments[i] = std::wstring(args[i].begin(), args[i].end());

				// If strip debug, we have pdb info.
				if (args[i] == "-Qstrip_debug") { bStripDebug = true; }

				// If strip reflection, we have reflection info.
				if (args[i] == "-Qstrip_reflect") { bStripRelfection = true; }
			}

			std::vector<LPCWSTR> compilationArguments(args.size());
			for (auto i = 0; i < args.size(); i++)
			{
				compilationArguments[i] = wstrArguments[i].c_str();
			}

			HRESULT hr = dxcompiler->Compile(&sourceBuffer,
				compilationArguments.data(),
				static_cast<uint32>(compilationArguments.size()),
				includeHandler.Get(),
				IID_PPV_ARGS(&compiledShaderBuffer));
			if (FAILED(hr))
			{
				result.bSuccess = false;
			}

			// Get compilation errors (if any).
			Microsoft::WRL::ComPtr<IDxcBlobUtf8> errors{};
			hr = compiledShaderBuffer->GetOutput(DXC_OUT_ERRORS, IID_PPV_ARGS(errors.GetAddressOf()), nullptr);
			if (FAILED(hr))
			{
				result.bSuccess = false;
			}

			if (errors && errors->GetStringLength() > 0)
			{
				const LPCSTR errorMessage = errors->GetStringPointer();

				// Copy error msg if exist error.
				result.errorMsg = errorMessage;
				result.bSuccess = false;
			}



			// Shader compile success, copy output to result.
			if (result.bSuccess)
			{
				Microsoft::WRL::ComPtr<IDxcBlob> shaderObject;
				hr = compiledShaderBuffer->GetOutput(DXC_OUT_OBJECT, IID_PPV_ARGS(shaderObject.GetAddressOf()), nullptr);
				if (FAILED(hr))
				{
					result.bSuccess = false;
				}
				else
				{
					result.shader.resize(shaderObject->GetBufferSize());
					memcpy(result.shader.data(), shaderObject->GetBufferPointer(), result.shader.size());
				}
			}

			// PDB
			if (result.bSuccess && bStripDebug)
			{
				Microsoft::WRL::ComPtr<IDxcBlob> debugData;
				Microsoft::WRL::ComPtr<IDxcBlobUtf16> debugDataPath;
				hr = compiledShaderBuffer->GetOutput(DXC_OUT_PDB, IID_PPV_ARGS(debugData.GetAddressOf()), debugDataPath.GetAddressOf());
				if (FAILED(hr))
				{
					result.bSuccess = false;
				}
				else
				{
					result.pdb.resize(debugData->GetBufferSize());
					memcpy(result.pdb.data(), debugData->GetBufferPointer(), result.pdb.size());
				}
			}

			// Reflection
			if (result.bSuccess && bStripRelfection)
			{
				Microsoft::WRL::ComPtr<IDxcBlob> reflectionData;
				hr = compiledShaderBuffer->GetOutput(DXC_OUT_REFLECTION, IID_PPV_ARGS(reflectionData.GetAddressOf()), nullptr);
				if (FAILED(hr))
				{
					result.bSuccess = false;
				}
				else
				{
					result.reflection.resize(reflectionData->GetBufferSize());
					memcpy(result.reflection.data(), reflectionData->GetBufferPointer(), result.reflection.size());
				}
			}
		}
	};
	using PlatformShaderCompiler = Win32DxcShaderCompiler;
#else
	#error "Shader compiler current only implement windows."
#endif 

	ShaderCompilerManager::ShaderCompilerManager(uint32 freeCount, uint32 desiredMaxCompileThreadCount)
		: Super(L"ShaderCompiler", freeCount, desiredMaxCompileThreadCount, [this]() { this->worker(); })
	{

	}

	ShaderCompilerManager::~ShaderCompilerManager()
	{

	}

	void ShaderCompilerManager::worker()
	{
		std::unique_ptr<IPlatformShaderCompiler> compiler = std::make_unique<PlatformShaderCompiler>();
		loop([&](const TaskType& func) { func(*compiler); });
	}
}