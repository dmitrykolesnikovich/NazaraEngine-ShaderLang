#include <Tests/ShaderUtils.hpp>
#include <NZSL/ShaderBuilder.hpp>
#include <NZSL/Parser.hpp>
#include <NZSL/Ast/ConstantPropagationVisitor.hpp>
#include <NZSL/Ast/EliminateUnusedPassVisitor.hpp>
#include <NZSL/Ast/SanitizeVisitor.hpp>
#include <catch2/catch.hpp>
#include <cctype>

void PropagateConstantAndExpect(std::string_view sourceCode, std::string_view expectedOptimizedResult)
{
	nzsl::Ast::ModulePtr shaderModule;
	REQUIRE_NOTHROW(shaderModule = nzsl::Parse(sourceCode));
	shaderModule = SanitizeModule(*shaderModule);
	REQUIRE_NOTHROW(shaderModule = nzsl::Ast::PropagateConstants(*shaderModule));

	ExpectNZSL(*shaderModule, expectedOptimizedResult);
}

void EliminateUnusedAndExpect(std::string_view sourceCode, std::string_view expectedOptimizedResult)
{
	nzsl::Ast::DependencyCheckerVisitor::Config depConfig;
	depConfig.usedShaderStages = nzsl::ShaderStageType_All;

	nzsl::Ast::ModulePtr shaderModule;
	REQUIRE_NOTHROW(shaderModule = nzsl::Parse(sourceCode));
	shaderModule = SanitizeModule(*shaderModule);
	REQUIRE_NOTHROW(shaderModule = nzsl::Ast::EliminateUnusedPass(*shaderModule, depConfig));

	ExpectNZSL(*shaderModule, expectedOptimizedResult);
}

TEST_CASE("optimizations", "[Shader]")
{
	WHEN("propagating constants")
	{
		PropagateConstantAndExpect(R"(
[nzsl_version("1.0")]
module;

[entry(frag)]
fn main()
{
	let output = 8.0 * (7.0 + 5.0) * 2.0 / 4.0 - 6.0 % 7.0;
	let output2 = 8 * (7 + 5) * 2 / 4 - 6 % 7;
}
)", R"(
[entry(frag)]
fn main()
{
	let output: f32 = 42.0;
	let output2: i32 = 42;
}
)");
	}

	WHEN("propagating vector constants")
	{
		PropagateConstantAndExpect(R"(
[nzsl_version("1.0")]
module;

[entry(frag)]
fn main()
{
	let output = vec4[f32](8.0, 2.0, -7.0, 0.0) * (7.0 + 5.0) * 2.0 / 4.0;
	let output2 = vec4[i32](8, 2, -7, 0) * (7 + 5) * 2 / 4;
}
)", R"(
[entry(frag)]
fn main()
{
	let output: vec4[f32] = vec4[f32](48.0, 12.0, -42.0, 0.0);
	let output2: vec4[i32] = vec4[i32](48, 12, -42, 0);
)");
	}

	WHEN("eliminating simple branch")
	{
		PropagateConstantAndExpect(R"(
[nzsl_version("1.0")]
module;

[entry(frag)]
fn main()
{
	if (5 + 3 < 2)
		discard;
}
)", R"(
[entry(frag)]
fn main()
{

}
)");
	}

	WHEN("eliminating multiple branches")
	{
		PropagateConstantAndExpect(R"(
[nzsl_version("1.0")]
module;

[entry(frag)]
fn main()
{
	let output = 0.0;
	if (5 <= 3)
		output = 5.0;
	else if (4 <= 3)
		output = 4.0;
	else if (3 <= 3)
		output = 3.0;
	else if (2 <= 3)
		output = 2.0;
	else if (1 <= 3)
		output = 1.0;
	else
		output = 0.0;
}
)", R"(
[entry(frag)]
fn main()
{
	let output: f32 = 0.0;
	output = 3.0;
}
)");
	}


	WHEN("eliminating multiple split branches")
	{
		PropagateConstantAndExpect(R"(
[nzsl_version("1.0")]
module;

[entry(frag)]
fn main()
{
	let output = 0.0;
	if (5 <= 3)
		output = 5.0;
	else
	{
		if (4 <= 3)
			output = 4.0;
		else
		{
			if (3 <= 3)
				output = 3.0;
			else
			{
				if (2 <= 3)
					output = 2.0;
				else
				{
					if (1 <= 3)
						output = 1.0;
					else
						output = 0.0;
				}
			}
		}
	}
}
)", R"(
[entry(frag)]
fn main()
{
	let output: f32 = 0.0;
	output = 3.0;
}
)");
	}

	WHEN("optimizing out scalar swizzle")
	{
		PropagateConstantAndExpect(R"(
[nzsl_version("1.0")]
module;

[entry(frag)]
fn main()
{
	let value = vec3[f32](3.0, 0.0, 1.0).z;
}
)", R"(
[entry(frag)]
fn main()
{
	let value: f32 = 1.0;
}
)");
	}

	WHEN("optimizing out scalar swizzle to vector")
	{
		PropagateConstantAndExpect(R"(
[nzsl_version("1.0")]
module;

[entry(frag)]
fn main()
{
	let value = (42.0).xxxx;
}
)", R"(
[entry(frag)]
fn main()
{
	let value: vec4[f32] = vec4[f32](42.0, 42.0, 42.0, 42.0);
}
)");
	}

	WHEN("optimizing out vector swizzle")
	{
		PropagateConstantAndExpect(R"(
[nzsl_version("1.0")]
module;

[entry(frag)]
fn main()
{
	let value = vec4[f32](3.0, 0.0, 1.0, 2.0).yzwx;
}
)", R"(
[entry(frag)]
fn main()
{
	let value: vec4[f32] = vec4[f32](0.0, 1.0, 2.0, 3.0);
}
)");
	}

	WHEN("optimizing out vector swizzle with repetition")
	{
		PropagateConstantAndExpect(R"(
[nzsl_version("1.0")]
module;

[entry(frag)]
fn main()
{
	let value = vec4[f32](3.0, 0.0, 1.0, 2.0).zzxx;
}
)", R"(
[entry(frag)]
fn main()
{
	let value: vec4[f32] = vec4[f32](1.0, 1.0, 3.0, 3.0);
}
)");
	}

	WHEN("optimizing out complex swizzle")
	{
		PropagateConstantAndExpect(R"(
[nzsl_version("1.0")]
module;

[entry(frag)]
fn main()
{
	let value = vec4[f32](0.0, 1.0, 2.0, 3.0).xyz.yz.y.x.xxxx;
}
)", R"(
[entry(frag)]
fn main()
{
	let value: vec4[f32] = vec4[f32](2.0, 2.0, 2.0, 2.0);
}
)");
	}

	WHEN("optimizing out complex swizzle on unknown value")
	{
		PropagateConstantAndExpect(R"(
[nzsl_version("1.0")]
module;

struct inputStruct
{
	value: vec4[f32]
}

external
{
	[set(0), binding(0)] data: uniform[inputStruct]
}

[entry(frag)]
fn main()
{
	let value = data.value.xyz.yz.y.x.xxxx;
}
)", R"(
[entry(frag)]
fn main()
{
	let value: vec4[f32] = data.value.zzzz;
}
)");
	}

	WHEN("eliminating unused code")
	{
		EliminateUnusedAndExpect(R"(
[nzsl_version("1.0")]
module;

struct inputStruct
{
	value: vec4[f32]
}

struct notUsed
{
	value: vec4[f32]
}

external
{
	[set(0), binding(0)] unusedData: uniform[notUsed],
	[set(0), binding(1)] data: uniform[inputStruct]
}

fn unusedFunction() -> vec4[f32]
{
	return unusedData.value;
}

struct Output
{
	value: vec4[f32]
}

[entry(frag)]
fn main() -> Output
{
	let unusedvalue = unusedFunction();

	let output: Output;
	output.value = data.value;
	return output;
})", R"(
[nzsl_version("1.0")]
module;

struct inputStruct
{
	value: vec4[f32]
}

external
{
	[set(0), binding(1)] data: uniform[inputStruct]
}

struct Output
{
	value: vec4[f32]
}

[entry(frag)]
fn main() -> Output
{
	let output: Output;
	output.value = data.value;
	return output;
})");
	}
}
