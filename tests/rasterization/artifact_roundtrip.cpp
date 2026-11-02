#include <rtsl/Serialization/Artifact.hpp>
#include <rtsl/IR/Verifier.hpp>

#include <cstdlib>

int main() {
	rtsl::Artifact artifact;
	auto& module = artifact.module;
	rtsl::ir::Type scalar;
	scalar.id = rtsl::ir::TypeId(1);
	scalar.kind = rtsl::ir::TypeKind::type_floating;
	scalar.bit_width = 32;
	module.types.push_back(scalar);
	rtsl::ir::Type array;
	array.id = rtsl::ir::TypeId(2);
	array.kind = rtsl::ir::TypeKind::type_array;
	array.element_type = scalar.id;
	array.element_count = 2;
	module.types.push_back(array);
	rtsl::ir::Type vertex;
	vertex.id = rtsl::ir::TypeId(3);
	vertex.kind = rtsl::ir::TypeKind::type_structure;
	vertex.members.push_back({module.strings.intern("point_size"), scalar.id});
	vertex.members.push_back({module.strings.intern("clip_distance"), array.id});
	vertex.members.push_back({module.strings.intern("cull_distance"), array.id});
	vertex.builtin_members.push_back({rtsl::ir::Builtin::builtin_point_size, {0}});
	vertex.builtin_members.push_back({rtsl::ir::Builtin::builtin_clip_distance, {1}});
	vertex.builtin_members.push_back({rtsl::ir::Builtin::builtin_cull_distance, {2}});
	module.types.push_back(vertex);
	const auto written = rtsl::ArtifactWriter().write(artifact);
	if (!written) return EXIT_FAILURE;
	const auto read = rtsl::ArtifactReader().read(written.bytes);
	if (!read) return EXIT_FAILURE;
	const auto* restored = read.artifact->module.findType(vertex.id);
	if (!restored || restored->builtin_members != vertex.builtin_members) return EXIT_FAILURE;
	const auto* restored_array = read.artifact->module.findType(array.id);
	if (!restored_array || restored_array->kind != array.kind || restored_array->element_count != 2 || restored_array->element_type != scalar.id) return EXIT_FAILURE;

	// A nested builtin must reach a real member at every level.
	rtsl::ir::Type outer;
	outer.id = rtsl::ir::TypeId(4);
	outer.kind = rtsl::ir::TypeKind::type_structure;
	outer.members.push_back({module.strings.intern("vertex"), vertex.id});
	outer.builtin_members.push_back({rtsl::ir::Builtin::builtin_point_size, {0, 0}});
	module.types.push_back(outer);
	if (!rtsl::ir::verify(module).valid()) return EXIT_FAILURE;
	module.types.back().builtin_members[0].member_path = {0, 3};
	if (rtsl::ir::verify(module).valid()) return EXIT_FAILURE;
	module.types.back().builtin_members[0].member_path = {0, 0, 0};
	if (rtsl::ir::verify(module).valid()) return EXIT_FAILURE;
	module.types.back().builtin_members[0].member_path.clear();
	if (rtsl::ir::verify(module).valid()) return EXIT_FAILURE;
	return EXIT_SUCCESS;
}
