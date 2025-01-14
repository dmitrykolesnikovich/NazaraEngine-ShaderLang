#include <Tests/ShaderUtils.hpp>
#include <NZSL/ShaderBuilder.hpp>
#include <NZSL/Parser.hpp>
#include <NZSL/Ast/ConstantPropagationVisitor.hpp>
#include <NZSL/Ast/SanitizeVisitor.hpp>
#include <catch2/catch.hpp>
#include <cctype>

void ExpectOutput(nzsl::Ast::Module& shaderModule, const nzsl::Ast::SanitizeVisitor::Options& options, std::string_view expectedOptimizedResult)
{
	nzsl::Ast::ModulePtr sanitizedShader;
	sanitizedShader = SanitizeModule(shaderModule, options);

	ExpectNZSL(*sanitizedShader, expectedOptimizedResult);
}

TEST_CASE("const", "[Shader]")
{
	WHEN("Using const for constants")
	{
		std::string_view sourceCode = R"(
[nzsl_version("1.0")]
module;

const LightCount = 3;
const LightCapacity = LightCount + 2;

[layout(std140)]
struct Light
{
	color: vec4[f32]
}

[layout(std140)]
struct LightData
{
	lights: array[Light, LightCapacity]
}
)";

		nzsl::Ast::ModulePtr shaderModule;
		REQUIRE_NOTHROW(shaderModule = nzsl::Parse(sourceCode));

		ExpectOutput(*shaderModule, {}, R"(
[layout(std140)]
struct LightData
{
	lights: array[Light, 5]
}
)");
	}

	WHEN("using const if")
	{
		std::string_view sourceCode = R"(
[nzsl_version("1.0")]
module;

option UseInt: bool = false;

[cond(UseInt)]
struct inputStruct
{
	value: i32
}

[cond(!UseInt)]
struct inputStruct
{
	value: f32
}

external
{
	[set(0), binding(0)] data: uniform[inputStruct]
}

[entry(frag)]
fn main()
{
	let value: f32;

	const if (UseInt)
	{
		value = f32(data.value);
	}
	else
	{
		value = data.value;
	}
}
)";

		nzsl::Ast::ModulePtr shaderModule;
		REQUIRE_NOTHROW(shaderModule = nzsl::Parse(sourceCode));

		nzsl::Ast::SanitizeVisitor::Options options;

		WHEN("Enabling option")
		{
			options.optionValues[Nz::CRC32("UseInt")] = true;

			ExpectOutput(*shaderModule, options, R"(
struct inputStruct
{
	value: i32
}

external
{
	[set(0), binding(0)] data: uniform[inputStruct]
}

[entry(frag)]
fn main()
{
	let value: f32;
	value = f32(data.value);
}
)");
		}

		WHEN("Disabling option")
		{
			options.optionValues[Nz::CRC32("UseInt")] = false;

			ExpectOutput(*shaderModule, options, R"(
struct inputStruct
{
	value: f32
}

external
{
	[set(0), binding(0)] data: uniform[inputStruct]
}

[entry(frag)]
fn main()
{
	let value: f32;
	value = data.value;
}
)");
		}
	}
	
	WHEN("using [unroll] attribute on numerical for")
	{
		std::string_view sourceCode = R"(
[nzsl_version("1.0")]
module;

const LightCount = 3;

[layout(std140)]
struct Light
{
	color: vec4[f32]
}

[layout(std140)]
struct LightData
{
	lights: array[Light, LightCount]
}

external
{
	[set(0), binding(0)] data: uniform[LightData]
}

[entry(frag)]
fn main()
{
	let color = (0.0).xxxx;

	[unroll]
	for i in 0 -> 10 : 2
	{
		color += data.lights[i].color;
	}
}
)";

		nzsl::Ast::ModulePtr shaderModule;
		REQUIRE_NOTHROW(shaderModule = nzsl::Parse(sourceCode));

		ExpectOutput(*shaderModule, {}, R"(
[entry(frag)]
fn main()
{
	let color: vec4[f32] = (0.0).xxxx;
	{
		let i: i32 = 0;
		color += data.lights[i].color;
	}

	{
		let i: i32 = 2;
		color += data.lights[i].color;
	}

	{
		let i: i32 = 4;
		color += data.lights[i].color;
	}

	{
		let i: i32 = 6;
		color += data.lights[i].color;
	}

	{
		let i: i32 = 8;
		color += data.lights[i].color;
	}

}
)");
	}

	WHEN("using [unroll] attribute on for-each")
	{
		std::string_view sourceCode = R"(
[nzsl_version("1.0")]
module;

const LightCount = 3;

[layout(std140)]
struct Light
{
	color: vec4[f32]
}

[layout(std140)]
struct LightData
{
	lights: array[Light, LightCount]
}

external
{
	[set(0), binding(0)] data: uniform[LightData]
}

[entry(frag)]
fn main()
{
	let color = (0.0).xxxx;

	[unroll]
	for light in data.lights
	{
		color += light.color;
	}
}
)";

		nzsl::Ast::ModulePtr shaderModule;
		REQUIRE_NOTHROW(shaderModule = nzsl::Parse(sourceCode));

		ExpectOutput(*shaderModule, {}, R"(
[entry(frag)]
fn main()
{
	let color: vec4[f32] = (0.0).xxxx;
	{
		let light: Light = data.lights[0];
		color += light.color;
	}

	{
		let light: Light = data.lights[1];
		color += light.color;
	}

	{
		let light: Light = data.lights[2];
		color += light.color;
	}

}
)");
	}
}
