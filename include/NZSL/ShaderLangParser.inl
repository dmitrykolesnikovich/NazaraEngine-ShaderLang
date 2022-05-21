// Copyright (C) 2022 Jérôme "Lynix" Leclercq (lynix680@gmail.com)
// This file is part of the "Nazara Shading Language" project
// For conditions of distribution and use, see copyright notice in Config.hpp

#include <NZSL/ShaderLangParser.hpp>

namespace nzsl::ShaderLang
{
	inline Parser::Parser() :
	m_context(nullptr)
	{
	}

	inline ShaderAst::ModulePtr Parse(const std::string_view& source, const std::string& filePath)
	{
		return Parse(Tokenize(source, filePath));
	}

	inline ShaderAst::ModulePtr Parse(const std::vector<Token>& tokens)
	{
		Parser parser;
		return parser.Parse(tokens);
	}
}
