#include <rtsl/Frontend/ModuleInterfaceImporter.hpp>

#include <rtsl/Sema/Sema.hpp>

namespace rtsl {
namespace {

ParsedType makeType(const InterfaceType& source, IdentifierTable& identifiers) {
	ParsedType result;
	result.Constant = source.constant;
	switch (source.kind) {
	case InterfaceTypeKind::type_void: result.Name = &identifiers.get("void"); break;
	case InterfaceTypeKind::type_bool: result.Name = &identifiers.get("bool"); break;
	case InterfaceTypeKind::type_i32: result.Name = &identifiers.get("i32"); break;
	case InterfaceTypeKind::type_u32: result.Name = &identifiers.get("u32"); break;
	case InterfaceTypeKind::type_usize: result.Name = &identifiers.get("usize"); break;
	case InterfaceTypeKind::type_f32: result.Name = &identifiers.get("f32"); break;
	case InterfaceTypeKind::type_named:
	case InterfaceTypeKind::type_template_parameter:
	case InterfaceTypeKind::type_template_specialization: result.Name = &identifiers.get(source.name); break;
	case InterfaceTypeKind::type_pointer:
	case InterfaceTypeKind::type_reference:
		if (source.arguments.size() == 1) result = makeType(source.arguments[0], identifiers);
		result.Pointer = source.kind == InterfaceTypeKind::type_pointer;
		result.Reference = source.kind == InterfaceTypeKind::type_reference;
		return result;
	}
	for (const InterfaceType& argument : source.arguments) {
		auto parsed = makeType(argument, identifiers);
		parsed.IntegerValue = argument.integer_value;
		result.Arguments.push_back(std::move(parsed));
	}
	return result;
}

ParsedAttributes makeAttributes(const std::vector<InterfaceAttribute>& source, IdentifierTable& identifiers) {
	ParsedAttributes result;
	for (const InterfaceAttribute& attribute : source) {
		ParsedAttr parsed{.Name = &identifiers.get(attribute.name)};
		for (const InterfaceAttributeToken& source_token : attribute.tokens) {
			Token token;
			token.setKind(static_cast<tok::TokenKind>(source_token.kind));
			token.setLength(static_cast<unsigned>(source_token.spelling.size()));
			if (token.is(tok::identifier) || (token.getKind() >= tok::kw_import && token.getKind() <= tok::kw_typename))
				token.setIdentifierInfo(&identifiers.get(source_token.spelling, token.getKind()));
			else if (token.is(tok::numeric_literal) || token.is(tok::string_literal)) token.setLiteralData(source_token.spelling.data());
			parsed.Tokens.push_back(token);
		}
		result.add(std::move(parsed));
	}
	return result;
}

} // namespace

std::vector<std::string> ModuleInterfaceImporter::import(const ModuleInterface& interface, Sema& sema) const {
	std::vector<std::string> diagnostics;
	IdentifierTable& identifiers = sema.getIdentifierTable();
	DeclContext* context = sema.getASTContext().getTranslationUnitDecl();
	for (const InterfaceUnit& unit : interface.units) for (const InterfaceDeclaration& declaration : unit.declarations) {
		std::visit([&](const auto& value) {
			using T = std::decay_t<decltype(value)>;
			if constexpr (std::is_same_v<T, InterfaceRecord>) {
				auto* record = sema.actOnStartRecord(context, &identifiers.get(value.name), {}, true, false, false, makeAttributes(value.attributes, identifiers));
				if (value.base_type) record->setBaseType(sema.actOnType(makeType(*value.base_type, identifiers)));
				for (const InterfaceField& field : value.fields) {
					Declarator declarator{.Name = &identifiers.get(field.name), .Type = makeType(field.type, identifiers)};
					sema.actOnField(record, declarator, makeAttributes(field.attributes, identifiers));
				}
			} else if constexpr (std::is_same_v<T, InterfaceTypeAlias>) {
				DeclSpec spec;
				sema.actOnTypeAlias(context, &identifiers.get(value.name), {}, spec, makeType(value.type, identifiers), makeAttributes(value.attributes, identifiers));
			} else if constexpr (std::is_same_v<T, InterfaceVariable>) {
				DeclSpec spec; spec.Storage = static_cast<StorageClass>(value.storage); spec.Constant = value.constant;
				Declarator declarator{.Name = &identifiers.get(value.name), .Type = makeType(value.type, identifiers)};
				sema.actOnVariable(context, spec, declarator, nullptr, makeAttributes(value.attributes, identifiers));
			} else if constexpr (std::is_same_v<T, InterfaceFunction>) {
				std::vector<IdentifierInfo*> templates;
				for (const std::string& parameter : value.template_parameters) templates.push_back(&identifiers.get(parameter));
				sema.pushTemplateParameters(templates);
				std::vector<ParmVarDecl*> parameters;
				for (const InterfaceParameter& parameter : value.parameters) {
					Declarator parameter_decl{.Name = &identifiers.get(parameter.name), .Type = makeType(parameter.type, identifiers)};
					parameters.push_back(sema.actOnParameter(context, parameter_decl, makeAttributes(parameter.attributes, identifiers)));
				}
				std::vector<ParsedType> type_only_parameters;
				for (const InterfaceType& parameter : value.type_only_parameters)
					type_only_parameters.push_back(makeType(parameter, identifiers));
				DeclSpec spec;
				Declarator declarator{.Name = &identifiers.get(value.name), .Type = makeType(value.return_type, identifiers), .Emits = value.implicit_emitter};
				sema.actOnFunction(context, spec, declarator, parameters, {}, nullptr, makeAttributes(value.attributes, identifiers), templates, type_only_parameters);
				sema.popTemplateParameters(templates);
			}
		}, declaration);
	}
	return diagnostics;
}

} // namespace rtsl
