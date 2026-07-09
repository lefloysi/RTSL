#include "support/basic_diagnostics.hpp"
#include "support/basic_source_manager.hpp"
#include "frontend/lexer.hpp"
#include "frontend/parser.hpp"

#include <catch2/catch_test_macros.hpp>

#include <sstream>

using namespace rtsl;

TEST_CASE("source locations are line-column mapped") {
	SourceManager sources;
	const auto file = sources.add_buffer("test.rtsl", "a\nbc\n");
	const auto loc = sources.location_at(file, 3);
	REQUIRE(loc.line == 2);
	REQUIRE(loc.column == 2);
}

TEST_CASE("diagnostics render with caret") {
	SourceManager sources;
	DiagnosticEngine diagnostics;
	const auto file = sources.add_buffer("test.rtsl", "line one\nbroken line\n");
	diagnostics.report(1, DiagnosticSeverity::error, sources.location_at(file, 9), "test.rtsl", "broken");
	std::ostringstream out;
	diagnostics.render(out, &sources);
	const auto text = out.str();
	REQUIRE(text.find("test.rtsl(") != std::string::npos);
	REQUIRE(text.find(": error") != std::string::npos);
	REQUIRE(text.find("broken line") != std::string::npos);
	REQUIRE(text.find('^') != std::string::npos);
}

TEST_CASE("lexer tokenizes keywords and punctuation") {
	SourceManager sources;
	DiagnosticEngine diagnostics;
	const auto file = sources.add_buffer("test.rtsl", "export fn helper() -> void {}");
	Lexer lexer(sources, diagnostics, file);
	const auto tokens = lexer.lex();
	REQUIRE_FALSE(diagnostics.has_error());
	REQUIRE(tokens[0].kind == TokenKind::kw_Export);
	REQUIRE(tokens[1].kind == TokenKind::kw_Function);
	REQUIRE(tokens[2].kind == TokenKind::identifier);
	REQUIRE(tokens[5].kind == TokenKind::arrow);
}

TEST_CASE("lexer reserves layout but leaves interpolation words contextual") {
	SourceManager sources;
	DiagnosticEngine diagnostics;
	const auto file = sources.add_buffer("test.rtsl", "layout smooth flat clip");
	Lexer lexer(sources, diagnostics, file);
	const auto tokens = lexer.lex();
	REQUIRE_FALSE(diagnostics.has_error());
	REQUIRE(tokens[0].kind == TokenKind::kw_Layout);
	REQUIRE(tokens[1].kind == TokenKind::identifier);
	REQUIRE(tokens[2].kind == TokenKind::identifier);
	REQUIRE(tokens[3].kind == TokenKind::identifier);
}

TEST_CASE("parser builds translation unit") {
	SourceManager sources;
	DiagnosticEngine diagnostics;
	const auto file = sources.add_buffer("test.rtsl", "export fn helper() {}");
	Lexer lexer(sources, diagnostics, file);
	const auto tokens = lexer.lex();
	Parser parser(sources, diagnostics, file, tokens);
	const auto unit = parser.parse_translation_unit();
	REQUIRE_FALSE(diagnostics.has_error());
	REQUIRE(unit.declarations.size() == 1);
	REQUIRE(unit.declarations.front().kind == DeclKind::function);
	REQUIRE(unit.declarations.front().exported);
}
