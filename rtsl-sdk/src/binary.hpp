#pragma once

// Binary serialization for RTSL artifacts. Same shape as leaf's lf::bin —
// a `process(stream, value)` customization point, `field(name, value)` for
// named fields with diagnostic context, and read_stream/write_stream that
// track a cursor + context. Ported to std types so the compiler stays
// dependency-free (no leaf/lf include chain).
//
// Format contract:
// - Fixed-width integers are little-endian.
// - Floating-point values preserve their IEEE bit pattern as little-endian.
// - bool is one byte; canonical reads only accept 0 or 1.
// - strings encode as `u32 length` followed by bytes (no null terminator).
// - vectors encode as `u32 size` followed by contiguous elements.
// - enums encode as their underlying type.

#include "basic_types.hpp"

#include <algorithm>
#include <bit>
#include <concepts>
#include <cstddef>
#include <cstring>
#include <expected>
#include <format>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace rtsl::bin {

// Minimal error type. Non-empty message means failure; empty means success.
// Kept trivial on purpose — the API returns errors by value everywhere and
// callers propagate via early-return (see IF_ERR macro below).
struct error {
	std::string message;

	error() = default;
	explicit error(std::string m) : message(std::move(m)) {}

	explicit operator bool() const { return !message.empty(); }

	void add_context(std::string_view prefix) {
		if (message.empty()) return;
		message = std::format("{} : {}", prefix, message);
	}
};

#define RTSL_BIN_TRY(expr) do { if (auto _rtsl_err = (expr); _rtsl_err) return _rtsl_err; } while (false)

// Named field reference — how callers describe "this field goes on the wire
// with this label for diagnostics". The label never touches the byte stream.
// T carries its own cv-qualification: `field("x", const_x)` deduces T = const U
// so const values flow through the same code path as mutable ones without a
// second overload.
template <typename T>
struct field_ref {
	std::string_view name;
	T& value;
};

template <typename T>
field_ref<T> field(std::string_view name, T& value) {
	return { name, value };
}

// data<T, D> — matches T that stripped of cv/ref equals D. Lets a single
// process() overload cover both mutable and const forms of a target type
// (the standard lf::bin pattern). For write streams the const flavour flows
// through cleanly; for read streams the underlying write-into-value expression
// only instantiates on the mutable form.
template <typename T, typename D>
concept data = std::same_as<std::remove_cvref_t<T>, D>;

// Generic-integer concept: everything std::integral except bool (bool has its
// own overload because it encodes as 0/1 with validation).
template <typename T>
concept fixed_integer = std::integral<std::remove_cvref_t<T>> && !std::same_as<std::remove_cvref_t<T>, bool>;

template <typename T>
concept plain_enum = std::is_enum_v<std::remove_cvref_t<T>>;

struct read_stream_tag {};
struct write_stream_tag {};

template <typename S>
concept readable_stream = std::same_as<typename std::remove_cvref_t<S>::stream_tag, read_stream_tag>;
template <typename S>
concept writable_stream = std::same_as<typename std::remove_cvref_t<S>::stream_tag, write_stream_tag>;
template <typename S>
concept byte_stream = readable_stream<S> || writable_stream<S>;

// Forward declaration: every custom type overloads this. Free function so
// argument-dependent lookup finds the overload without users needing to open
// the rtsl::bin namespace.
template <typename Stream, typename T>
error process(Stream& stream, T& value);

namespace detail {

// Push `name` onto the stream's diagnostic context for the RAII lifetime of
// this scope. On failure, the context prefix gets prepended to the error
// message so the caller sees `entries : entries[0] : name : ...`.
template <typename Stream>
struct context_scope {
	Stream& stream;
	std::string previous;

	context_scope(Stream& s, std::string_view name) : stream(s), previous(s.context_str()) {
		if (name.empty()) return;
		std::string next = previous;
		if (!next.empty()) next += " : ";
		next.append(name);
		stream.set_context(std::move(next));
	}
	~context_scope() { stream.set_context(std::move(previous)); }
};

template <typename Stream>
void add_context_prefix(const Stream& stream, error& err) {
	if (!err || stream.context_str().empty()) return;
	err.add_context(stream.context_str());
}

// `storage_type_t<T>` — T itself for plain integrals, the underlying type
// for enums. Lazy specialization keeps `std::underlying_type_t` from being
// instantiated on non-enum T (it's ill-formed there). This is what lets one
// scalar body handle both integer and enum encoding.
template <typename T, bool IsEnum = std::is_enum_v<T>>
struct storage_type { using type = T; };
template <typename T>
struct storage_type<T, true> { using type = std::underlying_type_t<T>; };
template <typename T>
using storage_type_t = typename storage_type<T>::type;

// Write a fixed-width integer or enum in little-endian. One body: `U` is the
// enum's underlying type for enums, T itself otherwise, so the `static_cast`
// is either a no-op or the enum→int reduction — no branching required.
template <typename Stream, typename T>
error write_le_scalar(Stream& stream, T value) {
	using U = storage_type_t<T>;
	using UInt = std::make_unsigned_t<U>;
	auto bits = std::bit_cast<UInt>(static_cast<U>(value));
	std::byte bytes[sizeof(T)]{};
	for (std::size_t i = 0; i < sizeof(T); ++i)
		bytes[i] = static_cast<std::byte>((bits >> (i * 8u)) & 0xffu);
	return stream.write_bytes(std::span<const std::byte>(bytes, sizeof(T)));
}

template <typename Stream, typename T>
error read_le_scalar(Stream& stream, T& value) {
	using U = storage_type_t<T>;
	using UInt = std::make_unsigned_t<U>;
	std::byte bytes[sizeof(T)]{};
	RTSL_BIN_TRY(stream.read_bytes(std::span<std::byte>(bytes, sizeof(T))));
	UInt bits = 0;
	for (std::size_t i = 0; i < sizeof(T); ++i)
		bits |= static_cast<UInt>(std::to_integer<u08>(bytes[i])) << (i * 8u);
	value = static_cast<T>(std::bit_cast<U>(bits));
	return {};
}

} // namespace detail

// Reader over a caller-owned byte span. Tracks cursor + diagnostic context.
// Truncation and short reads are hard errors — no exceptions, just a message.
struct read_stream {
	using stream_tag = read_stream_tag;

	explicit read_stream(std::span<const std::byte> bytes) : input_(bytes) {}
	explicit read_stream(std::span<const u08> bytes)
		: input_(reinterpret_cast<const std::byte*>(bytes.data()), bytes.size()) {}

	error read_bytes(std::span<std::byte> out) {
		if (out.size() > remaining())
			return error(std::format("read of {} bytes at offset {} exceeds input ({} bytes remain)", out.size(), cursor_, remaining()));
		if (!out.empty()) std::memcpy(out.data(), input_.data() + cursor_, out.size());
		cursor_ += out.size();
		return {};
	}

	error skip(std::size_t count) {
		if (count > remaining())
			return error(std::format("skip of {} bytes at offset {} exceeds input ({} bytes remain)", count, cursor_, remaining()));
		cursor_ += count;
		return {};
	}

	// Zero-copy view into the remaining input. Advances the cursor for nested
	// byte-stream readers.
	error take_view(std::span<const std::byte>& out, std::size_t count) {
		if (count > remaining())
			return error(std::format("view of {} bytes at offset {} exceeds input ({} bytes remain)", count, cursor_, remaining()));
		out = std::span<const std::byte>(input_.data() + cursor_, count);
		cursor_ += count;
		return {};
	}

	std::size_t cursor() const { return cursor_; }
	std::size_t remaining() const { return input_.size() - cursor_; }
	std::size_t size() const { return input_.size(); }
	bool at_end() const { return cursor_ >= input_.size(); }

	std::string_view context_str() const { return context_; }
	void set_context(std::string c) { context_ = std::move(c); }

	// Named-field entry point: `stream(field("name", value), field("y", y))`.
	// Each field pushes a diagnostic scope, dispatches to `process`, and
	// propagates the first error.
	template <typename... Fields>
	error operator()(Fields&&... fields) {
		error err;
		auto step = [&](auto&& f) -> bool {
			detail::context_scope scope(*this, f.name);
			err = process(*this, f.value);
			if (err) detail::add_context_prefix(*this, err);
			return !err;
		};
		(step(std::forward<Fields>(fields)) && ...);
		return err;
	}

  private:
	std::span<const std::byte> input_;
	std::size_t cursor_ = 0;
	std::string context_;
};

// Growing writer over a byte vector with the same field context tracking as
// the reader.
struct write_stream {
	using stream_tag = write_stream_tag;

	write_stream() = default;

	error write_bytes(std::span<const std::byte> in) {
		if (in.empty()) return {};
		output_.insert(output_.end(), reinterpret_cast<const u08*>(in.data()),
			reinterpret_cast<const u08*>(in.data()) + in.size());
		return {};
	}

	error padding(std::size_t count) {
		output_.insert(output_.end(), count, u08{ 0 });
		return {};
	}

	void reserve(std::size_t bytes) { output_.reserve(output_.size() + bytes); }

	std::span<const u08> written() const { return output_; }
	std::vector<u08> take_written() { return std::move(output_); }
	std::size_t size() const { return output_.size(); }

	std::string_view context_str() const { return context_; }
	void set_context(std::string c) { context_ = std::move(c); }

	template <typename... Fields>
	error operator()(Fields&&... fields) {
		error err;
		auto step = [&](auto&& f) -> bool {
			detail::context_scope scope(*this, f.name);
			err = process(*this, f.value);
			if (err) detail::add_context_prefix(*this, err);
			return !err;
		};
		(step(std::forward<Fields>(fields)) && ...);
		return err;
	}

  private:
	std::vector<u08> output_;
	std::string context_;
};

// --- Built-in process overloads ------------------------------------------
// Each type gets ONE overload. Const-ness is carried by the deduced template
// parameter — `Int& value` binds both `u32&` and `const u32&`. The write and
// read bodies are gated by `if constexpr (writable_stream<Stream>)`, so the
// mutation-heavy read branch is never instantiated on a const target. This
// is the lf::bin `data<T, D>` pattern applied uniformly.

namespace detail {
	template <typename T> struct is_std_vector : std::false_type {};
	template <typename E> struct is_std_vector<std::vector<E>> : std::true_type {};
}

template <typename T>
concept vector_data = detail::is_std_vector<std::remove_cvref_t<T>>::value;

template <byte_stream Stream, fixed_integer Int>
error process(Stream& stream, Int& value) {
	if constexpr (writable_stream<Stream>) return detail::write_le_scalar(stream, value);
	else                                   return detail::read_le_scalar(stream, value);
}

template <byte_stream Stream, plain_enum Enum>
error process(Stream& stream, Enum& value) {
	if constexpr (writable_stream<Stream>) return detail::write_le_scalar(stream, value);
	else                                   return detail::read_le_scalar(stream, value);
}

template <byte_stream Stream>
error process(Stream& stream, ir::Id& value) {
	return process(stream, value.value);
}

template <byte_stream Stream>
error process(Stream& stream, const ir::Id& value) {
	return process(stream, value.value);
}

template <byte_stream Stream, data<bool> Bool>
error process(Stream& stream, Bool& value) {
	if constexpr (writable_stream<Stream>) {
		const u08 byte = value ? 1u : 0u;
		return detail::write_le_scalar(stream, byte);
	} else {
		u08 byte = 0;
		RTSL_BIN_TRY(detail::read_le_scalar(stream, byte));
		if (byte != 0 && byte != 1)
			return error(std::format("bool byte was {}; expected 0 or 1", byte));
		value = byte != 0;
		return {};
	}
}

template <byte_stream Stream, data<std::string> Str>
error process(Stream& stream, Str& value) {
	if constexpr (writable_stream<Stream>) {
		RTSL_BIN_TRY(detail::write_le_scalar(stream, static_cast<u32>(value.size())));
		return stream.write_bytes(std::span<const std::byte>(
			reinterpret_cast<const std::byte*>(value.data()), value.size()));
	} else {
		u32 length = 0;
		RTSL_BIN_TRY(detail::read_le_scalar(stream, length));
		if (length > stream.remaining())
			return error(std::format("string length {} exceeds remaining input {}", length, stream.remaining()));
		value.assign(length, '\0');
		return stream.read_bytes(std::span<std::byte>(
			reinterpret_cast<std::byte*>(value.data()), length));
	}
}

// vector<T> — length-prefixed count, then per-element process(). For u08 the
// per-element write reduces to a 1-byte scalar write, which is what a
// dedicated bulk overload would do anyway. No special case needed; the loop
// is the general rule and vector<u08> falls out of it.
template <byte_stream Stream, vector_data Vec>
error process(Stream& stream, Vec& value) {
	if constexpr (writable_stream<Stream>) {
		RTSL_BIN_TRY(detail::write_le_scalar(stream, static_cast<u32>(value.size())));
		for (std::size_t i = 0; i < value.size(); ++i) {
			detail::context_scope scope(stream, std::format("[{}]", i));
			if (auto err = process(stream, value[i]); err) {
				detail::add_context_prefix(stream, err);
				return err;
			}
		}
		return {};
	} else {
		u32 count = 0;
		RTSL_BIN_TRY(detail::read_le_scalar(stream, count));
		if (count > stream.remaining())
			return error(std::format("element count {} exceeds remaining input {}", count, stream.remaining()));
		value.clear();
		value.resize(count);
		for (u32 i = 0; i < count; ++i) {
			detail::context_scope scope(stream, std::format("[{}]", i));
			if (auto err = process(stream, value[i]); err) {
				detail::add_context_prefix(stream, err);
				return err;
			}
		}
		return {};
	}
}

template <byte_stream Stream, typename T>
error process(Stream& stream, std::optional<T>& value) {
	if constexpr (writable_stream<Stream>) {
		bool present = value.has_value();
		RTSL_BIN_TRY(process(stream, present));
		if (present) return process(stream, *value);
		return {};
	} else {
		bool present = false;
		RTSL_BIN_TRY(process(stream, present));
		if (!present) {
			value.reset();
			return {};
		}
		value.emplace();
		return process(stream, *value);
	}
}

// --- Top-level entry points ----------------------------------------------

// std::expected propagates any error surfaced by a process() overload.
// Silently returning the partial byte buffer would mask a real failure
// (invariant checks in user-defined overloads, size overflows on write, ...).
template <typename T>
std::expected<std::vector<u08>, error> write(const T& value) {
	write_stream stream;
	if (auto err = process(stream, value); err) return std::unexpected(std::move(err));
	return stream.take_written();
}

template <typename T>
error read(std::span<const u08> bytes, T& value) {
	read_stream stream(bytes);
	if (auto err = process(stream, value); err) return err;
	if (!stream.at_end())
		return error(std::format("read finished with {} trailing bytes", stream.remaining()));
	return {};
}

} // namespace rtsl::bin
