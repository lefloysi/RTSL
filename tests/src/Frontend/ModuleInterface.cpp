#include <rtsl/Frontend/ModuleInterface.hpp>
#include <rtsl/Frontend/ModuleInterfaceBuilder.hpp>
#include <rtsl/Frontend/CompilerInstance.hpp>
#include <rtsl/Frontend/ModuleInterfaceImporter.hpp>
#include <rtsl/Sema/Sema.hpp>

#include <catch2/catch_test_macros.hpp>

TEST_CASE("module interfaces preserve exact import names and aggregate units") {
	rtsl::InterfaceType scalar;
	scalar.kind = rtsl::InterfaceTypeKind::type_f32;
	rtsl::InterfaceType vector;
	vector.kind = rtsl::InterfaceTypeKind::type_template_specialization;
	vector.name = "vec4";
	vector.arguments.push_back(scalar);
	rtsl::InterfaceType primitive;
	primitive.kind = rtsl::InterfaceTypeKind::type_template_specialization;
	primitive.name = "triangle_strip";
	primitive.arguments = {{.kind = rtsl::InterfaceTypeKind::type_named, .name = "Vertex"},
		{.kind = rtsl::InterfaceTypeKind::type_usize, .integer_value = 6}};

	rtsl::ModuleInterface interface;
	interface.library_name = "std";
	rtsl::InterfaceAttribute location{
		.name = "location",
		.tokens = {{.kind = 42, .spelling = "0"}},
	};
	interface.units = {
		{
			.import_path = "math/vector",
			.imports = {{.kind = rtsl::InterfaceImportKind::import_file, .name = "math/scalar"}},
			.declarations = {rtsl::InterfaceTypeAlias{.name = "Color", .attributes = {location}, .type = vector},
				rtsl::InterfaceTypeAlias{.name = "Output", .type = primitive}},
		},
		{
			.import_path = "graphics/curve.rtsl",
			.imports = {{.kind = rtsl::InterfaceImportKind::import_library, .name = "std"}},
			.declarations = {rtsl::InterfaceFunction{
				.name = "evaluate", .return_type = vector,
				.parameters = {{.name = "t", .type = scalar}},
				.type_only_parameters = {{.kind = rtsl::InterfaceTypeKind::type_template_specialization, .name = "tessellation", .arguments = {{.kind = rtsl::InterfaceTypeKind::type_named, .name = "equal"}}}},
				.template_parameters = {"T"}, .declaration = true}},
		},
	};

	const auto encoded = rtsl::ModuleInterfaceWriter{}.write(interface);
	REQUIRE(encoded);
	const auto decoded = rtsl::ModuleInterfaceReader{}.read(encoded.bytes);
	REQUIRE(decoded);
	REQUIRE(decoded.interface->library_name == "std");
	REQUIRE(decoded.interface->units.size() == 2);
	REQUIRE(decoded.interface->units[0].import_path == "math/vector");
	REQUIRE(decoded.interface->units[1].import_path == "graphics/curve.rtsl");
	REQUIRE(decoded.interface->units[0].imports[0].name == "math/scalar");
	REQUIRE(decoded.interface->units[1].imports[0].kind == rtsl::InterfaceImportKind::import_library);
	REQUIRE(decoded.interface->units[1].imports[0].name == "std");
	const auto& function = std::get<rtsl::InterfaceFunction>(decoded.interface->units[1].declarations[0]);
	REQUIRE(function.type_only_parameters.size() == 1);
	REQUIRE(function.type_only_parameters[0].name == "tessellation");
	const auto& alias = std::get<rtsl::InterfaceTypeAlias>(decoded.interface->units[0].declarations[0]);
	REQUIRE(alias.attributes.size() == 1);
	REQUIRE(alias.attributes[0].name == "location");
	REQUIRE(alias.attributes[0].tokens[0].spelling == "0");
	const auto& output = std::get<rtsl::InterfaceTypeAlias>(decoded.interface->units[0].declarations[1]);
	REQUIRE(output.type.arguments[1].integer_value == 6);
}

TEST_CASE("module interface reader rejects trailing data") {
	rtsl::ModuleInterface interface;
	interface.units.push_back({.import_path = "example"});
	auto encoded = rtsl::ModuleInterfaceWriter{}.write(interface);
	REQUIRE(encoded);
	encoded.bytes.push_back(std::byte{0});
	const auto decoded = rtsl::ModuleInterfaceReader{}.read(encoded.bytes);
	REQUIRE_FALSE(decoded);
	REQUIRE(decoded.error->code == rtsl::ModuleInterfaceErrorCode::error_trailing_data);
}

TEST_CASE("module interface builder selects exported declarations") {
	rtsl::CompilerInvocation invocation;
	invocation.setInputName("library.rtsl");
	invocation.setInputBuffer(R"(
struct Private { u32 value; }
@layout : (std430)
export struct Public { @location : (0) f32 value; }
@wire : (index)
export using Index = u32;
@binding : (0)
export var u32 count;
@stage : compute
export fn scale(@input : (0) f32 value) -> f32 { return value; }
)");
	rtsl::CompilerInstance compiler;
	compiler.setInvocation(std::move(invocation));
	REQUIRE(compiler.execute());
	const auto built = rtsl::ModuleInterfaceBuilder{}.buildUnit("library", *compiler.getASTContext()->getTranslationUnitDecl());
	REQUIRE(built.succeeded());
	REQUIRE(built.unit.import_path == "library");
	REQUIRE(built.unit.declarations.size() == 4);
	const auto& record = std::get<rtsl::InterfaceRecord>(built.unit.declarations[0]);
	REQUIRE(record.attributes[0].name == "layout");
	REQUIRE(record.fields[0].attributes[0].name == "location");
	const auto& function = std::get<rtsl::InterfaceFunction>(built.unit.declarations[3]);
	REQUIRE(function.attributes[0].name == "stage");
	REQUIRE(function.parameters[0].attributes[0].name == "input");
}

TEST_CASE("module interface importer exposes declarations to semantic analysis") {
	rtsl::InterfaceType scalar{.kind = rtsl::InterfaceTypeKind::type_f32};
	rtsl::ModuleInterface interface;
	interface.units.push_back({.import_path = "math/curve", .declarations = {
		rtsl::InterfaceTypeAlias{.name = "Weight", .type = scalar},
		rtsl::InterfaceVariable{.name = "default_weight", .type = scalar, .constant = true},
		rtsl::InterfaceFunction{.name = "evaluate", .return_type = scalar,
			.parameters = {{.name = "t", .type = scalar}}, .declaration = true},
	}});
	rtsl::ASTContext context;
	rtsl::DiagnosticsEngine diagnostics;
	rtsl::IdentifierTable identifiers;
	rtsl::Sema sema(context, diagnostics, identifiers);
	REQUIRE(rtsl::ModuleInterfaceImporter{}.import(interface, sema).empty());
	REQUIRE_FALSE(diagnostics.hasErrorOccurred());
	rtsl::ParsedType weight{.Name = &identifiers.get("Weight")};
	REQUIRE(sema.actOnType(weight));
	REQUIRE(sema.actOnIdentifierExpr(&identifiers.get("default_weight"), {}));
	REQUIRE(sema.actOnIdentifierExpr(&identifiers.get("evaluate"), {}));
}

TEST_CASE("build-supplied source imports are parsed before their consumer") {
	rtsl::CompilerInvocation invocation;
	invocation.setInputName("main.rtsl");
	invocation.setInputBuffer(R"(
import "math/weight";
fn main() -> f32 { var Weight value = default_weight; return value; }
)");
	invocation.addTranslationUnit({
		.ImportPath = "math/weight",
		.InputName = "math/weight.rtsl",
		.InputBuffer = "export using Weight = f32; export var f32 default_weight;",
	});
	rtsl::CompilerInstance compiler;
	compiler.setInvocation(std::move(invocation));
	REQUIRE(compiler.execute());
	REQUIRE(compiler.getSourceManager().getFileCount() == 2);
}
