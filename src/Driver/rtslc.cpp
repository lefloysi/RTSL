#include "rtsl.h"

#include "Basic/Diagnostics.h"
#include "Compiler/Compiler.h"
#include "Link/Linker.h"
#include "Serialization/Artifact.h"
#include "Serialization/TextRTIR.h"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

namespace {

std::vector<std::uint8_t> read_file(const std::string &path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        return {};
    }
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

bool write_bytes(const std::string &path, const std::vector<rtsl::u8> &bytes) {
    std::ofstream output(path, std::ios::binary);
    if (!output) {
        return false;
    }
    output.write(reinterpret_cast<const char *>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    return output.good();
}

void print_engine_diagnostics(const rtsl::DiagnosticEngine &diagnostics) {
    for (const auto &diagnostic : diagnostics.diagnostics()) {
        std::cerr << diagnostic.source_name << ':' << diagnostic.location.line << ':'
                  << diagnostic.location.column << ": " << diagnostic.message << '\n';
    }
}

// Replace the extension of `path` with `new_ext` (which should include the
// leading dot, e.g. ".rtslm"). Used to derive sidecar paths from a primary
// output path so the user only specifies one `-o`.
std::string with_extension(std::string_view path, std::string_view new_ext) {
    std::filesystem::path p{std::string(path)};
    p.replace_extension(std::string(new_ext));
    return p.string();
}

// Subcommand handlers. Each returns a process exit code (0 = success).

struct ParsedArgs {
    std::vector<std::string> positional;
    std::string output;
};

// Pull a single -o <value> out of argv. Everything else becomes positional.
// Returns false if -o is malformed (e.g. trailing -o with no value).
bool parse_args(int argc, char **argv, ParsedArgs &out) {
    for (int i = 2; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "-o") {
            if (i + 1 >= argc) return false;
            out.output = argv[++i];
        } else {
            out.positional.push_back(arg);
        }
    }
    return true;
}

void print_usage() {
    std::cerr <<
        "usage: rtslc <command> [args...]\n"
        "\n"
        "commands:\n"
        "  compile      <input.rtsl> -o <out.rtslo>\n"
        "               Compile one source file to an object. If any decl is\n"
        "               marked `export`, also writes <out>.rtslm next to it.\n"
        "\n"
        "  link-program <inputs>... -o <out.rtslp>\n"
        "               Link one or more .rtslo/.rtsll inputs into a runnable\n"
        "               program. At least one stage entry point is required.\n"
        "\n"
        "  link-library <inputs>... -o <out.rtsll>\n"
        "               Link inputs into a library (no entry points required).\n"
        "               If any exports survive, also writes <out>.rtslm.\n"
        "\n"
        "  dump         <input>\n"
        "               Print the textual RTIR of any artifact kind.\n"
        "\n"
        "  assemble     <input.rtir> -o <out.rtslo>\n"
        "               Assemble textual RTIR to a binary object.\n"
        "\n"
        "  help [<command>]\n"
        "               Show this message.\n";
}

int cmd_compile(int argc, char **argv) {
    ParsedArgs args;
    if (!parse_args(argc, argv, args) || args.positional.size() != 1 || args.output.empty()) {
        std::cerr << "usage: rtslc compile <input.rtsl> -o <out.rtslo>\n";
        return 1;
    }
    const auto source = read_file(args.positional.front());
    if (source.empty()) {
        std::cerr << "rtslc: failed to read " << args.positional.front() << '\n';
        return 1;
    }

    rtsl::CompilerInstance compiler;
    rtsl::CompilerInvocation invocation{.source_name = args.positional.front()};
    rtsl::Artifact artifact;
    compiler.compile_source_to(
        artifact,
        std::string_view(reinterpret_cast<const char *>(source.data()), source.size()),
        std::move(invocation));

    if (compiler.diagnostics().has_error() || artifact.bytes.empty()) {
        print_engine_diagnostics(compiler.diagnostics());
        return 1;
    }
    if (!write_bytes(args.output, artifact.bytes)) {
        std::cerr << "rtslc: failed to write " << args.output << '\n';
        return 1;
    }

    // Auto-emit .rtslm sidecar if the source declared any exports. Importers
    // can then pick up the public interface without seeing the full object.
    const rtsl::Artifact module_artifact = rtsl::extract_module_interface(artifact);
    if (!module_artifact.bytes.empty()) {
        const std::string module_path = with_extension(args.output, ".rtslm");
        if (!write_bytes(module_path, module_artifact.bytes)) {
            std::cerr << "rtslc: failed to write " << module_path << '\n';
            return 1;
        }
    }
    return 0;
}

int cmd_link(int argc, char **argv, bool produce_program) {
    ParsedArgs args;
    if (!parse_args(argc, argv, args) || args.positional.empty() || args.output.empty()) {
        const char *cmd = produce_program ? "link-program" : "link-library";
        const char *ext = produce_program ? "out.rtslp" : "out.rtsll";
        std::cerr << "usage: rtslc " << cmd << " <inputs>... -o <" << ext << ">\n";
        return 1;
    }

    rtsl::DiagnosticEngine diagnostics;
    rtsl::Linker linker(diagnostics);
    for (const auto &input_path : args.positional) {
        const auto bytes = read_file(input_path);
        if (bytes.empty()) {
            std::cerr << "rtslc: failed to read " << input_path << '\n';
            return 1;
        }
        if (!linker.add_artifact_bytes(std::span<const rtsl::u8>(bytes.data(), bytes.size()))) {
            print_engine_diagnostics(diagnostics);
            return 1;
        }
    }

    rtsl::Artifact linked = produce_program ? linker.link_program() : linker.link_library();
    if (diagnostics.has_error() || linked.bytes.empty()) {
        print_engine_diagnostics(diagnostics);
        return 1;
    }
    if (!write_bytes(args.output, linked.bytes)) {
        std::cerr << "rtslc: failed to write " << args.output << '\n';
        return 1;
    }

    // Libraries get the same auto-sidecar treatment as compile output.
    // A program is the end of the line - its module interface would only
    // describe entry points the user already knows about - so we skip it.
    if (!produce_program) {
        const rtsl::Artifact module_artifact = rtsl::extract_module_interface(linked);
        if (!module_artifact.bytes.empty()) {
            const std::string module_path = with_extension(args.output, ".rtslm");
            if (!write_bytes(module_path, module_artifact.bytes)) {
                std::cerr << "rtslc: failed to write " << module_path << '\n';
                return 1;
            }
        }
    }
    return 0;
}

int cmd_dump(int argc, char **argv) {
    ParsedArgs args;
    if (!parse_args(argc, argv, args) || args.positional.size() != 1) {
        std::cerr << "usage: rtslc dump <input>\n";
        return 1;
    }
    const auto bytes = read_file(args.positional.front());
    rtsl::Artifact artifact;
    rtsl::DiagnosticEngine diagnostics;
    if (bytes.empty() || !rtsl::read_artifact(bytes, artifact, &diagnostics)) {
        std::cerr << "rtslc: failed to read artifact " << args.positional.front() << '\n';
        print_engine_diagnostics(diagnostics);
        return 1;
    }
    std::cout << rtsl::disassemble_artifact(artifact);
    return 0;
}

int cmd_assemble(int argc, char **argv) {
    ParsedArgs args;
    if (!parse_args(argc, argv, args) || args.positional.size() != 1 || args.output.empty()) {
        std::cerr << "usage: rtslc assemble <input.rtir> -o <out.rtslo>\n";
        return 1;
    }
    const auto bytes = read_file(args.positional.front());
    rtsl::Artifact artifact;
    rtsl::DiagnosticEngine diagnostics;
    const std::string text(reinterpret_cast<const char *>(bytes.data()), bytes.size());
    if (bytes.empty() || !rtsl::assemble_text_rtir(text, artifact, &diagnostics)) {
        std::cerr << "rtslc: failed to assemble RTIR " << args.positional.front() << '\n';
        print_engine_diagnostics(diagnostics);
        return 1;
    }
    if (!write_bytes(args.output, artifact.bytes)) {
        std::cerr << "rtslc: failed to write " << args.output << '\n';
        return 1;
    }
    return 0;
}

} // namespace

int main(int argc, char **argv) {
    if (argc < 2) {
        print_usage();
        return 1;
    }
    const std::string command = argv[1];

    if (command == "help" || command == "--help" || command == "-h") {
        print_usage();
        return 0;
    }
    if (command == "compile") return cmd_compile(argc, argv);
    if (command == "link-program") return cmd_link(argc, argv, /*produce_program=*/true);
    if (command == "link-library") return cmd_link(argc, argv, /*produce_program=*/false);
    if (command == "dump") return cmd_dump(argc, argv);
    if (command == "assemble") return cmd_assemble(argc, argv);

    std::cerr << "rtslc: unknown command '" << command << "'\n";
    print_usage();
    return 1;
}
