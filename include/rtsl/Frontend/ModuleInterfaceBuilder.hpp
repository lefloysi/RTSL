#ifndef RTSL_FRONTEND_MODULE_INTERFACE_BUILDER_HPP
#define RTSL_FRONTEND_MODULE_INTERFACE_BUILDER_HPP

#include <rtsl/AST/ASTContext.hpp>
#include <rtsl/Frontend/ModuleInterface.hpp>

#include <string>
#include <string_view>
#include <vector>

namespace rtsl {

struct ModuleInterfaceBuildResult {
	InterfaceUnit unit;
	std::vector<std::string> diagnostics;
	[[nodiscard]] bool succeeded() const { return diagnostics.empty(); }
};

class ModuleInterfaceBuilder {
public:
	[[nodiscard]] ModuleInterfaceBuildResult buildUnit(std::string_view import_path,
		const TranslationUnitDecl& translation_unit) const;

private:
	[[nodiscard]] InterfaceType buildType(QualType type) const;
	[[nodiscard]] std::vector<InterfaceAttribute> buildAttributes(const Attr* attributes) const;
};

} // namespace rtsl

#endif
