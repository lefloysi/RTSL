#include "Emit/SpirvEmitter.hpp"

#include <spirv/unified1/spirv.h>

#include <algorithm>
#include <span>
#include <string>
#include <unordered_set>
#include <vector>

namespace rt {
namespace {

struct SpvWriter {
    std::vector<u32> words;
    u32 bound = 1;
};

static void begin(SpvWriter &w) {
    w.words = {SpvMagicNumber, SpvVersion, 0, 0, 0};
}

static void emit(SpvWriter &w, SpvOp op, const std::vector<u32> &ops) {
    w.words.push_back(((1u + static_cast<u32>(ops.size())) << SpvWordCountShift) | static_cast<u32>(op));
    w.words.insert(w.words.end(), ops.begin(), ops.end());
}

static void emit(SpvWriter &w, SpvOp op, std::initializer_list<u32> ops) {
    emit(w, op, std::vector<u32>(ops));
}

static void reserve(SpvWriter &w, u32 id) {
    w.bound = std::max(w.bound, id + 1);
}

static u32 fresh(SpvWriter &w) {
    return w.bound++;
}

static const RTFunction *find_stage_function(const RTArtifactModule &module, RTStageKind stage) {
    for (const auto &fn : module.functions) {
        if (fn.stage == stage) {
            return &fn;
        }
    }
    return nullptr;
}

static bool has_type_id(const RTArtifactModule &module, u32 type_id) {
    if (!type_id) return false;
    for (const auto &inst : module.type_constant_pool) {
        if (inst.result_id == type_id) return true;
    }
    return false;
}

static bool validate_type_refs(const RTArtifactModule &module, std::string *err) {
    for (const auto &inst : module.type_constant_pool) {
        for (u32 operand : inst.operands) {
            if (!has_type_id(module, operand)) {
                if (err) *err = "dangling RTSLP type reference " + std::to_string(operand);
                return false;
            }
        }
    }
    return true;
}

static bool is_type_op(RTIROp op) {
    return op >= RTIROp::TypeVoid && op <= RTIROp::TypeSampledImage;
}

static std::vector<u32> make_words(std::initializer_list<u32> ops) {
    return std::vector<u32>(ops.begin(), ops.end());
}

static bool emit_stage(const RTArtifactModule &module, RTStageKind stage, std::vector<u32> *out) {
    const RTFunction *fn = find_stage_function(module, stage);
    if (!fn) return false;

    std::string err;
    if (!validate_type_refs(module, &err)) {
        (void)err;
        return false;
    }

    SpvWriter w;
    begin(w);
    std::vector<u32> entry_ops;
    std::vector<u32> decl_ops;
    std::vector<u32> fn_ops;

    emit(w, SpvOpCapability, {SpvCapabilityShader});
    emit(w, SpvOpMemoryModel, {SpvAddressingModelLogical, SpvMemoryModelGLSL450});

    for (const auto &inst : module.type_constant_pool) {
        if (inst.result_id) reserve(w, inst.result_id);
    }
    for (const auto &func : module.functions) {
        if (func.result_id) reserve(w, func.result_id);
        for (const auto &inst : func.body) {
            if (inst.result_id) reserve(w, inst.result_id);
        }
    }

    u32 void_type = 0;
    for (const auto &inst : module.type_constant_pool) {
        if (inst.op == RTIROp::TypeVoid) {
            void_type = inst.result_id;
            break;
        }
    }
    if (!void_type) return false;

    for (const auto &inst : module.type_constant_pool) {
        if (!inst.result_id || !is_type_op(inst.op)) continue;
        switch (inst.op) {
        case RTIROp::TypeVoid:
            emit(w, SpvOpTypeVoid, {inst.result_id});
            break;
        case RTIROp::TypeBool:
            emit(w, SpvOpTypeBool, {inst.result_id});
            break;
        case RTIROp::TypeInt:
            emit(w, SpvOpTypeInt, {inst.result_id, 1, inst.literals.empty() ? 32u : inst.literals[0]});
            break;
        case RTIROp::TypeUInt:
            emit(w, SpvOpTypeInt, {inst.result_id, 0, inst.literals.empty() ? 32u : inst.literals[0]});
            break;
        case RTIROp::TypeFloat:
            emit(w, SpvOpTypeFloat, {inst.result_id, inst.literals.empty() ? 32u : inst.literals[0]});
            break;
        case RTIROp::TypeVector:
            if (!inst.operands.empty() && !inst.literals.empty()) emit(w, SpvOpTypeVector, {inst.result_id, inst.operands[0], inst.literals[0]});
            break;
        case RTIROp::TypeMatrix:
            if (!inst.operands.empty() && !inst.literals.empty()) emit(w, SpvOpTypeMatrix, {inst.result_id, inst.operands[0], inst.literals[0]});
            break;
        case RTIROp::TypeStruct: {
            std::vector<u32> ops{inst.result_id};
            ops.insert(ops.end(), inst.operands.begin(), inst.operands.end());
            emit(w, SpvOpTypeStruct, ops);
            break;
        }
        case RTIROp::TypePointer:
            if (!inst.operands.empty() && !inst.literals.empty()) emit(w, SpvOpTypePointer, {inst.result_id, inst.literals[0], inst.operands[0]});
            break;
        default:
            break;
        }
    }

    const u32 entry_id = fresh(w);
    entry_ops.push_back(((1u + 4u) << SpvWordCountShift) | static_cast<u32>(SpvOpEntryPoint));
    {
        const auto words = make_words({
        static_cast<u32>(stage == RTStageKind::Vertex ? SpvExecutionModelVertex : SpvExecutionModelFragment),
        entry_id,
        0x6e69616d,
        0
        });
        entry_ops.insert(entry_ops.end(), words.begin(), words.end());
    }

    emit(w, SpvOpFunction, {void_type, entry_id, 0, fresh(w)});
    emit(w, SpvOpLabel, {fresh(w)});
    emit(w, SpvOpReturn, {});
    emit(w, SpvOpFunctionEnd, {});

    w.words.insert(w.words.end(), entry_ops.begin(), entry_ops.end());
    w.words.insert(w.words.end(), decl_ops.begin(), decl_ops.end());
    w.words.insert(w.words.end(), fn_ops.begin(), fn_ops.end());
    w.words[3] = w.bound;
    *out = std::move(w.words);
    return true;
}

} // namespace

bool emit_rtslp_stage_spirv(const RTArtifactModule &module, RTStageKind stage, std::vector<u32> *spirv_out, std::string *error_out) {
    (void)error_out;
    return emit_stage(module, stage, spirv_out);
}

} // namespace rt
