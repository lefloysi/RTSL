#ifndef RTSL_IR_BUILDER_HPP
#define RTSL_IR_BUILDER_HPP

#include <rtsl/IR/IR.hpp>

#include <span>
#include <string_view>

namespace rtsl::ir {

class ModuleBuilder {
public:
	explicit ModuleBuilder(std::string_view name = {});

	[[nodiscard]] Module& module() noexcept { return module_value; }
	[[nodiscard]] const Module& module() const noexcept { return module_value; }
	[[nodiscard]] Module takeModule() noexcept;

	[[nodiscard]] SymbolId addSymbol(std::string_view fully_qualified_name, bool exported = false);
	[[nodiscard]] TypeId internType(Type type);
	[[nodiscard]] FunctionId addFunction(SymbolId symbol, TypeId return_type, std::span<const TypeId> parameter_types,
		std::span<const SymbolId> parameter_symbols = {}, bool declaration = false);
	[[nodiscard]] FunctionId addFunction(SymbolId symbol, TypeId return_type, std::span<const TypeId> parameter_types,
		std::span<const SymbolId> parameter_symbols, bool declaration, bool implicit_emitter);
	[[nodiscard]] BlockId addBlock(FunctionId function);
	[[nodiscard]] ValueId addBlockArgument(FunctionId function, BlockId block, TypeId type);
	[[nodiscard]] ValueId appendInstruction(FunctionId function, BlockId block, Opcode opcode, TypeId type = {}, std::span<const ValueId> operands = {}, std::span<const std::uint32_t> immediates = {}, FunctionId callee = {});
	void setTerminator(FunctionId function, BlockId block, Terminator terminator);
	void setMerge(FunctionId function, BlockId block, StructuredMerge merge);
	void addEntryPoint(const EntryPoint& entry_point);
	void addResource(Resource resource);
	void addUniform(Uniform uniform);
	void addStorageObject(StorageObject object);

private:
	[[nodiscard]] ValueId nextValue();
	[[nodiscard]] Function& requireFunction(FunctionId function);
	[[nodiscard]] Block& requireBlock(Function& function, BlockId block);

	Module module_value;
	std::uint32_t next_type{1};
	std::uint32_t next_value{1};
	std::uint32_t next_block{1};
	std::uint32_t next_function{1};
	std::uint32_t next_symbol{1};
};

} // namespace rtsl::ir

#endif
