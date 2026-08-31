#include <rtsl/Frontend/ModuleInterfaceBuilder.hpp>

#include <rtsl/Basic/TokenKinds.hpp>

namespace rtsl {

InterfaceType ModuleInterfaceBuilder::buildType(QualType type) const {
	InterfaceType result;
	result.constant = type.isConstQualified();
	const Type* source = type.getTypePtr();
	if (!source) return result;
	switch (source->getTypeClass()) {
	case TypeClass::type_builtin: {
		switch (static_cast<const BuiltinType*>(source)->getKind()) {
		case BuiltinTypeKind::builtin_void: result.kind = InterfaceTypeKind::type_void; break;
		case BuiltinTypeKind::builtin_bool: result.kind = InterfaceTypeKind::type_bool; break;
		case BuiltinTypeKind::builtin_i32: result.kind = InterfaceTypeKind::type_i32; break;
		case BuiltinTypeKind::builtin_u32: result.kind = InterfaceTypeKind::type_u32; break;
		case BuiltinTypeKind::builtin_usize: result.kind = InterfaceTypeKind::type_usize; break;
		case BuiltinTypeKind::builtin_f32: result.kind = InterfaceTypeKind::type_f32; break;
		}
		break;
	}
	case TypeClass::type_named:
		result.kind = InterfaceTypeKind::type_named;
		result.name = static_cast<const NamedType*>(source)->getName()->getName();
		break;
	case TypeClass::type_template_parameter:
		result.kind = InterfaceTypeKind::type_template_parameter;
		result.name = static_cast<const TemplateParameterType*>(source)->getName()->getName();
		break;
	case TypeClass::type_template_specialization: {
		const auto* specialization = static_cast<const TemplateSpecializationType*>(source);
		result.kind = InterfaceTypeKind::type_template_specialization;
		result.name = specialization->getName()->getName();
		for (unsigned index = 0; index < specialization->getArgumentCount(); ++index)
			result.arguments.push_back(buildType(specialization->arguments()[index]));
		break;
	}
	case TypeClass::type_pointer:
		result.kind = InterfaceTypeKind::type_pointer;
		result.arguments.push_back(buildType(static_cast<const PointerType*>(source)->getPointeeType()));
		break;
	case TypeClass::type_reference:
		result.kind = InterfaceTypeKind::type_reference;
		result.arguments.push_back(buildType(static_cast<const ReferenceType*>(source)->getPointeeType()));
		break;
	}
	return result;
}

std::vector<InterfaceAttribute> ModuleInterfaceBuilder::buildAttributes(const Attr* attributes) const {
	std::vector<InterfaceAttribute> result;
	for (const Attr* attribute = attributes; attribute; attribute = attribute->getNextAttr()) {
		InterfaceAttribute exported{.name = std::string(attribute->getName()->getName())};
		for (unsigned index = 0; index < attribute->getTokenCount(); ++index) {
			const AttrToken& token = attribute->tokens()[index];
			InterfaceAttributeToken exported_token{.kind = static_cast<std::uint16_t>(token.Kind)};
			if (token.Identifier) exported_token.spelling = token.Identifier->getName();
			else if (token.LiteralData) exported_token.spelling.assign(token.LiteralData, token.LiteralLength);
			else if (tok::isPunctuator(token.Kind)) exported_token.spelling = tok::getPunctuatorSpelling(token.Kind);
			else exported_token.spelling = tok::getTokenName(token.Kind);
			exported.tokens.push_back(std::move(exported_token));
		}
		result.push_back(std::move(exported));
	}
	return result;
}

ModuleInterfaceBuildResult ModuleInterfaceBuilder::buildUnit(std::string_view import_path,
	const TranslationUnitDecl& translation_unit) const {
	ModuleInterfaceBuildResult result;
	result.unit.import_path = import_path;
	for (const Decl* declaration = translation_unit.declsBegin(); declaration;
		declaration = declaration->getNextDeclInContext()) {
		if (declaration->getKind() == DeclKind::decl_import) {
			const auto* import = static_cast<const ImportDecl*>(declaration);
			result.unit.imports.push_back({
				.kind = import->getImportKind() == ImportDecl::Kind::import_file
					? InterfaceImportKind::import_file : InterfaceImportKind::import_library,
				.name = std::string(import->getModuleName()),
			});
			continue;
		}
		switch (declaration->getKind()) {
		case DeclKind::decl_record: {
			const auto* record = static_cast<const RecordDecl*>(declaration);
			if (!record->isExported()) break;
			InterfaceRecord exported{.name = std::string(record->getIdentifier()->getName()), .attributes = buildAttributes(record->getAttrs())};
			if (record->getBaseType()) exported.base_type = buildType(record->getBaseType());
			for (const Decl* member = record->declsBegin(); member; member = member->getNextDeclInContext()) {
				if (member->getKind() != DeclKind::decl_field) continue;
				const auto* field = static_cast<const FieldDecl*>(member);
				exported.fields.push_back({.name = std::string(field->getIdentifier()->getName()), .attributes = buildAttributes(field->getAttrs()), .type = buildType(field->getType())});
			}
			result.unit.declarations.emplace_back(std::move(exported));
			break;
		}
		case DeclKind::decl_type_alias: {
			const auto* alias = static_cast<const TypeAliasDecl*>(declaration);
			if (alias->isExported()) result.unit.declarations.emplace_back(InterfaceTypeAlias{
				.name = std::string(alias->getIdentifier()->getName()), .attributes = buildAttributes(alias->getAttrs()), .type = buildType(alias->getAliasedType())});
			break;
		}
		case DeclKind::decl_variable: {
			const auto* variable = static_cast<const VarDecl*>(declaration);
			if (variable->isExported()) result.unit.declarations.emplace_back(InterfaceVariable{
				.name = std::string(variable->getIdentifier()->getName()), .attributes = buildAttributes(variable->getAttrs()), .type = buildType(variable->getType()),
				.storage = static_cast<InterfaceStorageClass>(variable->getStorageClass()), .constant = variable->isConstant()});
			break;
		}
		case DeclKind::decl_function: {
			const auto* function = static_cast<const FunctionDecl*>(declaration);
			if (!function->isExported()) break;
			if (function->isFunctionTemplate() && function->getBody()) {
				result.diagnostics.push_back("module interface serialization for exported generic definitions is not implemented");
				break;
			}
			InterfaceFunction exported{.name = std::string(function->getIdentifier()->getName()), .attributes = buildAttributes(function->getAttrs()),
				.return_type = buildType(function->getType()), .implicit_emitter = function->hasImplicitEmitter(),
				.declaration = function->getBody() == nullptr};
			for (unsigned index = 0; index < function->getNumParams(); ++index) {
				const ParmVarDecl* parameter = function->parameters()[index];
				exported.parameters.push_back({.name = std::string(parameter->getIdentifier()->getName()), .attributes = buildAttributes(parameter->getAttrs()), .type = buildType(parameter->getType())});
			}
			for (unsigned index = 0; index < function->getNumTemplateParameters(); ++index)
				exported.template_parameters.push_back(std::string(function->templateParameters()[index]->getName()));
			result.unit.declarations.emplace_back(std::move(exported));
			break;
		}
		default: break;
		}
	}
	return result;
}

} // namespace rtsl
