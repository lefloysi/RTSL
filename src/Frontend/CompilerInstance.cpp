#include <rtsl/Frontend/CompilerInstance.hpp>

#include <rtsl/Lex/Preprocessor.hpp>
#include <rtsl/Parse/Parser.hpp>
#include <rtsl/Sema/Sema.hpp>

namespace rtsl {

CompilerInstance::CompilerInstance() = default;
CompilerInstance::~CompilerInstance() = default;

bool CompilerInstance::execute() {
	Context = std::make_unique<ASTContext>();
	FileID MainFile = Sources.createFileID(Invocation.getInputName(), Invocation.getInputBuffer());
	PP = std::make_unique<Preprocessor>(Sources, Diagnostics);
	PP->enterSourceFile(MainFile);
	Actions = std::make_unique<Sema>(*Context, Diagnostics, PP->getIdentifierTable());
	SyntaxParser = std::make_unique<Parser>(*PP, *Actions, Diagnostics);
	SyntaxParser->parseTranslationUnit();
	return !Diagnostics.hasErrorOccurred();
}

LinkedIRCompilation CompilerInstance::compileToLinkedRTIR() {
	LinkedIRCompilation Result;
	Result.FrontendSucceeded = execute();
	if (!Result.FrontendSucceeded) return Result;
	CodeGenerator Generator(Invocation.getModuleName());
	Result.CodeGeneration = Generator.generate(*Context);
	if (!Result.CodeGeneration.succeeded()) return Result;
	Linker ProgramLinker;
	std::span<const ir::Module> Modules(&Result.CodeGeneration.Module, 1);
	Result.Link = ProgramLinker.link(Invocation.getModuleName(), Modules);
	return Result;
}

}
