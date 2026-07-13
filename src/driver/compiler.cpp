#include "driver/compiler.hpp"

#include "artifact/linker.hpp"
#include "frontend/lexer.hpp"
#include "frontend/parser.hpp"
#include "frontend/preprocessor.hpp"
#include "ir/ir.hpp"
#include "sema/sema.hpp"
#include "support/io.hpp"

#include <filesystem>
#include <format>
#include <unordered_set>
#include <utility>

namespace rtsl {

namespace fs = std::filesystem;

struct LoadedImport {
	std::string name;
	Artifact artifact;
};

std::vector<fs::path> import_search_roots(const CompilerInvocation& invocation) {
	std::vector<fs::path> roots;
	if (!invocation.source_name.empty()) {
		roots.push_back(fs::path(invocation.source_name).parent_path());
	}
	for (const auto& path : invocation.import_paths) {
		roots.push_back(path);
	}
	return roots;
}

// Look for `name` with the given extension in every import search root.
static fs::path resolve_in_roots(const CompilerInvocation& invocation, std::string_view name, std::string_view extension) {
	for (const auto& root : import_search_roots(invocation)) {
		const auto candidate = with_extension(root / fs::path(name), extension);
		if (fs::exists(candidate)) {
			return candidate;
		}
	}
	return {};
}

fs::path resolve_import(const CompilerInvocation& invocation, std::string_view name) {
	return resolve_in_roots(invocation, name, artifact_extension(ArtifactKind::module));
}

// Autobuild fallback: when the .rtslm sidecar isn't on disk, look for the
// original `.rtsl` source next to any of the import search roots. If found,
// the caller compiles it and extracts a module interface on the fly.
fs::path resolve_import_source(const CompilerInvocation& invocation, std::string_view name) {
	return resolve_in_roots(invocation, name, source_extension());
}

bool CompilerInstance::load_or_build_import(const CompilerInvocation& invocation, std::string_view name,
	std::vector<u08>& bytes, std::unordered_set<std::string>& active_builds) {
	// Prefer an existing .rtslm sidecar: it's already the interface form and
	// carries no body work to redo.
	if (const auto module_path = resolve_import(invocation, name); !module_path.empty()) {
		bytes = read_file(module_path);
		return !bytes.empty();
	}

	// Autobuild: compile the sibling .rtsl source in-memory and take its
	// module interface. Diagnostics flow into this instance so nested errors
	// surface at the top level. The active-build set catches cycles: if we're
	// already partway through compiling this same source, bail out with an
	// error rather than recursing forever.
	const auto source_path = resolve_import_source(invocation, name);
	if (source_path.empty()) {
		bytes.clear();
		return true; // nothing found — caller will diagnose
	}
	const auto canonical = fs::weakly_canonical(source_path).string();
	if (!active_builds.insert(canonical).second) {
		diagnostics_.report(DiagnosticCode::compiler_validation_failed, DiagnosticSeverity::error, {}, invocation.source_name,
			std::format("import cycle detected while building '{}'", name));
		return false;
	}

	const auto imported_source = read_file(source_path);
	Artifact scratch;
	CompilerInvocation child{
		.source_name = source_path.string(),
		.defines = invocation.defines,
		.import_paths = invocation.import_paths,
	};
	compile_source_to_impl(scratch, as_text(imported_source), std::move(child), active_builds);
	active_builds.erase(canonical);
	if (diagnostics_.has_error()) {
		return false;
	}

	bytes = extract_module_interface(scratch).bytes;
	return true;
}

Artifact CompilerInstance::compile_source(std::string_view source, CompilerInvocation invocation) {
	Artifact artifact;
	compile_source_to(artifact, source, std::move(invocation));
	return artifact;
}

void CompilerInstance::compile_source_to(Artifact& artifact, std::string_view source, CompilerInvocation invocation) {
	// Top-level entry: seed a fresh active-build set for cycle detection and
	// clear diagnostics from prior compiles. Recursive autobuilds share the
	// set through compile_source_to_impl but skip both those steps.
	std::unordered_set<std::string> active_builds;
	diagnostics_.clear();
	compile_source_to_impl(artifact, source, std::move(invocation), active_builds);
}

void CompilerInstance::compile_source_to_impl(Artifact& artifact, std::string_view source,
	CompilerInvocation invocation, std::unordered_set<std::string>& active_builds) {
	artifact = Artifact{ .kind = ArtifactKind::object };
	const auto preprocessed = preprocess_source(source, invocation.defines);
	const auto invocation_source_name = invocation.source_name;
	const auto file_id = sources_.add_buffer(invocation_source_name, std::move(preprocessed));

	Lexer lexer(sources_, diagnostics_, file_id);
	const auto tokens = lexer.lex();

	Parser parser(sources_, diagnostics_, file_id, tokens);
	auto ast = parser.parse_translation_unit();

	std::vector<LoadedImport> imported_modules;
	for (const auto& import_name : ast.imports) {
		std::vector<u08> bytes;
		if (!load_or_build_import(invocation, import_name, bytes, active_builds)) {
			// Hard failure (cycle or nested compile error) — diagnostic already
			// emitted by load_or_build_import; move on so we can surface any
			// other errors in the same pass.
			continue;
		}
		if (bytes.empty()) {
			diagnostics_.report(DiagnosticCode::compiler_no_input, DiagnosticSeverity::error, sources_.location_at(file_id, 0), invocation_source_name,
								std::format("failed to resolve import '{}'", import_name));
			continue;
		}
		Artifact imported;
		if (!read_artifact(bytes, imported, &diagnostics_)) {
			diagnostics_.report(DiagnosticCode::compiler_file_read_failed, DiagnosticSeverity::error, sources_.location_at(file_id, 0), invocation_source_name,
								std::format("failed to load import '{}'", import_name));
			continue;
		}
		imported_modules.push_back(LoadedImport{ .name = import_name, .artifact = std::move(imported) });
	}

	Sema sema(sources_, diagnostics_);
	auto semantic_module = sema.analyze(ast);
	for (const auto& imported : imported_modules) {
		semantic_module.imported_exports.insert(semantic_module.imported_exports.end(), imported.artifact.exports.begin(), imported.artifact.exports.end());
	}
	for (const auto& decl : ast.declarations) {
		if (decl.kind != DeclKind::import || !decl.exported) {
			continue;
		}
		for (const auto& imported : imported_modules) {
			if (imported.name != decl.name) {
				continue;
			}
			semantic_module.exports.insert(semantic_module.exports.end(), imported.artifact.exports.begin(), imported.artifact.exports.end());
			break;
		}
	}
	if (diagnostics_.has_error()) {
		return;
	}
	auto ir = lower_to_ir(semantic_module, &diagnostics_);

	if (!diagnostics_.has_error() && verify_ir(ir, &diagnostics_)) {
		artifact.bytes = write_artifact(ArtifactKind::object, ir);
		artifact.module = ir;
		artifact.strings.clear();
		artifact.structs = ir.structs;
		artifact.imports = ir.imports;
		artifact.imported_exports = ir.imported_exports;
		artifact.exports = ir.exports;
		artifact.uniforms = ir.uniforms;
		artifact.stage_interfaces = ir.stage_interfaces;
		artifact.entries.clear();
		for (const auto& function : ir.functions) {
			if (function.stage == StageKind::none) {
				continue;
			}
			artifact.entries.push_back(Artifact::EntryPoint{
				.name = std::string(stage_entry_name(function.stage)),
				.mangled_name = std::string(stage_entry_name(function.stage)),
				.stage = function.stage,
				.function_id = function.result_id,
			});
		}
		artifact.debug_bytes = write_debug_artifact(ir);
	}
}

} // namespace rtsl
