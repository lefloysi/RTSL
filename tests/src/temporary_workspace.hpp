#pragma once

#include <atomic>
#include <chrono>
#include <filesystem>
#include <string>
#include <string_view>

namespace rtsl::test {

	class TemporaryWorkspace {
	public:
		explicit TemporaryWorkspace(std::string_view name) {
			std::error_code error;
			const auto root = std::filesystem::temp_directory_path(error);
			if (error)
				return;
			const auto timestamp = std::chrono::steady_clock::now().time_since_epoch().count();
			for (unsigned attempt = 0; attempt != 256; ++attempt) {
				const auto suffix = next_suffix_.fetch_add(1, std::memory_order_relaxed);
				path_ = root / ("rtsl-" + std::string(name) + "-" + std::to_string(timestamp) + "-" + std::to_string(suffix));
				error.clear();
				if (std::filesystem::create_directory(path_, error))
					created_ = true;
				if (created_)
					return;
			}
		}

		TemporaryWorkspace(const TemporaryWorkspace&) = delete;
		TemporaryWorkspace& operator=(const TemporaryWorkspace&) = delete;

		~TemporaryWorkspace() {
			if (!created_)
				return;
			std::error_code error;
			std::filesystem::remove_all(path_, error);
		}

		bool valid() const { return created_; }
		const std::filesystem::path& path() const { return path_; }

	private:
		inline static std::atomic_uint next_suffix_ = 0;
		std::filesystem::path path_;
		bool created_ = false;
	};

} // namespace rtsl::test
