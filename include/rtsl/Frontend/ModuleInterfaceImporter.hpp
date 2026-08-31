#ifndef RTSL_FRONTEND_MODULE_INTERFACE_IMPORTER_HPP
#define RTSL_FRONTEND_MODULE_INTERFACE_IMPORTER_HPP

#include <rtsl/Frontend/ModuleInterface.hpp>

#include <string>
#include <vector>

namespace rtsl {

class Sema;

// Installs declarations from a resolved .rtslm interface into the consuming
// semantic context. The importer does not make them exported by the consumer.
class ModuleInterfaceImporter {
public:
	[[nodiscard]] std::vector<std::string> import(const ModuleInterface& interface, Sema& sema) const;
};

} // namespace rtsl

#endif
