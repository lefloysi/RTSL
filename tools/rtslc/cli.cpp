#include <CLI/CLI.hpp>

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace rtslc_cli {
	struct Arguments {
		std::vector<std::string> raw;
		std::vector<std::string> sources;
		std::vector<std::string> libraries;
		std::vector<std::string> interfaces;
		std::string standard;
		std::string output;
		std::string module;
		std::string interface_output;
		int kind = 0;
		bool emit = false;
	};

	Arguments args;
}

extern "C" void rtslc_cli_reset() {
	rtslc_cli::args = {};
}

extern "C" void rtslc_cli_add(const char* text, std::size_t length) {
	rtslc_cli::args.raw.emplace_back(text, length);
}

extern "C" int rtslc_cli_parse() {
	auto& args = rtslc_cli::args;
	CLI::App app{"RTSL compiler frontend"};
	auto add_sources = [&args](const std::vector<std::string>& values) {
		args.sources.insert(args.sources.end(), values.begin(), values.end());
	};
	app.add_option_function<std::vector<std::string>>("input", add_sources, "RTSL source files")->trigger_on_parse();
	app.add_option_function<std::string>("--source", [&args](const std::string& value) {
		args.sources.push_back(value);
	}, "Additional RTSL source file")->trigger_on_parse();
	app.add_option("--std", args.standard, "RTSL standard library directory");
	app.add_option_function<std::string>("--interface", [&args](const std::string& value) {
		args.interfaces.push_back(value);
	}, "Module interface")->trigger_on_parse();
	app.add_option("--emit-interface", args.interface_output, "Output module interface");
	app.add_option_function<std::string>("--library,--object", [&args](const std::string& value) {
		args.libraries.push_back(value);
	}, "Object or module artifact")->trigger_on_parse();
	app.add_option("-m,--module", args.module, "Module name");
	app.add_option("-o,--output", args.output, "Output artifact path");
	app.add_flag_function("--emit-program", [&args](std::int64_t) {
		args.emit = true;
		args.kind = 0;
	}, "Emit a linked program")->trigger_on_parse();
	app.add_flag_function("--emit-module", [&args](std::int64_t) {
		args.emit = true;
		args.kind = 1;
	}, "Emit a module")->trigger_on_parse();
	app.add_flag_function("--emit-object", [&args](std::int64_t) {
		args.emit = true;
		args.kind = 2;
	}, "Emit an object")->trigger_on_parse();
	std::vector<const char*> argv{"rtslc"};
	for (const auto& arg : args.raw) {
		argv.push_back(arg.c_str());
	}
	try {
		app.parse(static_cast<int>(argv.size()), argv.data());
	} catch (const CLI::ParseError& error) {
		return app.exit(error) == 0 ? 1 : -1;
	}
	return 0;
}

extern "C" const char* rtslc_cli_value(int field, std::size_t index) {
	auto& args = rtslc_cli::args;
	switch (field) {
	case 0: return args.standard.c_str();
	case 1: return args.output.c_str();
	case 2: return args.module.c_str();
	case 3: return args.interface_output.c_str();
	case 4: return index < args.sources.size() ? args.sources[index].c_str() : "";
	case 5: return index < args.libraries.size() ? args.libraries[index].c_str() : "";
	case 6: return index < args.interfaces.size() ? args.interfaces[index].c_str() : "";
	default: return "";
	}
}

extern "C" std::size_t rtslc_cli_count(int field) {
	auto& args = rtslc_cli::args;
	switch (field) {
	case 4: return args.sources.size();
	case 5: return args.libraries.size();
	case 6: return args.interfaces.size();
	default: return 0;
	}
}

extern "C" int rtslc_cli_kind() {
	return rtslc_cli::args.kind;
}

extern "C" bool rtslc_cli_emit() {
	return rtslc_cli::args.emit || !rtslc_cli::args.output.empty();
}
