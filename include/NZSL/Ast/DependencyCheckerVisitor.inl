// Copyright (C) 2022 Jérôme "Lynix" Leclercq (lynix680@gmail.com)
// This file is part of the "Nazara Shading Language" project
// For conditions of distribution and use, see copyright notice in Config.hpp

#include <NZSL/Ast/DependencyCheckerVisitor.hpp>

namespace nzsl::ShaderAst
{
	inline auto DependencyCheckerVisitor::GetUsage() const -> const UsageSet&
	{
		return m_resolvedUsage;
	}

	inline void DependencyCheckerVisitor::MarkFunctionAsUsed(std::size_t funcIndex)
	{
		m_globalUsage.usedFunctions.UnboundedSet(funcIndex);
	}

	inline void DependencyCheckerVisitor::MarkStructAsUsed(std::size_t structIndex)
	{
		m_globalUsage.usedStructs.UnboundedSet(structIndex);
	}

	inline void DependencyCheckerVisitor::Register(Statement& statement)
	{
		Config defaultConfig;
		return Register(statement, defaultConfig);
	}

	inline void DependencyCheckerVisitor::Resolve(bool allowUnknownId)
	{
		Resolve(m_globalUsage, allowUnknownId);
	}
}
