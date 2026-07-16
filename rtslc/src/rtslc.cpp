#include "artifact/artifact.hpp"
#include "artifact/linker.hpp"
#include "driver/compiler.hpp"
#include "ir/text_rtir.hpp"
#include "support/basic_diagnostics.hpp"
#include "support/io.hpp"

#include <CLI/CLI.hpp>

#include <filesystem>
#include <span>
#include <iostream>
#include <string>
#include <system_error>
#include <vector>

namespace {

using rtsl::as_text;
using rtsl::read_file;
using rtsl::with_extension;
using rtsl::write_file;
namespace fs = std::filesystem;

void print_engine_diagnostics(const rtsl::DiagnosticEngine& diagnostics) {
	diagnostics.render(std::cerr);
}

int compile_mode(const fs::path& input_path, const fs::path& output, std::span<const std::string> include_dirs, bool verbose) {
	std::error_code input_error;
	if (!fs::exists(input_path, input_error) || input_error) {
		std::cerr << "rtslc: failed to read " << input_path << '\n';
		return 1;
	}
	std::error_code size_error;
	const auto input_size = fs::file_size(input_path, size_error);
	if (size_error) {
		std::cerr << "rtslc: failed to read " << input_path << '\n';
		return 1;
	}
	const auto source = read_file(input_path);
	if (source.empty() && input_size != 0) {
		std::cerr << "rtslc: failed to read " << input_path << '\n';
		return 1;
	}

	if (verbose) {
		std::cerr << "rtslc: compiling " << input_path << " -> " << output << '\n';
		for (const auto& dir : include_dirs) {
			std::cerr << "rtslc:   -I " << dir << '\n';
		}
	}

	rtsl::CompilerInstance compiler;
	rtsl::CompilerInvocation invocation{
		.source_name = input_path.string(),
		.import_paths = std::vector<std::string>{ include_dirs.begin(), include_dirs.end() },
	};
	rtsl::Artifact artifact;
	compiler.compile_source_to(artifact, as_text(source), std::move(invocation));

	if (compiler.diagnostics().has_error() || artifact.bytes.empty()) {
		print_engine_diagnostics(compiler.diagnostics());
		return 1;
	}
	if (!write_file(output, artifact.bytes)) {
		std::cerr << "rtslc: failed to write " << output << '\n';
		return 1;
	}

	const rtsl::Artifact module_artifact = rtsl::extract_module_interface(artifact);
	if (!module_artifact.bytes.empty()) {
		const auto module_path = with_extension(output, rtsl::artifact_extension(rtsl::ArtifactKind::module));
		if (!write_file(module_path, module_artifact.bytes)) {
			std::cerr << "rtslc: failed to write " << module_path << '\n';
			return 1;
		}
	}
	return 0;
}

int link_mode(std::span<const std::string> inputs, const fs::path& output, bool produce_program, bool verbose) {
	if (inputs.empty()) {
		std::cerr << "rtslc: no inputs provided\n";
		return 1;
	}
	if (verbose) {
		std::cerr << "rtslc: linking " << inputs.size() << " inputs -> " << output << '\n';
	}

	rtsl::DiagnosticEngine diagnostics;
	rtsl::Linker linker{ diagnostics };
	for (const auto& input_path : inputs) {
		const auto bytes = read_file(input_path);
		if (bytes.empty()) {
			std::cerr << "rtslc: failed to read " << input_path << '\n';
			return 1;
		}
		if (!linker.add_artifact_bytes(bytes)) {
			print_engine_diagnostics(diagnostics);
			return 1;
		}
	}

	rtsl::Artifact linked = produce_program ? linker.link_program() : linker.link_library();
	if (diagnostics.has_error() || linked.bytes.empty()) {
		print_engine_diagnostics(diagnostics);
		return 1;
	}
	if (!write_file(output, linked.bytes)) {
		std::cerr << "rtslc: failed to write " << output << '\n';
		return 1;
	}

	if (!produce_program) {
		const rtsl::Artifact module_artifact = rtsl::extract_module_interface(linked);
		if (!module_artifact.bytes.empty()) {
			const auto module_path = with_extension(output, rtsl::artifact_extension(rtsl::ArtifactKind::module));
			if (!write_file(module_path, module_artifact.bytes)) {
				std::cerr << "rtslc: failed to write " << module_path << '\n';
				return 1;
			}
		}
	}
	return 0;
}

int dump_mode(const fs::path& input_path) {
	const auto bytes = read_file(input_path);
	rtsl::Artifact artifact;
	rtsl::DiagnosticEngine diagnostics;
	if (bytes.empty() || !rtsl::read_artifact(bytes, artifact, &diagnostics)) {
		std::cerr << "rtslc: failed to read artifact " << input_path << '\n';
		print_engine_diagnostics(diagnostics);
		return 1;
	}
	std::cout << rtsl::disassemble_artifact(artifact);
	return 0;
}

} // namespace

int main(int argc, char** argv) {
	CLI::App app{ "RTSL compiler driver" };
	app.require_subcommand(1, 1);
	app.set_version_flag("--version", "rtslc 0.1");

	bool verbose = false;
	app.add_flag("-v,--verbose", verbose, "Print extra driver diagnostics");

	auto compile = app.add_subcommand("compile", "Compile source to an object");
	std::string compile_input;
	std::string compile_output;
	std::vector<std::string> compile_include_dirs;
	compile->add_option("input", compile_input, "Input .rtsl file")->required();
	compile->add_option("-o,--output", compile_output, "Output .rtslo file")->required();
	compile->add_option("-I,--include-dir", compile_include_dirs,
		"Search directory for imported .rtslm module interfaces (repeatable)");

	auto link_program = app.add_subcommand("link-program", "Link objects/libraries into a program");
	std::vector<std::string> link_program_inputs;
	std::string link_program_output;
	link_program->add_option("inputs", link_program_inputs, "Input .rtslo/.rtsll files")->required();
	link_program->add_option("-o,--output", link_program_output, "Output .rtslp file")->required();

	auto link_library = app.add_subcommand("link-library", "Link objects/libraries into a library");
	std::vector<std::string> link_library_inputs;
	std::string link_library_output;
	link_library->add_option("inputs", link_library_inputs, "Input .rtslo/.rtsll files")->required();
	link_library->add_option("-o,--output", link_library_output, "Output .rtsll file")->required();

	auto dump = app.add_subcommand("dump", "Disassemble any RTSL artifact");
	std::string dump_input;
	dump->add_option("input", dump_input, "Input artifact")->required();

	if (argc <= 1) {
		std::cout << app.help() << '\n';
		return 1;
	}

	CLI11_PARSE(app, argc, argv);

	if (*compile)
		return compile_mode(compile_input, compile_output, compile_include_dirs, verbose);
	if (*link_program)
		return link_mode(link_program_inputs, link_program_output, true, verbose);
	if (*link_library)
		return link_mode(link_library_inputs, link_library_output, false, verbose);
	if (*dump)
		return dump_mode(dump_input);
	std::cout << app.help() << '\n';
	return 1;
}
