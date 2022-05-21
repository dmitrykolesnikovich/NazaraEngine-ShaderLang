// Copyright (C) 2022 Jérôme "Lynix" Leclercq (lynix680@gmail.com)
// This file is part of the "Nazara Shading Language" project
// For conditions of distribution and use, see copyright notice in Config.hpp

#include <NZSL/SpirvWriter.hpp>
#include <Nazara/Utils/CallOnExit.hpp>
#include <Nazara/Utils/StackVector.hpp>
#include <NZSL/Enums.hpp>
#include <NZSL/SpirvAstVisitor.hpp>
#include <NZSL/SpirvBlock.hpp>
#include <NZSL/SpirvConstantCache.hpp>
#include <NZSL/SpirvData.hpp>
#include <NZSL/SpirvSection.hpp>
#include <NZSL/Ast/AstCloner.hpp>
#include <NZSL/Ast/AstConstantPropagationVisitor.hpp>
#include <NZSL/Ast/AstRecursiveVisitor.hpp>
#include <NZSL/Ast/EliminateUnusedPassVisitor.hpp>
#include <NZSL/Ast/SanitizeVisitor.hpp>
#include <SpirV/GLSL.std.450.h>
#include <frozen/unordered_map.h>
#include <tsl/ordered_map.h>
#include <tsl/ordered_set.h>
#include <cassert>
#include <map>
#include <stdexcept>
#include <type_traits>
#include <vector>

namespace nzsl
{
	namespace
	{
		struct SpirvBuiltin
		{
			const char* debugName;
			ShaderStageTypeFlags compatibleStages;
			SpirvBuiltIn decoration;
		};

		constexpr auto s_spirvBuiltinMapping = frozen::make_unordered_map<ShaderAst::BuiltinEntry, SpirvBuiltin>({
			{ ShaderAst::BuiltinEntry::FragCoord,      { "FragmentCoordinates", ShaderStageType::Fragment, SpirvBuiltIn::FragCoord } },
			{ ShaderAst::BuiltinEntry::FragDepth,      { "FragmentDepth",       ShaderStageType::Fragment, SpirvBuiltIn::FragDepth } },
			{ ShaderAst::BuiltinEntry::VertexPosition, { "VertexPosition",      ShaderStageType::Vertex,   SpirvBuiltIn::Position } }
		});

		class SpirvPreVisitor : public ShaderAst::AstRecursiveVisitor
		{
			public:
				struct UniformVar
				{
					std::uint32_t bindingIndex;
					std::uint32_t descriptorSet;
					std::uint32_t pointerId;
				};

				using BuiltinDecoration = std::map<std::uint32_t, SpirvBuiltIn>;
				using LocationDecoration = std::map<std::uint32_t, std::uint32_t>;
				using ExtInstList = std::unordered_set<std::string>;
				using ExtVarContainer = std::unordered_map<std::size_t /*varIndex*/, UniformVar>;
				using LocalContainer = std::unordered_set<ShaderAst::ExpressionType>;
				using FunctionContainer = std::vector<std::reference_wrapper<ShaderAst::DeclareFunctionStatement>>;
				using StructContainer = std::vector<ShaderAst::StructDescription*>;

				SpirvPreVisitor(SpirvConstantCache& constantCache, std::unordered_map<std::size_t, SpirvAstVisitor::FuncData>& funcs) :
				m_constantCache(constantCache),
				m_funcs(funcs)
				{
					m_constantCache.SetStructCallback([this](std::size_t structIndex) -> const ShaderAst::StructDescription&
					{
						assert(structIndex < declaredStructs.size());
						return *declaredStructs[structIndex];
					});
				}

				void Visit(ShaderAst::AccessIndexExpression& node) override
				{
					AstRecursiveVisitor::Visit(node);

					m_constantCache.Register(*m_constantCache.BuildType(node.cachedExpressionType.value()));
				}

				void Visit(ShaderAst::BinaryExpression& node) override
				{
					AstRecursiveVisitor::Visit(node);

					m_constantCache.Register(*m_constantCache.BuildType(node.cachedExpressionType.value()));
				}

				void Visit(ShaderAst::CallFunctionExpression& node) override
				{
					AstRecursiveVisitor::Visit(node);

					assert(m_funcIndex);
					auto& func = Nz::Retrieve(m_funcs, *m_funcIndex);

					auto& funcCall = func.funcCalls.emplace_back();
					funcCall.firstVarIndex = func.variables.size();

					for (const auto& parameter : node.parameters)
					{
						auto& var = func.variables.emplace_back();
						var.typeId = m_constantCache.Register(*m_constantCache.BuildPointerType(*GetExpressionType(*parameter), SpirvStorageClass::Function));
					}
				}

				void Visit(ShaderAst::ConditionalExpression& /*node*/) override
				{
					throw std::runtime_error("unexpected conditional expression, did you forget to sanitize the shader?");
				}

				void Visit(ShaderAst::ConditionalStatement& /*node*/) override
				{
					throw std::runtime_error("unexpected conditional expression, did you forget to sanitize the shader?");
				}

				void Visit(ShaderAst::ConstantValueExpression& node) override
				{
					std::visit([&](auto&& arg)
					{
						m_constantCache.Register(*m_constantCache.BuildConstant(arg));
					}, node.value);

					AstRecursiveVisitor::Visit(node);
				}

				void Visit(ShaderAst::DeclareExternalStatement& node) override
				{
					for (auto& extVar : node.externalVars)
					{
						SpirvConstantCache::Variable variable;
						variable.debugName = extVar.name;

						const ShaderAst::ExpressionType& extVarType = extVar.type.GetResultingValue();

						if (ShaderAst::IsSamplerType(extVarType))
						{
							variable.storageClass = SpirvStorageClass::UniformConstant;
							variable.type = m_constantCache.BuildPointerType(extVarType, variable.storageClass);
						}
						else
						{
							assert(ShaderAst::IsUniformType(extVarType));
							const auto& uniformType = std::get<ShaderAst::UniformType>(extVarType);
							const auto& structType = uniformType.containedType;
							assert(structType.structIndex < declaredStructs.size());
							const auto& type = m_constantCache.BuildType(*declaredStructs[structType.structIndex], { SpirvDecoration::Block });

							variable.storageClass = SpirvStorageClass::Uniform;
							variable.type = m_constantCache.BuildPointerType(type, variable.storageClass);
						}

						assert(extVar.bindingIndex.IsResultingValue());

						assert(extVar.varIndex);
						UniformVar& uniformVar = extVars[*extVar.varIndex];
						uniformVar.pointerId = m_constantCache.Register(variable);
						uniformVar.bindingIndex = extVar.bindingIndex.GetResultingValue();
						uniformVar.descriptorSet = (extVar.bindingSet.HasValue()) ? extVar.bindingSet.GetResultingValue() : 0;
					}
				}

				void Visit(ShaderAst::DeclareFunctionStatement& node) override
				{
					std::optional<ShaderStageType> entryPointType;
					if (node.entryStage.HasValue())
						entryPointType = node.entryStage.GetResultingValue();

					assert(node.funcIndex);
					std::size_t funcIndex = *node.funcIndex;

					auto& funcData = m_funcs[funcIndex];
					funcData.name = node.name;
					funcData.funcIndex = funcIndex;

					if (!entryPointType)
					{
						std::vector<ShaderAst::ExpressionType> parameterTypes;
						for (auto& parameter : node.parameters)
							parameterTypes.push_back(parameter.type.GetResultingValue());

						if (node.returnType.HasValue())
						{
							const auto& returnType = node.returnType.GetResultingValue();
							funcData.returnTypeId = m_constantCache.Register(*m_constantCache.BuildType(returnType));
							funcData.funcTypeId = m_constantCache.Register(*m_constantCache.BuildFunctionType(returnType, parameterTypes));
						}
						else
						{
							funcData.returnTypeId = m_constantCache.Register(*m_constantCache.BuildType(ShaderAst::NoType{}));
							funcData.funcTypeId = m_constantCache.Register(*m_constantCache.BuildFunctionType(ShaderAst::NoType{}, parameterTypes));
						}

						for (auto& parameter : node.parameters)
						{
							const auto& parameterType = parameter.type.GetResultingValue();

							auto& funcParam = funcData.parameters.emplace_back();
							funcParam.pointerTypeId = m_constantCache.Register(*m_constantCache.BuildPointerType(parameterType, SpirvStorageClass::Function));
							funcParam.typeId = m_constantCache.Register(*m_constantCache.BuildType(parameterType));
						}
					}
					else
					{
						using EntryPoint = SpirvAstVisitor::EntryPoint;

						std::vector<SpirvExecutionMode> executionModes;

						if (*entryPointType == ShaderStageType::Fragment)
						{
							executionModes.push_back(SpirvExecutionMode::OriginUpperLeft);
							if (node.earlyFragmentTests.HasValue() && node.earlyFragmentTests.GetResultingValue())
								executionModes.push_back(SpirvExecutionMode::EarlyFragmentTests);

							if (node.depthWrite.HasValue())
							{
								executionModes.push_back(SpirvExecutionMode::DepthReplacing);

								switch (node.depthWrite.GetResultingValue())
								{
									case ShaderAst::DepthWriteMode::Replace:   break;
									case ShaderAst::DepthWriteMode::Greater:   executionModes.push_back(SpirvExecutionMode::DepthGreater); break;
									case ShaderAst::DepthWriteMode::Less:      executionModes.push_back(SpirvExecutionMode::DepthLess); break;
									case ShaderAst::DepthWriteMode::Unchanged: executionModes.push_back(SpirvExecutionMode::DepthUnchanged); break;
								}
							}
						}

						funcData.returnTypeId = m_constantCache.Register(*m_constantCache.BuildType(ShaderAst::NoType{}));
						funcData.funcTypeId = m_constantCache.Register(*m_constantCache.BuildFunctionType(ShaderAst::NoType{}, {}));

						std::optional<EntryPoint::InputStruct> inputStruct;
						std::vector<EntryPoint::Input> inputs;
						if (!node.parameters.empty())
						{
							assert(node.parameters.size() == 1);
							auto& parameter = node.parameters.front();
							const auto& parameterType = parameter.type.GetResultingValue();

							assert(std::holds_alternative<ShaderAst::StructType>(parameterType));

							std::size_t structIndex = std::get<ShaderAst::StructType>(parameterType).structIndex;
							const ShaderAst::StructDescription* structDesc = declaredStructs[structIndex];

							std::size_t memberIndex = 0;
							for (const auto& member : structDesc->members)
							{
								if (member.cond.HasValue() && !member.cond.GetResultingValue())
									continue;

								if (std::uint32_t varId = HandleEntryInOutType(*entryPointType, funcIndex, member, SpirvStorageClass::Input); varId != 0)
								{
									inputs.push_back({
										m_constantCache.Register(*m_constantCache.BuildConstant(std::int32_t(memberIndex))),
										m_constantCache.Register(*m_constantCache.BuildPointerType(member.type.GetResultingValue(), SpirvStorageClass::Function)),
										varId
									});
								}

								memberIndex++;
							}

							inputStruct = EntryPoint::InputStruct{
								m_constantCache.Register(*m_constantCache.BuildPointerType(parameterType, SpirvStorageClass::Function)),
								m_constantCache.Register(*m_constantCache.BuildType(parameter.type.GetResultingValue()))
							};
						}

						std::optional<std::uint32_t> outputStructId;
						std::vector<EntryPoint::Output> outputs;
						if (node.returnType.HasValue() && !IsNoType(node.returnType.GetResultingValue()))
						{
							const ShaderAst::ExpressionType& returnType = node.returnType.GetResultingValue();

							assert(std::holds_alternative<ShaderAst::StructType>(returnType));

							std::size_t structIndex = std::get<ShaderAst::StructType>(returnType).structIndex;
							const ShaderAst::StructDescription* structDesc = declaredStructs[structIndex];

							std::size_t memberIndex = 0;
							for (const auto& member : structDesc->members)
							{
								if (member.cond.HasValue() && !member.cond.GetResultingValue())
									continue;

								if (std::uint32_t varId = HandleEntryInOutType(*entryPointType, funcIndex, member, SpirvStorageClass::Output); varId != 0)
								{
									outputs.push_back({
										std::int32_t(memberIndex),
										m_constantCache.Register(*m_constantCache.BuildType(member.type.GetResultingValue())),
										varId
									});
								}

								memberIndex++;
							}

							outputStructId = m_constantCache.Register(*m_constantCache.BuildType(returnType));
						}

						funcData.entryPointData = EntryPoint{
							*entryPointType,
							inputStruct,
							outputStructId,
							std::move(inputs),
							std::move(outputs),
							std::move(executionModes)
						};
					}

					m_funcIndex = funcIndex;
					AstRecursiveVisitor::Visit(node);
					m_funcIndex.reset();
				}

				void Visit(ShaderAst::DeclareStructStatement& node) override
				{
					AstRecursiveVisitor::Visit(node);

					assert(node.structIndex);
					std::size_t structIndex = *node.structIndex;
					if (structIndex >= declaredStructs.size())
						declaredStructs.resize(structIndex + 1);

					declaredStructs[structIndex] = &node.description;

					m_constantCache.Register(*m_constantCache.BuildType(node.description));
				}

				void Visit(ShaderAst::DeclareVariableStatement& node) override
				{
					AstRecursiveVisitor::Visit(node);

					assert(m_funcIndex);
					auto& func = m_funcs[*m_funcIndex];

					assert(node.varIndex);
					func.varIndexToVarId[*node.varIndex] = func.variables.size();

					auto& var = func.variables.emplace_back();
					var.typeId = m_constantCache.Register(*m_constantCache.BuildPointerType(node.varType.GetResultingValue(), SpirvStorageClass::Function));
				}

				void Visit(ShaderAst::IdentifierExpression& node) override
				{
					m_constantCache.Register(*m_constantCache.BuildType(node.cachedExpressionType.value()));

					AstRecursiveVisitor::Visit(node);
				}

				void Visit(ShaderAst::IntrinsicExpression& node) override
				{
					AstRecursiveVisitor::Visit(node);

					switch (node.intrinsic)
					{
						// Require GLSL.std.450
						case ShaderAst::IntrinsicType::CrossProduct:
						case ShaderAst::IntrinsicType::Exp:
						case ShaderAst::IntrinsicType::Length:
						case ShaderAst::IntrinsicType::Max:
						case ShaderAst::IntrinsicType::Min:
						case ShaderAst::IntrinsicType::Normalize:
						case ShaderAst::IntrinsicType::Pow:
						case ShaderAst::IntrinsicType::Reflect:
							extInsts.emplace("GLSL.std.450");
							break;

						// Part of SPIR-V core
						case ShaderAst::IntrinsicType::DotProduct:
						case ShaderAst::IntrinsicType::SampleTexture:
							break;
					}

					m_constantCache.Register(*m_constantCache.BuildType(node.cachedExpressionType.value()));
				}

				void Visit(ShaderAst::SwizzleExpression& node) override
				{
					AstRecursiveVisitor::Visit(node);

					for (std::size_t i = 0; i < node.componentCount; ++i)
					{
						std::int32_t indexCount = Nz::SafeCast<std::int32_t>(node.components[i]);
						m_constantCache.Register(*m_constantCache.BuildConstant(indexCount));
					}

					m_constantCache.Register(*m_constantCache.BuildType(node.cachedExpressionType.value()));
				}

				void Visit(ShaderAst::UnaryExpression& node) override
				{
					AstRecursiveVisitor::Visit(node);

					m_constantCache.Register(*m_constantCache.BuildType(node.cachedExpressionType.value()));
				}

				std::uint32_t HandleEntryInOutType(ShaderStageType entryPointType, std::size_t funcIndex, const ShaderAst::StructDescription::StructMember& member, SpirvStorageClass storageClass)
				{
					if (member.builtin.HasValue())
					{
						auto it = s_spirvBuiltinMapping.find(member.builtin.GetResultingValue());
						assert(it != s_spirvBuiltinMapping.end());

						const SpirvBuiltin& builtin = it->second;
						if ((builtin.compatibleStages & entryPointType) == 0)
							return 0;

						SpirvBuiltIn builtinDecoration = builtin.decoration;

						SpirvConstantCache::Variable variable;
						variable.debugName = builtin.debugName;
						variable.funcId = funcIndex;
						variable.storageClass = storageClass;
						variable.type = m_constantCache.BuildPointerType(member.type.GetResultingValue(), storageClass);

						std::uint32_t varId = m_constantCache.Register(variable);
						builtinDecorations[varId] = builtinDecoration;

						return varId;
					}
					else if (member.locationIndex.HasValue())
					{
						SpirvConstantCache::Variable variable;
						variable.debugName = member.name;
						variable.funcId = funcIndex;
						variable.storageClass = storageClass;
						variable.type = m_constantCache.BuildPointerType(member.type.GetResultingValue(), storageClass);

						std::uint32_t varId = m_constantCache.Register(variable);
						locationDecorations[varId] = member.locationIndex.GetResultingValue();

						return varId;
					}

					return 0;
				}

				BuiltinDecoration builtinDecorations;
				ExtInstList extInsts;
				ExtVarContainer extVars;
				LocationDecoration locationDecorations;
				StructContainer declaredStructs;

			private:
				SpirvConstantCache& m_constantCache;
				std::optional<std::size_t> m_funcIndex;
				std::unordered_map<std::size_t, SpirvAstVisitor::FuncData>& m_funcs;
		};
	}

	struct SpirvWriter::State
	{
		State() :
		constantTypeCache(nextVarIndex)
		{
		}

		struct Func
		{
			const ShaderAst::DeclareFunctionStatement* statement = nullptr;
			std::uint32_t typeId;
			std::uint32_t id;
		};

		std::unordered_map<std::string, std::uint32_t> extensionInstructionSet;
		std::unordered_map<std::string, std::uint32_t> varToResult;
		std::unordered_map<std::size_t, SpirvAstVisitor::FuncData> funcs;
		std::vector<std::uint32_t> resultIds;
		std::uint32_t nextVarIndex = 1;
		SpirvConstantCache constantTypeCache; //< init after nextVarIndex
		SpirvPreVisitor* previsitor;

		// Output
		SpirvSection header;
		SpirvSection constants;
		SpirvSection debugInfo;
		SpirvSection annotations;
		SpirvSection instructions;
	};

	SpirvWriter::SpirvWriter() :
	m_currentState(nullptr)
	{
	}

	std::vector<std::uint32_t> SpirvWriter::Generate(const ShaderAst::Module& module, const States& states)
	{
		ShaderAst::ModulePtr sanitizedModule;
		const ShaderAst::Module* targetModule;
		if (!states.sanitized)
		{
			ShaderAst::SanitizeVisitor::Options options;
			options.moduleResolver = states.shaderModuleResolver;
			options.optionValues = states.optionValues;
			options.reduceLoopsToWhile = true;
			options.removeAliases = true;
			options.removeCompoundAssignments = true;
			options.removeConstDeclaration = true;
			options.removeMatrixCast = true;
			options.removeOptionDeclaration = true;
			options.splitMultipleBranches = true;
			options.useIdentifierAccessesForStructs = false;

			sanitizedModule = ShaderAst::Sanitize(module, options);
			targetModule = sanitizedModule.get();
		}
		else
			targetModule = &module;

		ShaderAst::ModulePtr optimizedModule;
		if (states.optimize)
		{
			ShaderAst::StatementPtr tempAst;

			ShaderAst::DependencyCheckerVisitor::Config dependencyConfig;
			dependencyConfig.usedShaderStages = ShaderStageType_All;

			optimizedModule = ShaderAst::PropagateConstants(*targetModule);
			optimizedModule = ShaderAst::EliminateUnusedPass(*optimizedModule, dependencyConfig);

			targetModule = optimizedModule.get();
		}

		// Previsitor

		m_context.states = &states;

		State state;
		m_currentState = &state;
		Nz::CallOnExit onExit([this]()
		{
			m_currentState = nullptr;
		});

		// Register all extended instruction sets
		SpirvPreVisitor previsitor(state.constantTypeCache, state.funcs);
		for (const auto& importedModule : targetModule->importedModules)
			importedModule.module->rootNode->Visit(previsitor);

		targetModule->rootNode->Visit(previsitor);

		m_currentState->previsitor = &previsitor;

		for (const std::string& extInst : previsitor.extInsts)
			state.extensionInstructionSet[extInst] = AllocateResultId();

		// Assign function ID (required for forward declaration)
		for (auto&& [funcIndex, func] : state.funcs)
			func.funcId = AllocateResultId();

		SpirvAstVisitor visitor(*this, state.instructions, state.funcs);
		for (const auto& importedModule : targetModule->importedModules)
			importedModule.module->rootNode->Visit(visitor);

		targetModule->rootNode->Visit(visitor);

		AppendHeader();

		for (auto&& [varIndex, extVar] : previsitor.extVars)
		{
			state.annotations.Append(SpirvOp::OpDecorate, extVar.pointerId, SpirvDecoration::Binding, extVar.bindingIndex);
			state.annotations.Append(SpirvOp::OpDecorate, extVar.pointerId, SpirvDecoration::DescriptorSet, extVar.descriptorSet);
		}

		for (auto&& [varId, builtin] : previsitor.builtinDecorations)
			state.annotations.Append(SpirvOp::OpDecorate, varId, SpirvDecoration::BuiltIn, builtin);

		for (auto&& [varId, location] : previsitor.locationDecorations)
			state.annotations.Append(SpirvOp::OpDecorate, varId, SpirvDecoration::Location, location);

		m_currentState->constantTypeCache.Write(m_currentState->annotations, m_currentState->constants, m_currentState->debugInfo);

		std::vector<std::uint32_t> ret;
		MergeSections(ret, state.header);
		MergeSections(ret, state.debugInfo);
		MergeSections(ret, state.annotations);
		MergeSections(ret, state.constants);
		MergeSections(ret, state.instructions);

		return ret;
	}

	void SpirvWriter::SetEnv(Environment environment)
	{
		m_environment = std::move(environment);
	}

	std::uint32_t SpirvWriter::AllocateResultId()
	{
		return m_currentState->nextVarIndex++;
	}

	void SpirvWriter::AppendHeader()
	{
		m_currentState->header.AppendRaw(SpirvMagicNumber); //< Spir-V magic number

		std::uint32_t version = (m_environment.spvMajorVersion << 16) | m_environment.spvMinorVersion << 8;
		m_currentState->header.AppendRaw(version); //< Spir-V version number (1.0 for compatibility)
		m_currentState->header.AppendRaw(0); //< Generator identifier (TODO: Register generator to Khronos)

		m_currentState->header.AppendRaw(m_currentState->nextVarIndex); //< Bound (ID count)
		m_currentState->header.AppendRaw(0); //< Instruction schema (required to be 0 for now)

		m_currentState->header.Append(SpirvOp::OpCapability, SpirvCapability::Shader);

		for (const auto& [extInst, resultId] : m_currentState->extensionInstructionSet)
			m_currentState->header.Append(SpirvOp::OpExtInstImport, resultId, extInst);

		m_currentState->header.Append(SpirvOp::OpMemoryModel, SpirvAddressingModel::Logical, SpirvMemoryModel::GLSL450);

		for (auto&& [funcIndex, func] : m_currentState->funcs)
		{
			m_currentState->debugInfo.Append(SpirvOp::OpName, func.funcId, func.name);

			if (func.entryPointData)
			{
				auto& entryPointData = func.entryPointData.value();

				SpirvExecutionModel execModel;

				switch (entryPointData.stageType)
				{
					case ShaderStageType::Fragment:
						execModel = SpirvExecutionModel::Fragment;
						break;

					case ShaderStageType::Vertex:
						execModel = SpirvExecutionModel::Vertex;
						break;

					default:
						throw std::runtime_error("not yet implemented");
				}

				auto& funcData = func;
				m_currentState->header.AppendVariadic(SpirvOp::OpEntryPoint, [&](const auto& appender)
				{
					appender(execModel);
					appender(funcData.funcId);
					appender(funcData.name);

					for (const auto& input : entryPointData.inputs)
						appender(input.varId);

					for (const auto& output : entryPointData.outputs)
						appender(output.varId);
				});
			}
		}

		// Write execution modes
		for (auto&& [funcIndex, func] : m_currentState->funcs)
		{
			if (func.entryPointData)
			{
				for (SpirvExecutionMode executionMode : func.entryPointData->executionModes)
					m_currentState->header.Append(SpirvOp::OpExecutionMode, func.funcId, executionMode);
			}
		}
	}

	SpirvConstantCache::TypePtr SpirvWriter::BuildFunctionType(const ShaderAst::DeclareFunctionStatement& functionNode)
	{
		std::vector<ShaderAst::ExpressionType> parameterTypes;
		parameterTypes.reserve(functionNode.parameters.size());

		for (const auto& parameter : functionNode.parameters)
			parameterTypes.push_back(parameter.type.GetResultingValue());

		if (functionNode.returnType.HasValue())
			return m_currentState->constantTypeCache.BuildFunctionType(functionNode.returnType.GetResultingValue(), parameterTypes);
		else
			return m_currentState->constantTypeCache.BuildFunctionType(ShaderAst::NoType{}, parameterTypes);
	}

	std::uint32_t SpirvWriter::GetConstantId(const ShaderAst::ConstantValue& value) const
	{
		return m_currentState->constantTypeCache.GetId(*m_currentState->constantTypeCache.BuildConstant(value));
	}

	std::uint32_t SpirvWriter::GetExtendedInstructionSet(const std::string& instructionSetName) const
	{
		auto it = m_currentState->extensionInstructionSet.find(instructionSetName);
		assert(it != m_currentState->extensionInstructionSet.end());

		return it->second;
	}

	std::uint32_t SpirvWriter::GetExtVarPointerId(std::size_t extVarIndex) const
	{
		auto it = m_currentState->previsitor->extVars.find(extVarIndex);
		assert(it != m_currentState->previsitor->extVars.end());

		return it->second.pointerId;
	}

	std::uint32_t SpirvWriter::GetFunctionTypeId(const ShaderAst::DeclareFunctionStatement& functionNode)
	{
		return m_currentState->constantTypeCache.GetId({ *BuildFunctionType(functionNode) });
	}

	std::uint32_t SpirvWriter::GetPointerTypeId(const ShaderAst::ExpressionType& type, SpirvStorageClass storageClass) const
	{
		return m_currentState->constantTypeCache.GetId(*m_currentState->constantTypeCache.BuildPointerType(type, storageClass));
	}

	std::uint32_t SpirvWriter::GetTypeId(const ShaderAst::ExpressionType& type) const
	{
		return m_currentState->constantTypeCache.GetId(*m_currentState->constantTypeCache.BuildType(type));
	}

	std::uint32_t SpirvWriter::RegisterConstant(const ShaderAst::ConstantValue& value)
	{
		return m_currentState->constantTypeCache.Register(*m_currentState->constantTypeCache.BuildConstant(value));
	}

	std::uint32_t SpirvWriter::RegisterFunctionType(const ShaderAst::DeclareFunctionStatement& functionNode)
	{
		return m_currentState->constantTypeCache.Register({ *BuildFunctionType(functionNode) });
	}

	std::uint32_t SpirvWriter::RegisterPointerType(ShaderAst::ExpressionType type, SpirvStorageClass storageClass)
	{
		return m_currentState->constantTypeCache.Register(*m_currentState->constantTypeCache.BuildPointerType(type, storageClass));
	}

	std::uint32_t SpirvWriter::RegisterType(ShaderAst::ExpressionType type)
	{
		assert(m_currentState);
		return m_currentState->constantTypeCache.Register(*m_currentState->constantTypeCache.BuildType(type));
	}

	void SpirvWriter::MergeSections(std::vector<std::uint32_t>& output, const SpirvSection& from)
	{
		const std::vector<std::uint32_t>& bytecode = from.GetBytecode();

		std::size_t prevSize = output.size();
		output.resize(prevSize + bytecode.size());
		std::copy(bytecode.begin(), bytecode.end(), output.begin() + prevSize);
	}
}