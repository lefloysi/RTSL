#include <rtsl/IR/Builder.hpp>

#include <algorithm>
#include <stdexcept>
#include <utility>

namespace rtsl::ir {

ModuleBuilder::ModuleBuilder(std::string_view name) {
	module_value.name = module_value.strings.intern(name);
}

Module ModuleBuilder::takeModule() noexcept {
	return std::move(module_value);
}

SymbolId ModuleBuilder::addSymbol(std::string_view fully_qualified_name, bool exported) {
	for (Symbol& symbol : module_value.symbols) {
		if (module_value.strings.get(symbol.fully_qualified_name) == fully_qualified_name) {
			symbol.exported = symbol.exported || exported;
			return symbol.id;
		}
	}
	const StringId spelling = module_value.strings.intern(fully_qualified_name);
	if (!spelling) {
		throw std::length_error("symbol string table capacity exceeded");
	}
	const SymbolId id{next_symbol++};
	module_value.symbols.push_back(Symbol{.id = id, .fully_qualified_name = spelling, .exported = exported});
	return id;
}

TypeId ModuleBuilder::internType(Type type) {
	const auto found = std::ranges::find_if(module_value.types, [&](const Type& candidate) { return candidate.structurallyEquals(type); });
	if (found != module_value.types.end()) {
		return found->id;
	}
	type.id = TypeId{next_type++};
	const TypeId id = type.id;
	module_value.types.push_back(std::move(type));
	return id;
}

FunctionId ModuleBuilder::addFunction(SymbolId symbol, TypeId return_type, std::span<const TypeId> parameter_types,
	std::span<const SymbolId> parameter_symbols, bool declaration) {
	return addFunction(symbol, return_type, parameter_types, parameter_symbols, declaration, false);
}

FunctionId ModuleBuilder::addFunction(SymbolId symbol, TypeId return_type, std::span<const TypeId> parameter_types,
	std::span<const SymbolId> parameter_symbols, bool declaration, bool implicit_emitter) {
	if (!parameter_symbols.empty() && parameter_symbols.size() != parameter_types.size()) {
		throw std::invalid_argument("parameter symbol count does not match parameter type count");
	}
	Function function;
	function.id = FunctionId{next_function++};
	function.symbol = symbol;
	function.return_type = return_type;
	function.declaration = declaration;
	function.implicit_emitter = implicit_emitter;
	for (std::size_t index = 0; index < parameter_types.size(); ++index) {
		function.parameters.push_back(Parameter{
			.value = nextValue(),
			.type = parameter_types[index],
			.symbol = parameter_symbols.empty() ? SymbolId{} : parameter_symbols[index],
		});
	}
	const FunctionId id = function.id;
	module_value.functions.push_back(std::move(function));
	return id;
}

BlockId ModuleBuilder::addBlock(FunctionId function_id) {
	Function& function = requireFunction(function_id);
	const BlockId id{next_block++};
	function.blocks.push_back(Block{.id = id});
	return id;
}

ValueId ModuleBuilder::addBlockArgument(FunctionId function_id, BlockId block_id, TypeId type) {
	Function& function = requireFunction(function_id);
	Block& block = requireBlock(function, block_id);
	const ValueId value = nextValue();
	block.arguments.push_back(BlockArgument{.value = value, .type = type});
	return value;
}

ValueId ModuleBuilder::appendInstruction(FunctionId function_id, BlockId block_id, Opcode opcode, TypeId type, std::span<const ValueId> operands, std::span<const std::uint32_t> immediates, FunctionId callee) {
	Function& function = requireFunction(function_id);
	Block& block = requireBlock(function, block_id);
	if (block.terminator) {
		throw std::logic_error("cannot append an instruction after a terminator");
	}
	const ValueId result = type ? nextValue() : ValueId{};
	block.instructions.push_back(Instruction{
		.opcode = opcode,
		.result = result,
		.type = type,
		.callee = callee,
		.operands = {operands.begin(), operands.end()},
		.immediates = {immediates.begin(), immediates.end()},
	});
	return result;
}

void ModuleBuilder::setTerminator(FunctionId function_id, BlockId block_id, Terminator terminator) {
	Function& function = requireFunction(function_id);
	Block& block = requireBlock(function, block_id);
	if (block.terminator) {
		throw std::logic_error("block already has a terminator");
	}
	block.terminator = std::move(terminator);
}

void ModuleBuilder::setMerge(FunctionId function_id, BlockId block_id, StructuredMerge merge) {
	Function& function = requireFunction(function_id);
	requireBlock(function, block_id).merge = merge;
}

void ModuleBuilder::addEntryPoint(const EntryPoint& entry_point) {
	EntryPoint& result = module_value.entry_points.emplace_back();
	result.symbol = entry_point.symbol;
	result.function = entry_point.function;
	result.source_name = entry_point.source_name;
	result.stage = entry_point.stage;
	result.parameter_contracts = entry_point.parameter_contracts;
	switch (entry_point.configuration.index()) {
	case 0: result.configuration.emplace<std::monostate>(); break;
	case 1: result.configuration.emplace<TessellationControlConfiguration>(std::get<TessellationControlConfiguration>(entry_point.configuration)); break;
	case 2: result.configuration.emplace<TessellationEvaluationConfiguration>(std::get<TessellationEvaluationConfiguration>(entry_point.configuration)); break;
	case 3: result.configuration.emplace<GeometryConfiguration>(std::get<GeometryConfiguration>(entry_point.configuration)); break;
	case 4: result.configuration.emplace<ComputeConfiguration>(std::get<ComputeConfiguration>(entry_point.configuration)); break;
	default: throw std::invalid_argument("entry point configuration is invalid");
	}
}
void ModuleBuilder::addResource(Resource resource) { module_value.resources.push_back(resource); }
void ModuleBuilder::addUniform(Uniform uniform) { module_value.uniforms.push_back(uniform); }
void ModuleBuilder::addStorageObject(StorageObject object) { module_value.storage_objects.push_back(object); }

ValueId ModuleBuilder::nextValue() { return ValueId{next_value++}; }

Function& ModuleBuilder::requireFunction(FunctionId function) {
	if (Function* result = module_value.findFunction(function)) {
		return *result;
	}
	throw std::invalid_argument("unknown function id");
}

Block& ModuleBuilder::requireBlock(Function& function, BlockId block) {
	const auto found = std::ranges::find(function.blocks, block, &Block::id);
	if (found == function.blocks.end()) {
		throw std::invalid_argument("unknown block id");
	}
	return *found;
}

} // namespace rtsl::ir
