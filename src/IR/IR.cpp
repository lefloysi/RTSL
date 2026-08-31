#include <rtsl/IR/IR.hpp>

#include <algorithm>
#include <limits>

namespace rtsl::ir {

StringTable::StringTable() {
	records.push_back({});
}

std::uint64_t StringTable::hash(std::string_view spelling) const noexcept {
	std::uint64_t value = 14695981039346656037ull;
	for (const unsigned char character : spelling) {
		value ^= character;
		value *= 1099511628211ull;
	}
	return value;
}

StringId StringTable::intern(std::string_view spelling) {
	const std::uint64_t spelling_hash = hash(spelling);
	if (const auto found = buckets.find(spelling_hash); found != buckets.end()) {
		for (const StringId id : found->second) {
			if (get(id) == spelling) {
				return id;
			}
		}
	}

	if (storage.size() > std::numeric_limits<std::uint32_t>::max() - spelling.size() || records.size() >= std::numeric_limits<std::uint32_t>::max()) {
		return {};
	}

	const Record record{
		.offset = static_cast<std::uint32_t>(storage.size()),
		.size = static_cast<std::uint32_t>(spelling.size()),
	};
	for (const unsigned char character : spelling) {
		storage.push_back(static_cast<std::byte>(character));
	}
	const StringId id{static_cast<std::uint32_t>(records.size())};
	records.push_back(record);
	buckets[spelling_hash].push_back(id);
	return id;
}

std::string_view StringTable::get(StringId id) const noexcept {
	if (!id || id.value() >= records.size()) {
		return {};
	}
	const Record& record = records[id.value()];
	if (record.size == 0) {
		return {};
	}
	if (record.offset > storage.size() || record.size > storage.size() - record.offset) {
		return {};
	}
	const char* data = reinterpret_cast<const char*>(storage.data() + record.offset);
	return {data, record.size};
}

std::span<const std::byte> StringTable::bytes() const noexcept {
	return storage;
}

bool Type::structurallyEquals(const Type& other) const {
	return kind == other.kind && bit_width == other.bit_width && element_type == other.element_type && element_count == other.element_count && address_space == other.address_space && parameter_types == other.parameter_types && members == other.members && name == other.name;
}

const Type* Module::findType(TypeId id) const noexcept {
	const auto found = std::ranges::find(types, id, &Type::id);
	return found == types.end() ? nullptr : &*found;
}

Type* Module::findType(TypeId id) noexcept {
	const auto found = std::ranges::find(types, id, &Type::id);
	return found == types.end() ? nullptr : &*found;
}

const Symbol* Module::findSymbol(SymbolId id) const noexcept {
	const auto found = std::ranges::find(symbols, id, &Symbol::id);
	return found == symbols.end() ? nullptr : &*found;
}

const Function* Module::findFunction(FunctionId id) const noexcept {
	const auto found = std::ranges::find(functions, id, &Function::id);
	return found == functions.end() ? nullptr : &*found;
}

Function* Module::findFunction(FunctionId id) noexcept {
	const auto found = std::ranges::find(functions, id, &Function::id);
	return found == functions.end() ? nullptr : &*found;
}

const Block* Module::findBlock(const Function& function, BlockId id) const noexcept {
	const auto found = std::ranges::find(function.blocks, id, &Block::id);
	return found == function.blocks.end() ? nullptr : &*found;
}

} // namespace rtsl::ir
