#include <Tests/ShaderUtils.hpp>
#include <NZSL/GlslWriter.hpp>
#include <NZSL/LangWriter.hpp>
#include <NZSL/ShaderLangParser.hpp>
#include <NZSL/SpirvPrinter.hpp>
#include <NZSL/SpirvWriter.hpp>
#include <NZSL/Ast/AstReflect.hpp>
#include <NZSL/Ast/SanitizeVisitor.hpp>
#include <catch2/catch.hpp>
#include <glslang/Public/ShaderLang.h>
#include <spirv-tools/libspirv.hpp>

namespace
{
	// Use OpenGL default minimal values (from https://www.khronos.org/registry/OpenGL-Refpages/es3.0/html/glGet.xhtml, https://www.khronos.org/registry/OpenGL-Refpages/gl4/html/glGet.xhtml and https://www.khronos.org/registry/OpenGL-Refpages/gl2.1/)
	const TBuiltInResource s_minResources = {
		8,      //< maxLights
		6,      //< maxClipPlanes
		32,     //< maxTextureUnits
		32,     //< maxTextureCoords
		16,     //< maxVertexAttribs
		1024,   //< maxVertexUniformComponents
		60,     //< maxVaryingFloats
		16,     //< maxVertexTextureImageUnits
		32,     //< maxCombinedTextureImageUnits
		16,     //< maxTextureImageUnits
		896,    //< maxFragmentUniformComponents
		4,      //< maxDrawBuffers
		256,    //< maxVertexUniformVectors
		15,     //< maxVaryingVectors
		224,    //< maxFragmentUniformVectors
		256,    //< maxVertexOutputVectors
		224,    //< maxFragmentInputVectors
		-8,     //< minProgramTexelOffset
		7,      //< maxProgramTexelOffset
		8,      //< maxClipDistances
		0xFFFF, //< maxComputeWorkGroupCountX
		0xFFFF, //< maxComputeWorkGroupCountY
		0xFFFF, //< maxComputeWorkGroupCountZ
		1024,   //< maxComputeWorkGroupSizeX
		1024,   //< maxComputeWorkGroupSizeY
		64,     //< maxComputeWorkGroupSizeZ
		1024,   //< maxComputeUniformComponents
		16,     //< maxComputeTextureImageUnits
		8,      //< maxComputeImageUniforms
		8,      //< maxComputeAtomicCounters
		1,      //< maxComputeAtomicCounterBuffers
		60,     //< maxVaryingComponents
		64,     //< maxVertexOutputComponents
		64,     //< maxGeometryInputComponents
		128,    //< maxGeometryOutputComponents
		128,    //< maxFragmentInputComponents
		8,      //< maxImageUnits
		8,      //< maxCombinedImageUnitsAndFragmentOutputs
		8,      //< maxCombinedShaderOutputResources
		0,      //< maxImageSamples
		0,      //< maxVertexImageUniforms
		0,      //< maxTessControlImageUniforms
		0,      //< maxTessEvaluationImageUniforms
		0,      //< maxGeometryImageUniforms
		8,      //< maxFragmentImageUniforms
		8,      //< maxCombinedImageUniforms
		16,     //< maxGeometryTextureImageUnits
		256,    //< maxGeometryOutputVertices
		1024,   //< maxGeometryTotalOutputComponents
		1024,   //< maxGeometryUniformComponents
		64,     //< maxGeometryVaryingComponents
		128,    //< maxTessControlInputComponents
		128,    //< maxTessControlOutputComponents
		16,     //< maxTessControlTextureImageUnits
		1024,   //< maxTessControlUniformComponents
		4096,   //< maxTessControlTotalOutputComponents
		128,    //< maxTessEvaluationInputComponents
		128,    //< maxTessEvaluationOutputComponents
		16,     //< maxTessEvaluationTextureImageUnits
		1024,   //< maxTessEvaluationUniformComponents
		120,    //< maxTessPatchComponents
		32,     //< maxPatchVertices
		64,     //< maxTessGenLevel
		16,     //< maxViewports
		0,      //< maxVertexAtomicCounters
		0,      //< maxTessControlAtomicCounters
		0,      //< maxTessEvaluationAtomicCounters
		0,      //< maxGeometryAtomicCounters
		8,      //< maxFragmentAtomicCounters
		8,      //< maxCombinedAtomicCounters
		1,      //< maxAtomicCounterBindings
		0,      //< maxVertexAtomicCounterBuffers
		0,      //< maxTessControlAtomicCounterBuffers
		0,      //< maxTessEvaluationAtomicCounterBuffers
		0,      //< maxGeometryAtomicCounterBuffers
		1,      //< maxFragmentAtomicCounterBuffers
		1,      //< maxCombinedAtomicCounterBuffers
		16384,  //< maxAtomicCounterBufferSize
		4,      //< maxTransformFeedbackBuffers
		64,     //< maxTransformFeedbackInterleavedComponents
		8,      //< maxCullDistances
		8,      //< maxCombinedClipAndCullDistances
		4,      //< maxSamples
		256,    //< maxMeshOutputVerticesNV
		512,    //< maxMeshOutputPrimitivesNV
		32,     //< maxMeshWorkGroupSizeX_NV
		1,      //< maxMeshWorkGroupSizeY_NV
		1,      //< maxMeshWorkGroupSizeZ_NV
		32,     //< maxTaskWorkGroupSizeX_NV
		1,      //< maxTaskWorkGroupSizeY_NV
		1,      //< maxTaskWorkGroupSizeZ_NV
		4,      //< maxMeshViewCountNV
		1,      //< maxDualSourceDrawBuffersEXT
		{       //< limits
			true, //< nonInductiveForLoops
			true, //< whileLoops
			true, //< doWhileLoops
			true, //< generalUniformIndexing
			true, //< generalAttributeMatrixVectorIndexing
			true, //< generalVaryingIndexing
			true, //< generalSamplerIndexing
			true, //< generalVariableIndexing
			true, //< generalConstantMatrixVectorIndexing
		}
	};

	std::string_view Trim(std::string_view str)
	{
		while (!str.empty() && std::isspace(str.front()))
			str.remove_prefix(1);

		while (!str.empty() && std::isspace(str.back()))
			str.remove_suffix(1);

		return str;
	}
}

void ExpectGLSL(const nzsl::ShaderAst::Module& shaderModule, std::string_view expectedOutput)
{
	expectedOutput = Trim(expectedOutput);

	SECTION("Generating GLSL")
	{
		nzsl::ShaderAst::ModulePtr sanitizedModule;
		WHEN("Sanitizing a second time")
		{
			CHECK_NOTHROW(sanitizedModule = nzsl::ShaderAst::Sanitize(shaderModule));
		}
		const nzsl::ShaderAst::Module& targetModule = (sanitizedModule) ? *sanitizedModule : shaderModule;

		// Retrieve entry-point to get shader type
		std::optional<nzsl::ShaderStageType> entryShaderStage;

		nzsl::ShaderAst::AstReflect::Callbacks callbacks;
		callbacks.onEntryPointDeclaration = [&](nzsl::ShaderStageType stageType, const std::string& functionName)
		{
			INFO("multiple entry points found! (" << functionName << ")");
			REQUIRE((!entryShaderStage.has_value() || stageType == entryShaderStage));

			entryShaderStage = stageType;
		};

		nzsl::ShaderAst::AstReflect reflectVisitor;
		reflectVisitor.Reflect(*targetModule.rootNode, callbacks);

		{
			INFO("no entry point found");
			REQUIRE(entryShaderStage.has_value());
		}

		nzsl::GlslWriter writer;
		std::string output = writer.Generate(entryShaderStage, targetModule);

		WHEN("Validating expected code")
		{
			INFO("full GLSL output:\n" << output << "\nexcepted output:\n" << expectedOutput);
			REQUIRE(output.find(expectedOutput) != std::string::npos);
		}

		WHEN("Validating full GLSL code (using glslang)")
		{
			EShLanguage stage = EShLangVertex;
			switch (*entryShaderStage)
			{
				case nzsl::ShaderStageType::Fragment:
					stage = EShLangFragment;
					break;

				case nzsl::ShaderStageType::Vertex:
					stage = EShLangVertex;
					break;
			}

			glslang::TShader glslangShader(stage);
			glslangShader.setEnvInput(glslang::EShSourceGlsl, stage, glslang::EShClientOpenGL, 300);
			glslangShader.setEnvClient(glslang::EShClientOpenGL, glslang::EShTargetOpenGL_450);
			glslangShader.setEnvTarget(glslang::EShTargetNone, static_cast<glslang::EShTargetLanguageVersion>(0));
			glslangShader.setEntryPoint("main");

			const char* source = output.c_str();
			glslangShader.setStrings(&source, 1);

			if (!glslangShader.parse(&s_minResources, 100, false, static_cast<EShMessages>(EShMsgDefault | EShMsgKeepUncalled)))
			{
				INFO("full GLSL output:\n" << output << "\nerror:\n" << glslangShader.getInfoLog());
				REQUIRE(false);
			}
		}
	}
}

void ExpectNZSL(const nzsl::ShaderAst::Module& shaderModule, std::string_view expectedOutput)
{
	expectedOutput = Trim(expectedOutput);

	SECTION("Generating NZSL")
	{
		nzsl::ShaderAst::ModulePtr sanitizedModule;
		WHEN("Sanitizing a second time")
		{
			CHECK_NOTHROW(sanitizedModule = nzsl::ShaderAst::Sanitize(shaderModule));
		}
		const nzsl::ShaderAst::Module& targetModule = (sanitizedModule) ? *sanitizedModule : shaderModule;

		nzsl::LangWriter writer;
		std::string output = writer.Generate(targetModule);

		WHEN("Validating expected code")
		{
			INFO("full NZSL output:\n" << output << "\nexcepted output:\n" << expectedOutput);
			REQUIRE(output.find(expectedOutput) != std::string::npos);
		}

		WHEN("Validating full NZSL code (by recompiling it)")
		{
			// validate NZSL by recompiling it
			REQUIRE_NOTHROW(nzsl::ShaderLang::Parse(output));
		}
	}
}

void ExpectSPIRV(const nzsl::ShaderAst::Module& shaderModule, std::string_view expectedOutput, bool outputParameter)
{
	expectedOutput = Trim(expectedOutput);

	SECTION("Generating SPIRV")
	{
		nzsl::ShaderAst::ModulePtr sanitizedModule;
		WHEN("Sanitizing a second time")
		{
			CHECK_NOTHROW(sanitizedModule = nzsl::ShaderAst::Sanitize(shaderModule));
		}
		const nzsl::ShaderAst::Module& targetModule = (sanitizedModule) ? *sanitizedModule : shaderModule;

		nzsl::SpirvWriter writer;
		nzsl::SpirvPrinter printer;

		nzsl::SpirvPrinter::Settings settings;
		settings.printHeader = false;
		settings.printParameters = outputParameter;

		auto spirv = writer.Generate(targetModule);
		std::string output = printer.Print(spirv.data(), spirv.size(), settings);

		WHEN("Validating expected code")
		{
			INFO("full SPIRV output:\n" << output << "\nexcepted output:\n" << expectedOutput);
			REQUIRE(output.find(expectedOutput) != std::string::npos);
		}

		WHEN("Validating full SPIRV code (using libspirv)")
		{
			// validate SPIRV with libspirv
			spvtools::SpirvTools spirvTools(spv_target_env::SPV_ENV_VULKAN_1_0);
			spirvTools.SetMessageConsumer([&](spv_message_level_t /*level*/, const char* /*source*/, const spv_position_t& /*position*/, const char* message)
			{
				std::string fullSpirv;
				if (!spirvTools.Disassemble(spirv, &fullSpirv))
					fullSpirv = "<failed to disassemble SPIRV>";

				UNSCOPED_INFO(fullSpirv + "\n" + message);
			});

			REQUIRE(spirvTools.Validate(spirv));
		}
	}
}

nzsl::ShaderAst::ModulePtr SanitizeModule(const nzsl::ShaderAst::Module& module)
{
	nzsl::ShaderAst::SanitizeVisitor::Options defaultOptions;
	return SanitizeModule(module, defaultOptions);
}

nzsl::ShaderAst::ModulePtr SanitizeModule(const nzsl::ShaderAst::Module& module, const nzsl::ShaderAst::SanitizeVisitor::Options& options)
{
	nzsl::ShaderAst::ModulePtr shaderModule;
	WHEN("We sanitize the shader")
	{
		REQUIRE_NOTHROW(shaderModule = nzsl::ShaderAst::Sanitize(module, options));
	}

	WHEN("We output NZSL and try to parse it again")
	{
		nzsl::LangWriter langWriter;
		std::string outputCode = langWriter.Generate((shaderModule) ? *shaderModule : module);
		REQUIRE_NOTHROW(shaderModule = nzsl::ShaderAst::Sanitize(*nzsl::ShaderLang::Parse(outputCode), options));
	}

	// Ensure sanitization
	if (!shaderModule)
		REQUIRE_NOTHROW(shaderModule = nzsl::ShaderAst::Sanitize(module, options));

	return shaderModule;
}