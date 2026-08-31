#include <rtsl/Frontend/CompilerInstance.hpp>
#include <rtsl/Serialization/Artifact.hpp>
#include <rtsl/Serialization/DebugArtifact.hpp>

#include <CLI/CLI.hpp>

#include <fstream>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <string>

namespace {

rtsl::debug::Artifact makeDebugArtifact(const rtsl::CompilerInstance& compiler,
	const std::string& compilation_identity) {
	rtsl::debug::Artifact artifact;
	artifact.compilation_identity = compilation_identity;
	artifact.compiler_identity = "rtslc";
	artifact.target_identity = "rtir";
	const rtsl::SourceManager& sources = compiler.getSourceManager();
	for (std::size_t index = 1; index <= sources.getFileCount(); ++index) {
		const rtsl::FileID file = static_cast<rtsl::FileID>(index);
		const std::string_view text = sources.getBuffer(file);
		rtsl::debug::SourceFile source;
		source.logical_name = sources.getName(file);
		source.text = text;
		source.line_starts.push_back(0);
		for (std::uint32_t offset = 0; offset < text.size(); ++offset)
			if (text[offset] == '\n' && offset + 1 < text.size()) source.line_starts.push_back(offset + 1);
		artifact.source_files.push_back(std::move(source));
	}
	return artifact;
}

bool writeDebugArtifact(const std::string& path, const rtsl::debug::Artifact& artifact) {
	const rtsl::debug::WriteResult encoded = rtsl::debug::ArtifactWriter{}.write(artifact);
	if (!encoded) {
		std::cerr << path << ": failed to encode debug artifact: " << encoded.error->message << '\n';
		return false;
	}
	std::ofstream output(path, std::ios::binary);
	output.write(reinterpret_cast<const char*>(encoded.bytes.data()), static_cast<std::streamsize>(encoded.bytes.size()));
	if (!output) {
		std::cerr << path << ": failed to write debug artifact\n";
		return false;
	}
	return true;
}

} // namespace

int main(int ArgumentCount, char** Arguments) {
	CLI::App Application{"RTSL compiler frontend"};
	std::string InputPath;
	std::string ModuleName;
	std::string OutputPath;
	std::string DebugOutputPath;
	bool EmitProgram{};
	Application.add_option("input", InputPath, "RTSL source file")->required()->check(CLI::ExistingFile);
	Application.add_option("-m,--module", ModuleName, "Module name");
	Application.add_option("-o,--output", OutputPath, "Output artifact path");
	Application.add_option("--debug-output", DebugOutputPath, "Output .rtsld debug artifact path");
	Application.add_flag("--emit-program", EmitProgram, "Compile and link a program artifact");
	CLI11_PARSE(Application, ArgumentCount, Arguments);

	std::ifstream Input(InputPath, std::ios::binary);
	std::string Source(std::istreambuf_iterator<char>(Input), {});
	rtsl::CompilerInvocation Invocation;
	Invocation.setInputName(InputPath);
	Invocation.setInputBuffer(std::move(Source));
	Invocation.setModuleName(std::move(ModuleName));
	rtsl::CompilerInstance Compiler;
	Compiler.setInvocation(std::move(Invocation));
	if (!OutputPath.empty() || !DebugOutputPath.empty()) EmitProgram = true;
	if (EmitProgram && OutputPath.empty() && DebugOutputPath.empty()) {
		std::cerr << "rtslc: --emit-program requires -o/--output\n";
		return 1;
	}
	bool Success{};
	rtsl::LinkedIRCompilation Compilation;
	if (EmitProgram) {
		Compilation = Compiler.compileToLinkedRTIR();
		Success = Compilation.succeeded();
	} else Success = Compiler.execute();
	for (const auto& Diagnostic : Compiler.getDiagnostics().diagnostics()) {
		const rtsl::PresumedLoc Location = Compiler.getSourceManager().getPresumedLoc(Diagnostic.Range.Begin);
		const char* Level = Diagnostic.Level == rtsl::DiagnosticLevel::diagnostic_error ? "error" : "warning";
		if (Location.isValid()) {
			std::cerr << Location.Filename << '(' << Location.Line << ',' << Location.Column << "): "
				<< Level << " RTSL" << std::setw(4) << std::setfill('0') << Diagnostic.Code << std::setfill(' ')
				<< ": " << Diagnostic.Message << '\n';
		} else {
			std::cerr << InputPath << ": " << Level << " RTSL" << std::setw(4) << std::setfill('0')
				<< Diagnostic.Code << std::setfill(' ') << ": " << Diagnostic.Message << '\n';
		}
	}
	if (EmitProgram) {
		for (const auto& Diagnostic : Compilation.CodeGeneration.Diagnostics)
			std::cerr << InputPath << ": code generation: " << Diagnostic.Message << '\n';
		for (const auto& Diagnostic : Compilation.CodeGeneration.Verification.issues())
			std::cerr << InputPath << ": code generation verification: " << Diagnostic.message << '\n';
		for (const auto& Diagnostic : Compilation.Link.Diagnostics)
			std::cerr << InputPath << ": link: " << Diagnostic.Message << '\n';
		for (const auto& Diagnostic : Compilation.Link.Verification.issues())
			std::cerr << InputPath << ": verification: " << Diagnostic.message << '\n';
		if (Success) {
			if (!OutputPath.empty()) {
				rtsl::Artifact Artifact{.kind = rtsl::ArtifactKind::artifact_program, .module = std::move(Compilation.Link.Module)};
				auto Encoded = rtsl::ArtifactWriter{}.write(Artifact);
				if (!Encoded) {
					std::cerr << InputPath << ": artifact: " << Encoded.error->message << '\n';
					Success = false;
				} else {
					std::ofstream Output(OutputPath, std::ios::binary);
					Output.write(reinterpret_cast<const char*>(Encoded.bytes.data()), static_cast<std::streamsize>(Encoded.bytes.size()));
					if (!Output) {
						std::cerr << OutputPath << ": failed to write program artifact\n";
						Success = false;
					}
				}
			}
			if (Success && !DebugOutputPath.empty())
				Success = writeDebugArtifact(DebugOutputPath, makeDebugArtifact(Compiler, InputPath));
		}
	}
	return Success ? 0 : 1;
}
