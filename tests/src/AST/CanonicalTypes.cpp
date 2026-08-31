#include <rtsl/AST/ASTContext.hpp>

#include <catch2/catch_test_macros.hpp>

TEST_CASE("ASTContext canonicalizes derived and named types") {
	rtsl::ASTContext Context;
	rtsl::IdentifierTable Identifiers;
	auto& Name = Identifiers.get("Vertex");

	const rtsl::QualType Vertex = Context.getNamedType(&Name);
	REQUIRE(Vertex == Context.getNamedType(&Name));
	REQUIRE(Context.getPointerType(Vertex) == Context.getPointerType(Vertex));
	REQUIRE(Context.getReferenceType(Vertex) == Context.getReferenceType(Vertex));

	const std::vector Arguments{Vertex, Context.getBuiltinType(rtsl::BuiltinTypeKind::builtin_u32)};
	REQUIRE(Context.getTemplateSpecializationType(&Name, Arguments) ==
		Context.getTemplateSpecializationType(&Name, Arguments));
}
