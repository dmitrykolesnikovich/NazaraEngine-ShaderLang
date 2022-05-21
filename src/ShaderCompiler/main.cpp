#include <NZSL/LangWriter.hpp>
#include <NZSL/ShaderLangErrors.hpp>
#include <NZSL/ShaderLangLexer.hpp>
#include <NZSL/ShaderLangParser.hpp>
#include <NZSL/Ast/AstSerializer.hpp>
#include <NZSL/Ast/SanitizeVisitor.hpp>
#include <cxxopts.hpp>
#include <fmt/color.h>
#include <fmt/format.h>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>

enum class LogFormat
{
	Classic,
	VisualStudio
};

std::vector<std::uint8_t> ReadFileContent(const std::filesystem::path& filePath)
{
	std::ifstream inputFile(filePath, std::ios::in | std::ios::binary);
	if (!inputFile)
		throw std::runtime_error("failed to open " + filePath.generic_u8string());

	inputFile.seekg(0, std::ios::end);

	std::streamsize length = inputFile.tellg();

	inputFile.seekg(0, std::ios::beg);

	std::vector<std::uint8_t> content(length);
	if (length > 0 && !inputFile.read(reinterpret_cast<char*>(&content[0]), length))
		throw std::runtime_error("failed to read " + filePath.generic_u8string());

	return content;
}

std::string ReadSourceFileContent(const std::filesystem::path& filePath)
{
	std::vector<std::uint8_t> content = ReadFileContent(filePath);
	return std::string(reinterpret_cast<const char*>(&content[0]), content.size());
}

void WriteFileContent(std::filesystem::path& filePath, const void* data, std::size_t size)
{
	std::ofstream outputFile(filePath, std::ios::out | std::ios::binary | std::ios::trunc);
	if (!outputFile)
		throw std::runtime_error("failed to open " + filePath.generic_u8string());

	if (outputFile.write(static_cast<const char*>(data), size))
		throw std::runtime_error("failed to write " + filePath.generic_u8string());
}

int main(int argc, char* argv[])
{
	cxxopts::Options options("nzslc", "Tool for validating and compiling NZSL shaders");
	options.add_options()
		("c,compile", "Compile input shader")
		("output-nzsl", "Output shader as NZSL to stdout")
		("header-file", "Generate an includable header file")
		("log-format", "Set log format (classic, vs)", cxxopts::value<std::string>())
		("i,input", "Input file(s)", cxxopts::value<std::string>())
		("o,output", "Output path", cxxopts::value<std::string>()->default_value("."), "path")
		("p,partial", "Allow partial compilation")
		("s,show", "Show informations about the shader (default)")
		("h,help", "Print usage")
	;

	options.parse_positional("input");
	options.positional_help("shader path");

	try
	{
		auto result = options.parse(argc, argv);
		if (result.count("help") > 0)
		{
			fmt::print("{}\n", options.help());
			return EXIT_SUCCESS;
		}

		if (result.count("input") == 0)
		{
			fmt::print(stderr, "no input file\n{}\n", options.help());
			return EXIT_SUCCESS;
		}

		LogFormat logFormat = LogFormat::Classic;
		if (result.count("log-format") != 0)
		{
			const std::string& formatStr = result["log-format"].as<std::string>();
			if (formatStr == "vs")
				logFormat = LogFormat::VisualStudio;
			else if (formatStr != "classic")
			{
				fmt::print(stderr, "{} is not a file\n", formatStr);
				return EXIT_FAILURE;
			}
		}

		std::filesystem::path inputPath = result["input"].as<std::string>();
		if (!std::filesystem::is_regular_file(inputPath))
		{
			fmt::print(stderr, "{} is not a file\n", inputPath.generic_u8string());
			return EXIT_FAILURE;
		}

		try
		{
			nzsl::ShaderAst::ModulePtr shaderModule;
			if (inputPath.extension() == ".nzsl")
			{
				std::string sourceContent = ReadSourceFileContent(inputPath);

				std::vector<nzsl::ShaderLang::Token> tokens = nzsl::ShaderLang::Tokenize(sourceContent, inputPath.generic_u8string());

				shaderModule = nzsl::ShaderLang::Parse(tokens);
			}
			else if (inputPath.extension() == ".nzslb")
			{
				std::vector<std::uint8_t> sourceContent = ReadFileContent(inputPath);
				nzsl::Unserializer unserializer(sourceContent.data(), sourceContent.size());

				shaderModule = nzsl::ShaderAst::UnserializeShader(unserializer);
			}
			else
			{
				fmt::print("{} has unknown extension\n", inputPath.generic_u8string());
				return EXIT_FAILURE;
			}

			if (result.count("compile") > 0)
			{
				nzsl::ShaderAst::SanitizeVisitor::Options sanitizeOptions;
				sanitizeOptions.allowPartialSanitization = result.count("partial") > 0;

				shaderModule = nzsl::ShaderAst::Sanitize(*shaderModule, sanitizeOptions);

				nzsl::Serializer unserializer;
				nzsl::ShaderAst::SerializeShader(unserializer, shaderModule);

				const std::vector<std::uint8_t>& shaderData = unserializer.GetData();

				std::filesystem::path outputPath = inputPath;
				if (result.count("header-file") > 0)
				{
					outputPath.replace_extension(".nzslb.h");

					std::stringstream ss;

					bool first = true;
					for (std::size_t i = 0; i < shaderData.size(); ++i)
					{
						if (!first)
							ss << ',';

						ss << +shaderData[i];

						first = false;
					}

					std::string headerFile = std::move(ss).str();
					WriteFileContent(outputPath, headerFile.data(), headerFile.size());
				}
				else
				{
					outputPath.replace_extension(".nzslb");
					WriteFileContent(outputPath, shaderData.data(), shaderData.size());
				}
			}

			if (result.count("output-nzsl") > 0)
			{
				nzsl::LangWriter nzslWriter;
				fmt::print("{}", nzslWriter.Generate(*shaderModule));
			}

			return EXIT_SUCCESS;
		}
		catch (const nzsl::ShaderLang::Error& error)
		{
			const nzsl::ShaderLang::SourceLocation& errorLocation = error.GetSourceLocation();
			if (errorLocation.IsValid())
			{
				if (logFormat == LogFormat::Classic)
				{
					fmt::print(stderr, (fmt::emphasis::bold | fg(fmt::color::red)), "{}\n", error.what());

					try
					{
						// Retrieve line
						std::string sourceContent = ReadSourceFileContent(*errorLocation.file);

						std::size_t lineStartOffset = 0;
						if (errorLocation.startLine > 1)
						{
							lineStartOffset = sourceContent.find('\n') + 1;
							for (std::size_t i = 0; i < errorLocation.startLine - 2; ++i) //< remember startLine is 1-based
							{
								lineStartOffset = sourceContent.find('\n', lineStartOffset);
								if (lineStartOffset == std::string::npos)
									throw std::runtime_error("file content doesn't match original source");

								++lineStartOffset;
							}
						}
						std::size_t lineEndOffset = sourceContent.find('\n', lineStartOffset);

						std::string errorLine = sourceContent.substr(lineStartOffset, lineEndOffset - lineStartOffset);

						// handle tabs
						std::uint32_t startColumn = errorLocation.startColumn - 1;
						std::size_t startPos = 0;
						while ((startPos = errorLine.find('\t', startPos)) != std::string::npos)
						{
							if (startPos < startColumn)
								startColumn += 3;

							errorLine.replace(startPos, 1, "    ");
							startPos += 4;
						}

						std::size_t columnSize;
						if (errorLocation.startLine == errorLocation.endLine)
							columnSize = errorLocation.endColumn - errorLocation.startColumn + 1;
						else
							columnSize = 1;

						std::string lineStr = std::to_string(errorLocation.startLine);

						fmt::print(stderr, " {} | {}\n", lineStr, errorLine);
						fmt::print(stderr, " {} | {}", std::string(lineStr.size(), ' '), std::string(startColumn, ' '));
						fmt::print(stderr, fg(fmt::color::green), "{}\n", std::string(columnSize, '^'));
					}
					catch (const std::exception& e)
					{
						fmt::print(stderr, "failed to print error line: {}\n", e.what());
					}
				}
				else if (logFormat == LogFormat::VisualStudio)
				{
					// VS requires absolute path
					std::filesystem::path fullPath;
					if (errorLocation.file)
						fullPath = std::filesystem::absolute(*errorLocation.file);

					fmt::print(stderr, "{}({},{}): error {}: {}\n", fullPath.generic_u8string(), errorLocation.startLine, errorLocation.startColumn, ToString(error.GetErrorType()), error.GetErrorMessage());
				}
			}
			else
				fmt::print(stderr, (fmt::emphasis::bold | fg(fmt::color::red)), "{}\n", error.what());

			return EXIT_FAILURE;
		}
	}
	catch (const cxxopts::OptionException& e)
	{
		fmt::print(stderr, "{}\n{}\n", e.what(), options.help());
		return EXIT_FAILURE;
	}
	catch (const std::exception& e)
	{
		fmt::print(stderr, "{}\n", e.what());
		return EXIT_FAILURE;
	}
}