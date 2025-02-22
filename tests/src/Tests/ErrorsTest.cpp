#include <Tests/ShaderUtils.hpp>
#include <NZSL/FilesystemModuleResolver.hpp>
#include <NZSL/Parser.hpp>
#include <NZSL/Ast/SanitizeVisitor.hpp>
#include <catch2/catch.hpp>

TEST_CASE("errors", "[Shader]")
{
	SECTION("Checking lexer errors")
	{
		CHECK_THROWS_WITH(nzsl::Tokenize("1x42"), "(1,1 -> 4): LBadNumber error: bad number");
		CHECK_THROWS_WITH(nzsl::Tokenize("123456789876543210123456789"), "(1,1 -> 27): LNumberOutOfRange error: number is out of range");
		CHECK_THROWS_WITH(nzsl::Tokenize("\"Hello world"), "(1,1 -> 13): LUnfinishedString error: unfinished string");
		CHECK_THROWS_WITH(nzsl::Tokenize(R"("hello \p")"), "(1,1 -> 9): LUnrecognizedChar error: unrecognized character");
		CHECK_THROWS_WITH(nzsl::Tokenize("$"), "(1, 1): LUnrecognizedToken error: unrecognized token");
	}

	SECTION("Checking parser errors")
	{
		CHECK_THROWS_WITH(nzsl::Parse("nazara"), "(1,1 -> 6): PUnexpectedToken error: unexpected token Identifier");
		CHECK_THROWS_WITH(nzsl::Parse("module;"), "(1,1 -> 6): PMissingAttribute error: missing attribute nzsl_version");
		CHECK_THROWS_WITH(nzsl::Parse("[nzsl_version] module;"), "(1,2 -> 13): PAttributeMissingParameter error: attribute nzsl_version requires a parameter");
		CHECK_THROWS_WITH(nzsl::Parse("[nzsl_version(\"1.0\"), nzsl_version(\"1.0\")] module;"), "(1,23 -> 41): PAttributeMultipleUnique error: attribute nzsl_version can only be present once");
		CHECK_THROWS_WITH(nzsl::Parse("[nzsl_version(\"1.0\"), author(\"Lynix\"), author(\"Sir Lynix\")] module;"), "(1,40 -> 58): PAttributeMultipleUnique error: attribute author can only be present once");
		CHECK_THROWS_WITH(nzsl::Parse("[nzsl_version(\"1.0\"), author(\"Lynix\"), desc(\"Desc\")] [desc(\"Description\")] module;"), "(1,55 -> 73): PAttributeMultipleUnique error: attribute desc can only be present once");
		CHECK_THROWS_WITH(nzsl::Parse("[nzsl_version(\"1.0\"), author(\"Lynix\"), desc(\"Desc\"), license(\"Public domain\")] [license(\"MIT\")] module;"), "(1,81 -> 94): PAttributeMultipleUnique error: attribute license can only be present once");

		CHECK_THROWS_WITH(nzsl::Parse(R"(
[nzsl_version("1.0")]
[feature(non_existent)]
module;
)"), "(3,10 -> 21): PAttributeInvalidParameter error: invalid parameter non_existent for attribute feature");

		CHECK_THROWS_WITH(nzsl::Parse(R"(
[nzsl_version("1.0")]
[feature(primitive_externals)]
[feature(primitive_externals)]
module;
)"), "(4,2 -> 29): PModuleFeatureMultipleUnique error: module feature primitive_externals has already been specified");

		CHECK_THROWS_WITH(nzsl::Parse(R"(
[nzsl_version("1.0")]
module;

[cond(true)]
)"), "(7, 0): PUnexpectedToken error: unexpected token EndOfStream");

		// alias statements don't support attributes
		CHECK_THROWS_WITH(nzsl::Parse(R"(
[nzsl_version("1.0")]
module;

[cond(false)]
alias vec3f32 = vec3[f32];
)"), "(5,2 -> 12): PUnexpectedAttribute error: unexpected attribute cond");

		// import statements don't support cond attribute
		CHECK_THROWS_WITH(nzsl::Parse(R"(
[nzsl_version("1.0")]
module;

[cond(true)]
import Stuff;
)"), "(5,2 -> 11): PUnexpectedAttribute error: unexpected attribute cond");

		// option statements don't support attributes
		CHECK_THROWS_WITH(nzsl::Parse(R"(
[nzsl_version("1.0")]
module;

[cond(false)]
option enable: bool;
)"), "(5,2 -> 12): PUnexpectedAttribute error: unexpected attribute cond");
	}

	SECTION("Checking compiler errors")
	{
		auto Compile = [](std::string_view sourceCode)
		{
			nzsl::Ast::Sanitize(*nzsl::Parse(sourceCode));
		};

		/************************************************************************/

		SECTION("Arrays")
		{
			// unsized arrays can only be used on declaration (for implicit size)
			CHECK_NOTHROW(Compile(R"(
[nzsl_version("1.0")]
module;

const data = array[f32](1.0, 2.0, 3.0);

fn main()
{
	let runtimeData = array[i32](1, 2, 3, 4, 5);
}
)"));

			// however it's an error to give a size and provide less parameters than specified
			CHECK_THROWS_WITH(Compile(R"(
[nzsl_version("1.0")]
module;

const data = array[f32, 4](1.0, 2.0, 3.0);
)"), "(5,14 -> 41): CCastComponentMismatch error: component count (3) doesn't match required component count (4)");

			// and more
			CHECK_THROWS_WITH(Compile(R"(
[nzsl_version("1.0")]
module;

const data = array[f32, 2](1.0, 2.0, 3.0);
)"), "(5,14 -> 41): CCastComponentMismatch error: component count (3) doesn't match required component count (2)");

			// it's an error to declare an unsized array outside of this case
			CHECK_THROWS_WITH(Compile(R"(
[nzsl_version("1.0")]
module;

fn main()
{
	let data: array[f32];
}
)"), "(7,2 -> 22): CArrayLengthRequired error: array length is required in this context");

			CHECK_THROWS_WITH(Compile(R"(
[nzsl_version("1.0")]
module;

struct Data
{
	data: array[bool]
}
)"), "(7,2 -> 5): CArrayLengthRequired error: array length is required in this context");

			CHECK_THROWS_WITH(Compile(R"(
[nzsl_version("1.0")]
module;

fn test(param: array[f32])
{
}
)"), "(5,9 -> 25): CArrayLengthRequired error: array length is required in this context");

			// TODO: if the error happens on the return type, the whole function gets flagged (add source location to ExpressionValue ?)
			CHECK_THROWS_WITH(Compile(R"(
[nzsl_version("1.0")]
module;

fn test() -> array[f32]
{
	let x = array[f32](1.0);
	return x;
}
)"), "(5 -> 9,1 -> 1): CArrayLengthRequired error: array length is required in this context");
		}

		/************************************************************************/

		SECTION("Builtins")
		{
			CHECK_THROWS_WITH(Compile(R"(
[nzsl_version("1.0")]
module;

struct Input
{
	[builtin(position)] pos: f32
}
		)"), "(7,22 -> 24): CBuiltinUnexpectedType error: builtin position expected type vec4[f32], got type f32");

			// If the member is not used, no error should happen
			CHECK_NOTHROW(Compile(R"(
[nzsl_version("1.0")]
module;

struct Input
{
	[builtin(position)] pos: vec4[f32]
}

fn test(input: Input)
{
}

[entry(frag)]
fn main(input: Input) 
{
	test(input);
})"));

			CHECK_THROWS_WITH(Compile(R"(
[nzsl_version("1.0")]
module;

struct Input
{
	[location(0)] data: f32
}

fn clip(v: f32)
{
	if (v < 0.0)
		discard;
}

[entry(vert)]
fn main(input: Input) 
{
	clip(input.data);
})"), "(13,3 -> 9): CInvalidStageDependency error: this is only valid in the fragment stage but this functions gets called in the vertex stage");

			CHECK_THROWS_WITH(Compile(R"(
[nzsl_version("1.0")]
module;

struct Input
{
	[builtin(position)] pos: vec4[f32]
}

fn test(input: Input) -> vec4[f32]
{
	return input.pos;
}

[entry(frag)]
fn main(input: Input) 
{
	test(input);
})"), "(12,9 -> 17): CBuiltinUnsupportedStage error: builtin position is not available in fragment stage");
		}

		/************************************************************************/

		SECTION("Casts")
		{
			CHECK_THROWS_WITH(Compile(R"(
[nzsl_version("1.0")]
module;

fn main()
{
	let a: vec2[f32];
	let b: vec3[f32];
	let x = mat2[f32](a, b);
}
)"), "(9, 23): CCastMatrixVectorComponentMismatch error: vector component count (3) doesn't match target matrix row count (2)");
		}

		/************************************************************************/

		SECTION("Constants")
		{
			CHECK_THROWS_WITH(Compile(R"(
[nzsl_version("1.0")]
module;

const Pi: f32 = 3;

)"), "(5, 17): CVarDeclarationTypeUnmatching error: initial expression type (i32) doesn't match specified type (f32)");
		}
		/************************************************************************/

		SECTION("Constant propagation")
		{
			CHECK_THROWS_WITH(Compile(R"(
[nzsl_version("1.0")]
module;

const V = 21 * 2 / (9 - 3 * 3);

)"), "(5,11 -> 30): CIntegralDivisionByZero error: integral division by zero in expression (42 / 0)");

			CHECK_THROWS_WITH(Compile(R"(
[nzsl_version("1.0")]
module;

const V = vec4[i32](7, 6, 5, 4) / vec4[i32](3, 2, 1, 0);

)"), "(5,11 -> 55): CIntegralDivisionByZero error: integral division by zero in expression (vec4[i32](7, 6, 5, 4) / vec4[i32](3, 2, 1, 0))");

			CHECK_THROWS_WITH(Compile(R"(
[nzsl_version("1.0")]
module;

const V = 21 * 2 % (9 - 3 * 3);

)"), "(5,11 -> 30): CIntegralModuloByZero error: integral modulo by zero in expression (42 % 0)");

			CHECK_THROWS_WITH(Compile(R"(
[nzsl_version("1.0")]
module;

const V = vec4[i32](7, 6, 5, 4) % vec4[i32](3, 2, 1, 0);

)"), "(5,11 -> 55): CIntegralModuloByZero error: integral modulo by zero in expression (vec4[i32](7, 6, 5, 4) % vec4[i32](3, 2, 1, 0))");
		}

		/************************************************************************/

		SECTION("Features")
		{
			CHECK_THROWS_WITH(Compile(R"(
[nzsl_version("1.0")]
module;

external
{
	[binding(0)] data: mat4[f32]
}
)"), "(7,15 -> 29): CExtTypeNotAllowed error: external variable data has unauthorized type (mat4[f32]): only storage buffers, samplers and uniform buffers (and primitives, vectors and matrices if primitive external feature is enabled) are allowed in external blocks");
		}

		/************************************************************************/

		SECTION("Import")
		{
			CHECK_THROWS_WITH(Compile(R"(
[nzsl_version("1.0")]
module;

import *, * from Module;

)"), "(5, 11): CImportMultipleWildcard error: only one wildcard can be present in an import directive");

			CHECK_THROWS_WITH(Compile(R"(
[nzsl_version("1.0")]
module;

import * as Y from Module;

)"), "(5,8 -> 13): CImportWildcardRename error: wildcard cannot be renamed");

			CHECK_THROWS_WITH(Compile(R"(
[nzsl_version("1.0")]
module;

import X, X from Module;

)"), "(5, 11): CImportIdentifierAlreadyPresent error: X identifier was already imported");
		}

		/************************************************************************/

		SECTION("Intrinsics")
		{
			CHECK_THROWS_WITH(Compile(R"(
[nzsl_version("1.0")]
module;

fn main()
{
	let a = mat2x3[f32](1.0, 2.0, 3.0, 4.0, 5.0, 6.0);
	let b = inverse(a);
}
)"), "(8, 18): CIntrinsicExpectedType error: expected type square matrix for parameter #0, got mat2x3[f32]");
		}

		/************************************************************************/

		SECTION("Loops")
		{
			CHECK_THROWS_WITH(Compile(R"(
[nzsl_version("1.0")]
module;

fn main()
{
	break;
}
)"), "(7,2 -> 6): CLoopControlOutsideOfLoop error: loop control instruction break found outside of loop");

			CHECK_THROWS_WITH(Compile(R"(
[nzsl_version("1.0")]
module;

fn main()
{
	continue;
}
)"), "(7,2 -> 9): CLoopControlOutsideOfLoop error: loop control instruction continue found outside of loop");

			// break is forbidden in a unrolled loop
			CHECK_THROWS_WITH(Compile(R"(
[nzsl_version("1.0")]
module;

fn main()
{
	[unroll]
	for i in 0 -> 10
	{
		if (i > 5)
			break;
	}
}
)"), "(11,4 -> 8): CLoopControlOutsideOfLoop error: loop control instruction break found outside of loop");

			// continue is forbidden in a unrolled loop
			CHECK_THROWS_WITH(Compile(R"(
[nzsl_version("1.0")]
module;

fn main()
{
	[unroll]
	for i in 0 -> 10
	{
		if (i == 5)
			continue;
	}
}
)"), "(11,4 -> 11): CLoopControlOutsideOfLoop error: loop control instruction continue found outside of loop");
		}

		/************************************************************************/

		SECTION("Modules")
		{
			std::string_view importedSource = R"(
[nzsl_version("1.0")]
[feature(primitive_externals)]
module Module;

external
{
	data: mat4[f32]
}
)";

			std::string_view shaderSource = R"(
[nzsl_version("1.0")]
module;

import * from Module;
)";

			nzsl::Ast::ModulePtr shaderModule = nzsl::Parse(shaderSource);

			auto directoryModuleResolver = std::make_shared<nzsl::FilesystemModuleResolver>();
			directoryModuleResolver->RegisterModule(importedSource);

			nzsl::Ast::SanitizeVisitor::Options sanitizeOpt;
			sanitizeOpt.moduleResolver = directoryModuleResolver;

			CHECK_THROWS_WITH(nzsl::Ast::Sanitize(*shaderModule, sanitizeOpt), "(5,1 -> 21): CModuleFeatureMismatch error: module Module requires feature primitive_externals");
		}

		/************************************************************************/

		SECTION("Options")
		{
			CHECK_THROWS_WITH(Compile(R"(
[nzsl_version("1.0")]
module;

option test: bool = 42;

)"), "(5,1 -> 23): CVarDeclarationTypeUnmatching error: initial expression type (bool) doesn't match specified type (i32)");
		}
		/************************************************************************/

		SECTION("Variables")
		{
			CHECK_THROWS_WITH(Compile(R"(
[nzsl_version("1.0")]
module;

fn main()
{
	let a: i32 = 42.66;
}
)"), "(7,2 -> 20): CVarDeclarationTypeUnmatching error: initial expression type (i32) doesn't match specified type (f32)");
		}
	}
}
