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
	if (inst.result_id != IRId_None) {
		out << "%" << inst.result_id << " = ";
	}
	out << ir_op_name(inst.op);
	if (inst.type_id != IRId_None)
		out << " %" << inst.type_id;
	for (IRId operand : inst.operands)
		out << " %" << operand;
	for (u32 literal : inst.literals)
		out << " " << literal;
	out << "\n";
}

} // namespace

const char* ir_op_name(IROp op) {
	// Mnemonic table generated from the same ops.def the enum came from.
	switch (op) {
#define RTSL_IR_OP(name, disasm_name) \
	case IROp::name: return disasm_name;
#include "ir/ops.def"
	}
	return "OpUnknown";
}

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
		out << "\n; function %" << fn.result_id;
		if (!fn.stage.empty())
			out << " stage=" << fn.stage;
		if (fn.generated)
			out << " generated";
		out << "\n";
		for (const auto& inst : fn.body)
			append_instruction(out, inst);
	}
	return out.str();
}

} // namespace rtsl
