#include <rtsl/Frontend/CompilerInstance.hpp>

#include <rtsl/Frontend/ModuleInterface.hpp>
#include <rtsl/Frontend/ModuleInterfaceImporter.hpp>
#include <rtsl/Frontend/CoreSource.hpp>
#include <rtsl/Lex/Lexer.hpp>

#include <rtsl/Lex/Preprocessor.hpp>
#include <rtsl/Parse/Parser.hpp>
#include <rtsl/Sema/Sema.hpp>

#include <unordered_set>

#include <fstream>
#include <functional>
#include <iterator>
#include <unordered_map>

namespace rtsl {
namespace {

struct RequestedImport {
	ImportDecl::Kind kind{ImportDecl::Kind::import_file};
	std::string name;
};

std::vector<RequestedImport> discoverImports(std::string_view name, std::string_view buffer,
	DiagnosticsEngine& diagnostics) {
	SourceManager sources;
	IdentifierTable identifiers;
	Lexer lexer(sources.createFileID(name, buffer), sources, identifiers, diagnostics);
	std::vector<RequestedImport> result;
	Token token;
	while (true) {
		lexer.lex(token);
		if (token.is(tok::eof)) break;
		if (token.isNot(tok::kw_import)) continue;
		Token argument;
		lexer.lex(argument);
		if (argument.is(tok::string_literal)) {
			auto text = std::string_view(argument.getLiteralData(), argument.getLength());
			if (text.size() >= 2) result.push_back({ImportDecl::Kind::import_file, std::string(text.substr(1, text.size() - 2))});
			continue;
		}
		if (!argument.is(tok::less)) continue;
		lexer.lex(argument);
		if (!argument.is(tok::identifier)) continue;
		const std::string library(argument.getIdentifierInfo()->getName());
		lexer.lex(argument);
		if (argument.is(tok::greater)) result.push_back({ImportDecl::Kind::import_library, library});
	}
	return result;
}

std::optional<ModuleInterface> readInterfaceFile(const std::string& path, DiagnosticsEngine& diagnostics) {
	std::ifstream stream(path, std::ios::binary);
	if (!stream) {
		diagnostics.report(DiagnosticLevel::diagnostic_error, {}, "cannot open module interface supplied by the build");
		return std::nullopt;
	}
	std::vector<char> characters((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
	std::vector<std::byte> bytes(characters.size());
	for (std::size_t index = 0; index < characters.size(); ++index) bytes[index] = static_cast<std::byte>(characters[index]);
	auto decoded = ModuleInterfaceReader{}.read(bytes);
	if (decoded) return std::move(*decoded.interface);
	diagnostics.report(DiagnosticLevel::diagnostic_error, {}, decoded.error->message);
	return std::nullopt;
}

} // namespace

CompilerInstance::CompilerInstance() = default;
CompilerInstance::~CompilerInstance() = default;

bool CompilerInstance::execute() {
	Context = std::make_unique<ASTContext>();
	PP = std::make_unique<Preprocessor>(Sources, Diagnostics);
	Actions = std::make_unique<Sema>(*Context, Diagnostics, PP->getIdentifierTable());

	std::unordered_map<std::string_view, const TranslationUnitInput*> sources;
	std::unordered_map<std::string_view, std::string_view> interfaces;
	std::unordered_map<std::string_view, std::string_view> libraries;
	for (const TranslationUnitInput& unit : Invocation.getTranslationUnits()) sources.emplace(unit.ImportPath, &unit);
	for (const ModuleInterfaceInput& interface : Invocation.getModuleInterfaces()) interfaces.emplace(interface.ImportPath, interface.InterfacePath);
	for (const LibraryInterfaceInput& library : Invocation.getLibraryInterfaces()) libraries.emplace(library.LibraryName, library.InterfacePath);
	std::unordered_set<std::string> loaded_interfaces;
	std::unordered_set<std::string> visiting_sources;
	std::unordered_set<std::string> visited_sources;
	std::vector<const TranslationUnitInput*> ordered_sources;
	std::function<void(const std::string&, ImportDecl::Kind)> loadInterface;
	loadInterface = [&](const std::string& key, ImportDecl::Kind kind) {
		const auto& table = kind == ImportDecl::Kind::import_file ? interfaces : libraries;
		auto position = table.find(key);
		if (position == table.end()) return;
		if (!loaded_interfaces.insert(std::string(position->second)).second) return;
		auto interface = readInterfaceFile(std::string(position->second), Diagnostics);
		if (!interface) return;
		for (const InterfaceUnit& unit : interface->units) for (const InterfaceImport& dependency : unit.imports)
			loadInterface(dependency.name, dependency.kind == InterfaceImportKind::import_file ? ImportDecl::Kind::import_file : ImportDecl::Kind::import_library);
		for (const std::string& diagnostic : ModuleInterfaceImporter{}.import(*interface, *Actions))
			Diagnostics.report(DiagnosticLevel::diagnostic_error, {}, diagnostic);
	};
	std::function<void(std::string_view, std::string_view, std::string_view)> visitSource;
	visitSource = [&](std::string_view key, std::string_view name, std::string_view buffer) {
		const std::string source_key(key);
		if (visiting_sources.contains(source_key)) {
			Diagnostics.report(DiagnosticLevel::diagnostic_error, {}, "recursive source import is not yet supported");
			return;
		}
		if (!visited_sources.insert(source_key).second) return;
		visiting_sources.insert(source_key);
		for (const RequestedImport& dependency : discoverImports(name, buffer, Diagnostics)) {
			if (dependency.kind == ImportDecl::Kind::import_library) { loadInterface(dependency.name, dependency.kind); continue; }
			if (auto source = sources.find(dependency.name); source != sources.end())
				visitSource(source->first, source->second->InputName, source->second->InputBuffer);
			else if (interfaces.contains(dependency.name)) loadInterface(dependency.name, dependency.kind);
			else Diagnostics.report(DiagnosticLevel::diagnostic_error, {}, "import does not name a translation unit or module interface supplied by the build");
		}
		visiting_sources.erase(source_key);
		if (key != "<main>") ordered_sources.push_back(sources.find(key)->second);
	};
	visitSource("<main>", Invocation.getInputName(), Invocation.getInputBuffer());
	for (const TranslationUnitInput& unit : Invocation.getTranslationUnits())
		visitSource(unit.ImportPath, unit.InputName, unit.InputBuffer);
	Actions->setParsingCore(true);
	PP->enterSourceFile(Sources.createFileID("<rtsl-core>", core::source));
	SyntaxParser = std::make_unique<Parser>(*PP, *Actions, Diagnostics);
	SyntaxParser->parseTranslationUnit();
	Actions->setParsingCore(false);
	std::vector<FileID> files;
	files.push_back(Sources.createFileID(Invocation.getInputName(), Invocation.getInputBuffer()));
	for (const TranslationUnitInput* unit : ordered_sources) files.push_back(Sources.createFileID(unit->InputName, unit->InputBuffer));
	for (FileID file : files) PP->enterSourceFile(file);
	SyntaxParser = std::make_unique<Parser>(*PP, *Actions, Diagnostics);
	SyntaxParser->parseTranslationUnit();
	validateImports();
	return !Diagnostics.hasErrorOccurred();
}

void CompilerInstance::validateImports() {
	std::unordered_set<std::string_view> KnownImports;
	std::unordered_set<std::string_view> KnownLibraries;
	for (const auto& Unit : Invocation.getTranslationUnits()) {
		if (!KnownImports.insert(Unit.ImportPath).second)
			Diagnostics.report(DiagnosticLevel::diagnostic_error, {}, "build supplies more than one translation unit or module interface for the same import path");
	}
	for (const auto& Interface : Invocation.getModuleInterfaces()) {
		if (!KnownImports.insert(Interface.ImportPath).second)
			Diagnostics.report(DiagnosticLevel::diagnostic_error, {}, "build supplies more than one translation unit or module interface for the same import path");
	}
	for (const auto& Library : Invocation.getLibraryInterfaces()) {
		if (!KnownLibraries.insert(Library.LibraryName).second)
			Diagnostics.report(DiagnosticLevel::diagnostic_error, {}, "build supplies more than one interface for the same library name");
	}
	if (KnownImports.empty() && KnownLibraries.empty()) return;
	for (Decl* Declaration = Context->getTranslationUnitDecl()->declsBegin(); Declaration;
		Declaration = Declaration->getNextDeclInContext()) {
		if (Declaration->getKind() != DeclKind::decl_import) continue;
		auto Import = static_cast<ImportDecl*>(Declaration);
		if (Import->getImportKind() == ImportDecl::Kind::import_file && KnownImports.contains(Import->getModuleName())) continue;
		if (Import->getImportKind() == ImportDecl::Kind::import_library && KnownLibraries.contains(Import->getModuleName())) continue;
		Diagnostics.report(DiagnosticLevel::diagnostic_error, {Import->getLocation(), Import->getLocation()},
			Import->getImportKind() == ImportDecl::Kind::import_file
				? "import does not name a translation unit or module interface supplied by the build"
				: "import does not name a library interface supplied by the build");
	}
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
