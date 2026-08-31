#include <rtsl/IR/Builder.hpp>
#include <rtsl/IR/Verifier.hpp>

#include <catch2/catch_test_macros.hpp>

TEST_CASE("entry point default stage configuration is a monostate") {
	rtsl::ir::EntryPoint Entry{};
	REQUIRE(Entry.configuration.index() == 0);
	rtsl::ir::ModuleBuilder Builder("entry");
	rtsl::ir::Type VoidType;
	VoidType.kind = rtsl::ir::TypeKind::type_void;
	auto Void = Builder.internType(VoidType);
	auto Symbol = Builder.addSymbol("entry::main");
	auto Function = Builder.addFunction(Symbol, Void, {}, {}, false);
	auto Block = Builder.addBlock(Function);
	Builder.setTerminator(Function, Block, {.kind = rtsl::ir::TerminatorKind::terminator_return});
	Entry.symbol = Symbol;
	Entry.function = Function;
	Entry.stage = rtsl::ir::Stage::stage_vertex;
	Entry.configuration = std::monostate{};
	Builder.addEntryPoint(Entry);
	REQUIRE(Builder.module().entry_points.front().configuration.index() == 0);
}

TEST_CASE("RTIR interns names in the module string table") {
	rtsl::ir::ModuleBuilder builder("example");
	const rtsl::ir::SymbolId first = builder.addSymbol("example::main");
	const rtsl::ir::SymbolId second = builder.addSymbol("example::main");

	REQUIRE(first == second);
	const rtsl::ir::Symbol* symbol = builder.module().findSymbol(first);
	REQUIRE(symbol != nullptr);
	REQUIRE(builder.module().strings.get(symbol->fully_qualified_name) == "example::main");
}

TEST_CASE("RTIR verifies a terminated typed function") {
	rtsl::ir::ModuleBuilder builder("example");
	rtsl::ir::Type void_type;
	void_type.kind = rtsl::ir::TypeKind::type_void;
	const rtsl::ir::TypeId void_id = builder.internType(void_type);
	const rtsl::ir::SymbolId symbol = builder.addSymbol("example::main");
	const rtsl::ir::FunctionId function = builder.addFunction(symbol, void_id, {});
	const rtsl::ir::BlockId entry = builder.addBlock(function);
	rtsl::ir::Terminator terminator;
	terminator.kind = rtsl::ir::TerminatorKind::terminator_return;
	builder.setTerminator(function, entry, terminator);

	const rtsl::ir::VerificationResult result = rtsl::ir::verify(builder.module());
	REQUIRE(result.valid());
}

TEST_CASE("RTIR rejects a construct whose operands do not form its result type") {
	rtsl::ir::ModuleBuilder builder("example");
	rtsl::ir::Type void_type;
	void_type.kind = rtsl::ir::TypeKind::type_void;
	const rtsl::ir::TypeId void_id = builder.internType(void_type);
	rtsl::ir::Type float_type;
	float_type.kind = rtsl::ir::TypeKind::type_floating;
	float_type.bit_width = 32;
	const rtsl::ir::TypeId float_id = builder.internType(float_type);
	rtsl::ir::Type vector_type;
	vector_type.kind = rtsl::ir::TypeKind::type_vector;
	vector_type.element_type = float_id;
	vector_type.element_count = 4;
	const rtsl::ir::TypeId vector_id = builder.internType(vector_type);
	const rtsl::ir::SymbolId symbol = builder.addSymbol("example::main");
	const rtsl::ir::FunctionId function = builder.addFunction(symbol, void_id, {});
	const rtsl::ir::BlockId entry = builder.addBlock(function);
	const std::uint32_t zero = 0;
	const rtsl::ir::ValueId scalar = builder.appendInstruction(function, entry,
		rtsl::ir::Opcode::opcode_constant_floating, float_id, {}, std::span(&zero, 1));
	builder.appendInstruction(function, entry, rtsl::ir::Opcode::opcode_construct, vector_id,
		std::span(&scalar, 1));
	rtsl::ir::Terminator terminator;
	terminator.kind = rtsl::ir::TerminatorKind::terminator_return;
	builder.setTerminator(function, entry, terminator);

	const rtsl::ir::VerificationResult result = rtsl::ir::verify(builder.module());
	REQUIRE_FALSE(result.valid());
	bool found_type_mismatch{};
	for (const rtsl::ir::VerificationIssue& issue : result.issues())
		found_type_mismatch |= issue.code == rtsl::ir::VerificationCode::verification_type_mismatch;
	REQUIRE(found_type_mismatch);
}
