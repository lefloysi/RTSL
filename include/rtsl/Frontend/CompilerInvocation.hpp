#ifndef RTSL_FRONTEND_COMPILER_INVOCATION_HPP
#define RTSL_FRONTEND_COMPILER_INVOCATION_HPP

#include <string>

namespace rtsl {

class CompilerInvocation {
public:
	void setModuleName(std::string Name) { ModuleName = std::move(Name); }
	void setInputName(std::string Name) { InputName = std::move(Name); }
	void setInputBuffer(std::string Buffer) { InputBuffer = std::move(Buffer); }
	[[nodiscard]] const std::string& getModuleName() const { return ModuleName; }
	[[nodiscard]] const std::string& getInputName() const { return InputName; }
	[[nodiscard]] const std::string& getInputBuffer() const { return InputBuffer; }
private:
	std::string ModuleName;
	std::string InputName;
	std::string InputBuffer;
};

}

#endif
