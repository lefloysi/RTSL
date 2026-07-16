#include "rtsl.h"

#include "artifact/artifact.hpp"
#include "driver/compiler.hpp"
#include "ir/ir.hpp"
#include "artifact/linker.hpp"
#include "sema/uniform_lowering.hpp"

#include <cstring>
#include <exception>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

rtsl_uniform_kind uniform_kind_from_type(const rtsl::IRModule& module, std::string_view uniform_type, rtsl::IRId type_id) {
	switch (rtsl::resource_binding_kind(uniform_type)) {
	case rtsl::ResourceBindingKind::uniform_buffer:
		return RTSL_UNIFORM_KIND_UNIFORM_BUFFER;
	case rtsl::ResourceBindingKind::storage_buffer:
		return RTSL_UNIFORM_KIND_STORAGE_BUFFER;
	case rtsl::ResourceBindingKind::sampler:
	case rtsl::ResourceBindingKind::sampled_image:
	case rtsl::ResourceBindingKind::image:
	case rtsl::ResourceBindingKind::none:
		break;
	}
	for (const auto& inst : module.type_constant_pool) {
		if (inst.result_id != type_id) {
			continue;
		}
		switch (inst.op) {
		case rtsl::IROp::TypeSampler:
			return RTSL_UNIFORM_KIND_SAMPLER;
		case rtsl::IROp::TypeImage:
			return RTSL_UNIFORM_KIND_IMAGE;
		case rtsl::IROp::TypeSampledImage:
			return RTSL_UNIFORM_KIND_SAMPLED_IMAGE;
		default:
			return RTSL_UNIFORM_KIND_UNIFORM_BUFFER;
		}
	}
	return RTSL_UNIFORM_KIND_UNIFORM_BUFFER;
}


struct rtsl_module_t {
	rtsl::Artifact artifact;

	// Reflection views cached on first query. All string pointers live in the
	// artifact and stay valid for the module's lifetime.
	std::vector<std::string> owned_uniform_names;
	std::vector<rtsl_uniform_info> uniform_views;
	std::vector<rtsl_stage_variable> stage_views;
	std::vector<rtsl_entry_info> entry_views;
	bool reflection_built = false;

	void ensure_reflection() {
		if (reflection_built) {
			return;
		}
		reflection_built = true;

		uniform_views.reserve(artifact.uniforms.size());
		owned_uniform_names.reserve(artifact.uniforms.size());
		for (const auto& uniform : artifact.uniforms) {
			owned_uniform_names.push_back(uniform.scope_name.empty() ? uniform.name : uniform.scope_name + "." + uniform.name);
			uniform_views.push_back(rtsl_uniform_info{
				.qualified_name = owned_uniform_names.back().c_str(),
				.type_name = uniform.type.c_str(),
				.group = uniform.set,
				.member = uniform.member,
				.access = static_cast<rtsl_access>(uniform.access),
				.kind = uniform_kind_from_type(artifact.module, uniform.type, uniform.type_id),
			});
		}

		// Host reflection exposes only the target-facing input and output
		// interfaces. Varyings are the vertex-to-fragment contract used by a
		// backend when it emits both stages; they are not a host-visible surface.
		for (const auto& interface : artifact.stage_interfaces) {
			if (interface.role == rtsl::StageRole::varying) {
				continue;
			}
			for (const auto& field : interface.fields) {
				stage_views.push_back(rtsl_stage_variable{
					.role = static_cast<rtsl_stage_role>(interface.role),
					.payload_type = interface.type_name.c_str(),
					.name = field.name.c_str(),
					.interpolation = static_cast<rtsl_interpolation>(field.interpolation),
					.placement = static_cast<rtsl_stage_field_placement>(field.placement),
					.location = field.location,
				});
			}
		}

		for (const auto& entry : artifact.entries) {
			entry_views.push_back(rtsl_entry_info{
				.name = entry.name.c_str(),
				.stage = entry.stage.c_str(),
			});
		}
	}
};

struct rtsl_context_t {
	rtsl::CompilerInstance compiler;
	rtsl_result result{ RTSL_OK, "ok" };
};

struct rtsl_linker_t {
	rtsl_context_t* ctx = nullptr;
	rtsl::Linker linker;

	explicit rtsl_linker_t(rtsl_context_t* context)
		: ctx(context), linker(context->compiler.diagnostics()) {}
};

static void set_result(rtsl_context ctx, int code, const char* text) {
	if (ctx) {
		ctx->result = rtsl_result{ code, text };
	}
}

static rtsl_output_kind to_c_kind(rtsl::ArtifactKind kind) {
	switch (kind) {
	case rtsl::ArtifactKind::object: return RTSL_OUTPUT_OBJECT;
	case rtsl::ArtifactKind::module: return RTSL_OUTPUT_MODULE;
	case rtsl::ArtifactKind::library: return RTSL_OUTPUT_LIBRARY;
	case rtsl::ArtifactKind::program: return RTSL_OUTPUT_PROGRAM;
	}
	std::unreachable();
}

static rtsl_diagnostic_severity to_c_severity(rtsl::DiagnosticSeverity severity) {
	switch (severity) {
	case rtsl::DiagnosticSeverity::ignored: return RTSL_DIAG_IGNORED;
	case rtsl::DiagnosticSeverity::note: return RTSL_DIAG_NOTE;
	case rtsl::DiagnosticSeverity::warning: return RTSL_DIAG_WARNING;
	case rtsl::DiagnosticSeverity::error: return RTSL_DIAG_ERROR;
	case rtsl::DiagnosticSeverity::fatal: return RTSL_DIAG_FATAL;
	}
	std::unreachable();
}

extern "C" {

uint32_t rtslGetVersionMajor(void) {
	return rtsl::ArtifactVersionMajor;
}

uint32_t rtslGetVersionMinor(void) {
	return rtsl::ArtifactVersionMinor;
}

rtsl_context rtslCreateContext(void) {
	return new (std::nothrow) rtsl_context_t();
}

rtsl_result rtslCtxGetResult(rtsl_context ctx) {
	return ctx ? ctx->result : rtsl_result{ RTSL_ERROR_INVALID_ARGUMENT, "null context" };
}

size_t rtslCtxGetDiagnosticCount(rtsl_context ctx) {
	if (!ctx) {
		return 0;
	}
	return ctx->compiler.diagnostics().diagnostics().size();
}

rtsl_diagnostic rtslCtxGetDiagnostic(rtsl_context ctx, size_t index) {
	if (!ctx || index >= ctx->compiler.diagnostics().diagnostics().size()) {
		return {};
	}
	const auto& diagnostic = ctx->compiler.diagnostics().diagnostics()[index];
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

void rtslDestroyContext(rtsl_context ctx) {
	delete ctx;
}

rtsl_module rtslCompileSource(rtsl_context ctx, const char* source, size_t source_size, const char* source_name) {
	if (!ctx || (!source && source_size != 0)) {
		set_result(ctx, RTSL_ERROR_INVALID_ARGUMENT, "invalid compile arguments");
		return nullptr;
	}

	try {
		rtsl::CompilerInvocation invocation;
		invocation.source_name = source_name ? source_name : "<memory>";
		rtsl::Artifact artifact;
		ctx->compiler.compile_source_to(artifact, std::string_view(source ? source : "", source_size), std::move(invocation));
		if (ctx->compiler.diagnostics().has_error() || artifact.bytes.empty()) {
			set_result(ctx, RTSL_ERROR_COMPILE_FAILED, "compile failed");
			return nullptr;
		}
		set_result(ctx, RTSL_OK, "ok");
		auto* module = new (std::nothrow) rtsl_module_t{ .artifact = std::move(artifact) };
		if (!module) {
			set_result(ctx, RTSL_ERROR_INTERNAL, "allocation failed");
		}
		return module;
	} catch (...) {
		set_result(ctx, RTSL_ERROR_INTERNAL, "internal compiler error");
		return nullptr;
	}
}

rtsl_blob rtslModuleGetBytecode(rtsl_module module) {
	if (!module) {
		return {};
	}
	return rtsl_blob{ module->artifact.bytes.data(), module->artifact.bytes.size() };
}

rtsl_output_kind rtslModuleGetKind(rtsl_module module) {
	return module ? to_c_kind(module->artifact.kind) : RTSL_OUTPUT_OBJECT;
}

void rtslDestroyModule(rtsl_module module) {
	delete module;
}

rtsl_module rtslLoadModule(rtsl_context ctx, const uint8_t* data, size_t size) {
	if (!ctx || (!data && size != 0)) {
		set_result(ctx, RTSL_ERROR_INVALID_ARGUMENT, "invalid load arguments");
		return nullptr;
	}
	try {
		rtsl::Artifact artifact;
		if (!rtsl::read_artifact(std::span<const rtsl::u08>(data, size), artifact, &ctx->compiler.diagnostics())) {
			set_result(ctx, RTSL_ERROR_ARTIFACT_FAILED, "failed to load artifact");
			return nullptr;
		}
		set_result(ctx, RTSL_OK, "ok");
		auto* module = new (std::nothrow) rtsl_module_t{ .artifact = std::move(artifact) };
		if (!module) {
			set_result(ctx, RTSL_ERROR_INTERNAL, "allocation failed");
		}
		return module;
	} catch (...) {
		set_result(ctx, RTSL_ERROR_INTERNAL, "internal error while loading artifact");
		return nullptr;
	}
}

rtsl_module rtslLoadModuleFromBytes(const uint8_t* data, size_t size) {
	if (!data && size != 0) {
		return nullptr;
	}
	try {
		rtsl::Artifact artifact;
		if (!rtsl::read_artifact(std::span<const rtsl::u08>(data, size), artifact, nullptr)) {
			return nullptr;
		}
		return new (std::nothrow) rtsl_module_t{ .artifact = std::move(artifact) };
	} catch (...) {
		return nullptr;
	}
}

size_t rtslModuleGetUniformCount(rtsl_module module) {
	if (!module) {
		return 0;
	}
	module->ensure_reflection();
	return module->uniform_views.size();
}

int rtslModuleGetUniform(rtsl_module module, size_t index, rtsl_uniform_info* out_info) {
	if (!module || !out_info) {
		return 0;
	}
	module->ensure_reflection();
	if (index >= module->uniform_views.size()) {
		return 0;
	}
	*out_info = module->uniform_views[index];
	return 1;
}

size_t rtslModuleGetStageVariableCount(rtsl_module module) {
	if (!module) {
		return 0;
	}
	module->ensure_reflection();
	return module->stage_views.size();
}

int rtslModuleGetStageVariable(rtsl_module module, size_t index, rtsl_stage_variable* out_var) {
	if (!module || !out_var) {
		return 0;
	}
	module->ensure_reflection();
	if (index >= module->stage_views.size()) {
		return 0;
	}
	*out_var = module->stage_views[index];
	return 1;
}

int rtslModuleGetStageLocation(rtsl_module module, rtsl_stage_role role, const char* field_name, uint32_t* out_location) {
	if (!module || !field_name || !out_location) {
		return 0;
	}
	module->ensure_reflection();
	for (const auto& view : module->stage_views) {
		if (view.role == role && std::strcmp(view.name, field_name) == 0) {
			*out_location = view.location;
			return 1;
		}
	}
	return 0;
}

size_t rtslModuleGetEntryCount(rtsl_module module) {
	if (!module) {
		return 0;
	}
	module->ensure_reflection();
	return module->entry_views.size();
}

int rtslModuleGetEntry(rtsl_module module, size_t index, rtsl_entry_info* out_entry) {
	if (!module || !out_entry) {
		return 0;
	}
	module->ensure_reflection();
	if (index >= module->entry_views.size()) {
		return 0;
	}
	*out_entry = module->entry_views[index];
	return 1;
}

rtsl_linker rtslCreateLinker(rtsl_context ctx) {
	if (!ctx) {
		return nullptr;
	}
	auto* linker = new (std::nothrow) rtsl_linker_t(ctx);
	if (!linker) {
		set_result(ctx, RTSL_ERROR_INTERNAL, "failed to create linker");
	}
	return linker;
}

int rtslLinkerAddModule(rtsl_linker linker, rtsl_module module) {
	if (!linker || !module) {
		return 0;
	}
	return linker->linker.add_artifact(module->artifact) ? 1 : 0;
}

int rtslLinkerAddBlob(rtsl_linker linker, const uint8_t* data, size_t size) {
	if (!linker || (!data && size != 0)) {
		return 0;
	}
	return linker->linker.add_artifact_bytes(std::span<const rtsl::u08>(data, size)) ? 1 : 0;
}

rtsl_module link_with(rtsl_linker linker, bool program) {
	if (!linker) {
		return nullptr;
	}
	try {
		auto artifact = program ? linker->linker.link_program() : linker->linker.link_library();
		if (linker->ctx->compiler.diagnostics().has_error() || artifact.bytes.empty()) {
			set_result(linker->ctx, RTSL_ERROR_LINK_FAILED, "link failed");
			return nullptr;
		}
		set_result(linker->ctx, RTSL_OK, "ok");
		return new rtsl_module_t{ .artifact = std::move(artifact) };
	} catch (...) {
		set_result(linker->ctx, RTSL_ERROR_INTERNAL, "internal linker error");
		return nullptr;
	}
}

rtsl_module rtslLinkLibrary(rtsl_linker linker) {
	return link_with(linker, false);
}

rtsl_module rtslLinkProgram(rtsl_linker linker) {
	return link_with(linker, true);
}

void rtslDestroyLinker(rtsl_linker linker) {
	delete linker;
}

} // extern "C"
