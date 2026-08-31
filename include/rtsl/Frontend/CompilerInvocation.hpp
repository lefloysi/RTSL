#ifndef RTSL_FRONTEND_COMPILER_INVOCATION_HPP
#define RTSL_FRONTEND_COMPILER_INVOCATION_HPP

#include <string>
#include <vector>

namespace rtsl {

struct TranslationUnitInput {
	std::string ImportPath;
	std::string InputName;
	std::string InputBuffer;
};

struct ModuleInterfaceInput {
	std::string ImportPath;
	std::string InterfacePath;
};

struct LibraryInterfaceInput {
	std::string LibraryName;
	std::string InterfacePath;
};

class CompilerInvocation {
public:
	void setModuleName(std::string Name) { ModuleName = std::move(Name); }
	void setInputName(std::string Name) { InputName = std::move(Name); }
	void setInputBuffer(std::string Buffer) { InputBuffer = std::move(Buffer); }
	void addTranslationUnit(TranslationUnitInput Input) { TranslationUnits.push_back(std::move(Input)); }
	void addModuleInterface(ModuleInterfaceInput Input) { ModuleInterfaces.push_back(std::move(Input)); }
	void addLibraryInterface(LibraryInterfaceInput Input) { LibraryInterfaces.push_back(std::move(Input)); }
	[[nodiscard]] const std::string& getModuleName() const { return ModuleName; }
	[[nodiscard]] const std::string& getInputName() const { return InputName; }
	[[nodiscard]] const std::string& getInputBuffer() const { return InputBuffer; }
	[[nodiscard]] const std::vector<TranslationUnitInput>& getTranslationUnits() const { return TranslationUnits; }
	[[nodiscard]] const std::vector<ModuleInterfaceInput>& getModuleInterfaces() const { return ModuleInterfaces; }
	[[nodiscard]] const std::vector<LibraryInterfaceInput>& getLibraryInterfaces() const { return LibraryInterfaces; }
private:
	std::string ModuleName;
	std::string InputName;
	std::string InputBuffer;
	std::vector<TranslationUnitInput> TranslationUnits;
	std::vector<ModuleInterfaceInput> ModuleInterfaces;
	std::vector<LibraryInterfaceInput> LibraryInterfaces;
};

}

#endif
