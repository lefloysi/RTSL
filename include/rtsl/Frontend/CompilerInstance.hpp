#ifndef RTSL_FRONTEND_COMPILER_INSTANCE_HPP
#define RTSL_FRONTEND_COMPILER_INSTANCE_HPP

#include <rtsl/AST/ASTContext.hpp>
#include <rtsl/Basic/Diagnostic.hpp>
#include <rtsl/Basic/SourceManager.hpp>
#include <rtsl/CodeGen/CodeGenerator.hpp>
#include <rtsl/Frontend/CompilerInvocation.hpp>
#include <rtsl/Linker/Linker.hpp>

#include <memory>

namespace rtsl {

class Parser;
class Preprocessor;
class Sema;

struct LinkedIRCompilation {
	bool FrontendSucceeded{};
	CodeGenResult CodeGeneration;
	LinkResult Link;
	[[nodiscard]] bool succeeded() const {
		return FrontendSucceeded && CodeGeneration.succeeded() && Link.succeeded();
	}
};

class CompilerInstance {
public:
	CompilerInstance();
	~CompilerInstance();
	void setInvocation(CompilerInvocation Value) { Invocation = std::move(Value); }
	[[nodiscard]] bool execute();
	[[nodiscard]] LinkedIRCompilation compileToLinkedRTIR();
	[[nodiscard]] ASTContext* getASTContext() const { return Context.get(); }
	[[nodiscard]] DiagnosticsEngine& getDiagnostics() { return Diagnostics; }
	[[nodiscard]] const SourceManager& getSourceManager() const { return Sources; }
	[[nodiscard]] Preprocessor* getPreprocessor() const { return PP.get(); }
	[[nodiscard]] Sema* getSema() const { return Actions.get(); }
	[[nodiscard]] Parser* getParser() const { return SyntaxParser.get(); }
private:
	void validateImports();
	CompilerInvocation Invocation;
	DiagnosticsEngine Diagnostics;
	SourceManager Sources;
	std::unique_ptr<ASTContext> Context;
	std::unique_ptr<Preprocessor> PP;
	std::unique_ptr<Sema> Actions;
	std::unique_ptr<Parser> SyntaxParser;
};

}

#endif
