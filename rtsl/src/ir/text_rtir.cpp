#include "ir/text_rtir.hpp"

#include <sstream>

namespace rtsl {

namespace {

std::string_view kind_name(ArtifactKind kind) {
	switch (kind) {
	case ArtifactKind::object: return "rtslo";
	case ArtifactKind::module: return "rtslm";
	case ArtifactKind::library: return "rtsll";
	case ArtifactKind::program: return "rtslp";
	}
	return "unknown";
}

void append_instruction(std::ostringstream& out, const IRInstruction& inst) {
	if (inst.result_id != ID<IRInstruction>{}) {
		out << "%" << raw_id(inst.result_id) << " = ";
	}
	out << ir_op_name(inst.op);
	if (inst.type_id != ID<IRInstruction>{})
		out << " %" << raw_id(inst.type_id);
	for (ID<IRInstruction> operand : inst.operands)
		out << " %" << raw_id(operand);
	for (u32 literal : inst.literals)
		out << " " << literal;
	out << "\n";
}

} // namespace

std::string disassemble_artifact(const Artifact& artifact) {
	std::ostringstream out;
	out << "artifact " << kind_name(artifact.kind) << " " << ArtifactVersionMajor << "." << ArtifactVersionMinor << "\n";
	out << "source \"" << artifact.module.source_name << "\"\n\n";
	if (!artifact.module.imports.empty()) {
		out << "; imports\n";
		for (const auto& name : artifact.module.imports) {
			out << "import \"" << name << "\";\n";
		}
		out << "\n";
	}
	out << "; type_constant_pool\n";
	for (const auto& inst : artifact.module.type_constant_pool)
		append_instruction(out, inst);
	out << "\n; global_variables\n";
	for (const auto& inst : artifact.module.global_variables)
		append_instruction(out, inst);
	for (const auto& fn : artifact.module.functions) {
		out << "\n; function %" << raw_id(fn.result_id);
		if (!fn.stage.empty())
			out << " stage=" << fn.stage;
		if (fn.is_generated())
			out << " generated";
		out << "\n";
		for (const auto& inst : fn.body)
			append_instruction(out, inst);
	}
	return out.str();
}

} // namespace rtsl
