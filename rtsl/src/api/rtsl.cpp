#include "rtslc.h"

#include "artifact/artifact.hpp"
#include "artifact/linker.hpp"
#include "driver/compiler.hpp"

#include <exception>
#include <new>
#include <span>
#include <string_view>
#include <utility>

struct rtsl_module_t {
	rtsl::Artifact artifact;
};

struct rtsl_context_t {
	rtsl::CompilerInstance compiler;
	rtsl_result result{ RTSL_OK, "ok" };
};

struct rtsl_linker_t {
	rtsl_context_t* context = nullptr;
	rtsl::Linker linker;

	explicit rtsl_linker_t(rtsl_context_t& owner)
		: context(&owner), linker(owner.compiler.diagnostics()) {}
};

namespace {

void set_result(rtsl_context context, int code, const char* text) {
	if (context) context->result = rtsl_result{ code, text };
}

rtsl_output_kind to_c_kind(rtsl::ArtifactKind kind) {
	switch (kind) {
	case rtsl::ArtifactKind::object: return RTSL_OUTPUT_OBJECT;
	case rtsl::ArtifactKind::module: return RTSL_OUTPUT_MODULE;
	case rtsl::ArtifactKind::library: return RTSL_OUTPUT_LIBRARY;
	case rtsl::ArtifactKind::program: return RTSL_OUTPUT_PROGRAM;
	}
	return RTSL_OUTPUT_OBJECT;
}

rtsl_diagnostic_severity to_c_severity(rtsl::DiagnosticSeverity severity) {
	switch (severity) {
	case rtsl::DiagnosticSeverity::ignored: return RTSL_DIAG_IGNORED;
	case rtsl::DiagnosticSeverity::note: return RTSL_DIAG_NOTE;
	case rtsl::DiagnosticSeverity::warning: return RTSL_DIAG_WARNING;
	case rtsl::DiagnosticSeverity::error: return RTSL_DIAG_ERROR;
	case rtsl::DiagnosticSeverity::fatal: return RTSL_DIAG_FATAL;
	}
	return RTSL_DIAG_FATAL;
}

rtsl_module link(rtsl_linker linker, bool program) {
	if (!linker) return nullptr;
	try {
		auto artifact = program ? linker->linker.link_program() : linker->linker.link_library();
		if (linker->context->compiler.diagnostics().has_error() || artifact.bytes.empty()) {
			set_result(linker->context, RTSL_ERROR_LINK_FAILED, "link failed");
			return nullptr;
		}
		set_result(linker->context, RTSL_OK, "ok");
		return new (std::nothrow) rtsl_module_t{ .artifact = std::move(artifact) };
	} catch (...) {
		set_result(linker->context, RTSL_ERROR_INTERNAL, "internal linker error");
		return nullptr;
	}
}

} // namespace

extern "C" {

uint32_t rtslGetVersionMajor(void) { return rtsl::ArtifactVersionMajor; }
uint32_t rtslGetVersionMinor(void) { return rtsl::ArtifactVersionMinor; }

rtsl_context rtslCreateContext(void) {
	return new (std::nothrow) rtsl_context_t{};
}

rtsl_result rtslCtxGetResult(rtsl_context context) {
	return context ? context->result : rtsl_result{ RTSL_ERROR_INVALID_ARGUMENT, "null context" };
}

size_t rtslCtxGetDiagnosticCount(rtsl_context context) {
	return context ? context->compiler.diagnostics().diagnostics().size() : 0;
}

rtsl_diagnostic rtslCtxGetDiagnostic(rtsl_context context, size_t index) {
	if (!context || index >= context->compiler.diagnostics().diagnostics().size()) return {};
	const auto& diagnostic = context->compiler.diagnostics().diagnostics()[index];
	return rtsl_diagnostic{
		.code = diagnostic.code,
		.severity = to_c_severity(diagnostic.severity),
		.source_name = diagnostic.source_name.c_str(),
		.offset = diagnostic.location.offset,
		.line = diagnostic.location.line,
		.column = diagnostic.location.column,
		.text = diagnostic.message.c_str(),
	};
}

void rtslDestroyContext(rtsl_context context) {
	delete context;
}

rtsl_module rtslCompileSource(rtsl_context context, const char* source, size_t source_size, const char* source_name) {
	if (!context || (!source && source_size != 0)) {
		set_result(context, RTSL_ERROR_INVALID_ARGUMENT, "invalid compile arguments");
		return nullptr;
	}
	try {
		rtsl::CompilerInvocation invocation{ .source_name = source_name ? source_name : "<memory>" };
		auto artifact = context->compiler.compile_source(std::string_view{ source ? source : "", source_size }, std::move(invocation));
		if (context->compiler.diagnostics().has_error() || artifact.bytes.empty()) {
			set_result(context, RTSL_ERROR_COMPILE_FAILED, "compile failed");
			return nullptr;
		}
		auto* module = new (std::nothrow) rtsl_module_t{ .artifact = std::move(artifact) };
		if (!module) set_result(context, RTSL_ERROR_INTERNAL, "allocation failed");
		else set_result(context, RTSL_OK, "ok");
		return module;
	} catch (...) {
		set_result(context, RTSL_ERROR_INTERNAL, "internal compiler error");
		return nullptr;
	}
}

rtsl_module rtslLoadModule(rtsl_context context, const uint8_t* data, size_t size) {
	if (!context || (!data && size != 0)) {
		set_result(context, RTSL_ERROR_INVALID_ARGUMENT, "invalid load arguments");
		return nullptr;
	}
	try {
		rtsl::Artifact artifact;
		if (!rtsl::read_artifact(std::span<const rtsl::u08>{ data, size }, artifact, &context->compiler.diagnostics())) {
			set_result(context, RTSL_ERROR_ARTIFACT_FAILED, "failed to load artifact");
			return nullptr;
		}
		auto* module = new (std::nothrow) rtsl_module_t{ .artifact = std::move(artifact) };
		if (!module) set_result(context, RTSL_ERROR_INTERNAL, "allocation failed");
		else set_result(context, RTSL_OK, "ok");
		return module;
	} catch (...) {
		set_result(context, RTSL_ERROR_INTERNAL, "internal error while loading artifact");
		return nullptr;
	}
}

rtsl_blob rtslModuleGetBytecode(rtsl_module module) {
	if (!module) return {};
	return rtsl_blob{ module->artifact.bytes.data(), module->artifact.bytes.size() };
}

rtsl_output_kind rtslModuleGetKind(rtsl_module module) {
	return module ? to_c_kind(module->artifact.kind) : RTSL_OUTPUT_OBJECT;
}

void rtslDestroyModule(rtsl_module module) {
	delete module;
}

rtsl_linker rtslCreateLinker(rtsl_context context) {
	if (!context) return nullptr;
	auto* linker = new (std::nothrow) rtsl_linker_t{ *context };
	if (!linker) set_result(context, RTSL_ERROR_INTERNAL, "failed to create linker");
	return linker;
}

int rtslLinkerAddModule(rtsl_linker linker, rtsl_module module) {
	return linker && module && linker->linker.add_artifact(module->artifact) ? 1 : 0;
}

int rtslLinkerAddBlob(rtsl_linker linker, const uint8_t* data, size_t size) {
	if (!linker || (!data && size != 0)) return 0;
	return linker->linker.add_artifact_bytes(std::span<const rtsl::u08>{ data, size }) ? 1 : 0;
}

rtsl_module rtslLinkLibrary(rtsl_linker linker) { return link(linker, false); }
rtsl_module rtslLinkProgram(rtsl_linker linker) { return link(linker, true); }

void rtslDestroyLinker(rtsl_linker linker) {
	delete linker;
}

} // extern "C"
