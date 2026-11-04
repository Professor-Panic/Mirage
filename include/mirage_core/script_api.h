#pragma once

#include <algorithm>
#include <cstdint>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "mirage_core/reflection.h"

// ---------------------------------------------------------------------------
// Script API metadata
// ---------------------------------------------------------------------------
// This header deliberately has no Sol2 dependency. It describes the public
// Lua-facing API, but does not decide how that API is registered in a runtime.
// LuaBindingBackend consumes these descriptors to create the runtime API.
// Future generators (LuaLS, docs, etc.) consume the same descriptors.

struct ScriptApiParameter
{
    std::string name;
    std::string type = "any";
    bool optional = false;
    bool variadic = false;
    std::string defaultValue;
};

struct ScriptApiCallable
{
    std::string name;
    std::string description;
    std::vector<ScriptApiParameter> parameters;
    std::vector<std::string> returns;
    bool isStatic = false;
    bool customBinding = false;
    std::string bindingNotes;
};

struct ScriptApiProperty
{
    std::string name;
    std::string type = "any";
    std::string description;
    bool readOnly = false;
    bool customBinding = false;
    std::string bindingNotes;
};

struct ScriptApiType
{
    std::string name;       // Lua-visible type name
    std::string cppType;    // diagnostic / generator metadata
    std::string description;
    bool internal = false;  // hidden implementation userdata is not user API
    bool component = false; // logical reflected ECS component type
    std::string ownership = "value";
    std::string lifetime;
    std::vector<std::string> bases;
    std::vector<std::string> operators;
    std::vector<ScriptApiProperty> properties;
    std::vector<ScriptApiCallable> constructors;
    std::vector<ScriptApiCallable> methods;
};

struct ScriptApiTable
{
    std::string name;
    std::string description;
    bool internal = false;
    std::vector<ScriptApiCallable> functions;
};

struct ScriptApiEnumValue
{
    std::string name;
    std::int64_t value = 0;
    std::string description;
};

struct ScriptApiEnum
{
    std::string name;
    std::string cppType;
    std::string description;
    bool internal = false;
    std::vector<ScriptApiEnumValue> values;
};

struct ScriptApiConstant
{
    std::string path;
    std::string type = "any";
    std::string valueExpression;
    std::string description;
    bool internal = false;
};

class ScriptApiRegistry
{
public:
    const std::map<std::string, ScriptApiType>& Types() const { return types_; }
    const std::map<std::string, ScriptApiTable>& Tables() const { return tables_; }
    const std::map<std::string, ScriptApiEnum>& Enums() const { return enums_; }
    const std::map<std::string, ScriptApiConstant>& Constants() const { return constants_; }
    const std::map<std::string, std::vector<ScriptApiCallable>>& GlobalFunctions() const { return globalFunctions_; }

    void DescribeType(ScriptApiType type)
    {
        if (type.name.empty())
            throw std::invalid_argument("Script API type requires a Lua name");

        const std::string name = type.name;
        auto it = types_.find(name);
        if (it == types_.end())
        {
            types_.emplace(name, std::move(type));
            return;
        }

        ScriptApiType& existing = it->second;
        if (!type.cppType.empty() && !existing.cppType.empty() && type.cppType != existing.cppType)
            throw std::logic_error("Conflicting C++ types registered as Lua type '" + name + "'");
        MergeType(existing, type);
    }

    void DescribeTable(ScriptApiTable table)
    {
        if (table.name.empty())
            throw std::invalid_argument("Script API table requires a name");
        const std::string name = table.name;
        auto it = tables_.find(name);
        if (it == tables_.end())
        {
            tables_.emplace(name, std::move(table));
            return;
        }

        if (!table.description.empty() && it->second.description.empty())
            it->second.description = table.description;
        it->second.internal = it->second.internal && table.internal;
        for (const auto& function : table.functions)
            AddCallable(it->second.functions, function);
    }

    void DescribeTableFunction(const std::string& tableName, ScriptApiCallable function)
    {
        if (tableName.empty() || function.name.empty())
            throw std::invalid_argument("Script API table function requires table and function names");
        ScriptApiTable table;
        table.name = tableName;
        DescribeTable(std::move(table));
        AddCallable(tables_.at(tableName).functions, function);
    }

    void DescribeGlobalFunction(const std::string& path, ScriptApiCallable function)
    {
        if (path.empty())
            throw std::invalid_argument("Script API global function requires a path");
        function.name = path;
        AddCallable(globalFunctions_[path], function);
    }

    void DescribeEnum(ScriptApiEnum description)
    {
        if (description.name.empty())
            throw std::invalid_argument("Script API enum requires a Lua name");

        auto [it, inserted] = enums_.try_emplace(description.name, description);
        if (inserted)
            return;

        ScriptApiEnum& existing = it->second;
        if (!existing.cppType.empty() && !description.cppType.empty() &&
            existing.cppType != description.cppType)
            throw std::logic_error("Conflicting C++ enum types registered as Lua enum '" + description.name + "'");
        if (existing.description.empty()) existing.description = description.description;
        existing.internal = existing.internal && description.internal;
        for (const ScriptApiEnumValue& value : description.values)
        {
            const auto found = std::find_if(existing.values.begin(), existing.values.end(),
                [&](const ScriptApiEnumValue& current) { return current.name == value.name; });
            if (found == existing.values.end())
                existing.values.push_back(value);
            else if (found->value != value.value)
                throw std::logic_error("Conflicting value for '" + description.name + "." + value.name + "'");
            else if (found->description.empty())
                found->description = value.description;
        }
    }

    void DescribeConstant(ScriptApiConstant description)
    {
        if (description.path.empty())
            throw std::invalid_argument("Script API constant requires a global path");
        auto [it, inserted] = constants_.try_emplace(description.path, description);
        if (!inserted)
        {
            const ScriptApiConstant& existing = it->second;
            if (existing.type != description.type ||
                (!existing.valueExpression.empty() && !description.valueExpression.empty() &&
                 existing.valueExpression != description.valueExpression))
                throw std::logic_error("Conflicting Script API constant: " + description.path);
            if (it->second.description.empty()) it->second.description = description.description;
            if (it->second.valueExpression.empty()) it->second.valueExpression = description.valueExpression;
            it->second.internal = it->second.internal && description.internal;
        }
    }

    // Only explicitly script-exposed reflected components become Lua API
    // types. Editor/serialization reflection alone is not an exposure grant.
    void ImportReflectedComponents(const Reflection& reflection)
    {
        for (const auto& [registeredName, component] : reflection.All())
        {
            if (!component.scriptExposed)
                continue;

            ScriptApiType type;
            type.name = component.scriptName.empty() ? registeredName : component.scriptName;
            type.cppType = registeredName;
            type.description = component.scriptDescription;
            type.component = true;
            // Every reflected component is returned as a live ComponentHandle
            // userdata at runtime. Preserve that relationship in LuaLS too.
            type.bases.push_back("ComponentHandle");
            type.ownership = "engine-owned";
            type.lifetime = "The component belongs to an EnTT entity. Lua handles resolve it on each access and become unavailable when the entity or component is removed.";

            for (const FieldInfo& field : component.fields)
            {
                if (!field.scriptExposed)
                    continue;
                type.properties.push_back({
                    field.name,
                    FieldTypeName(field.type),
                    field.scriptDescription.empty() ? field.label : field.scriptDescription,
                    field.scriptReadOnly,
                    field.scriptCustomBinding,
                    field.scriptBindingNotes
                });
            }
            DescribeType(std::move(type));
        }
    }

    // Validates the metadata graph without needing a Lua state. Runtime binding
    // runs this after all modules have contributed their declarations; tooling
    // can also invoke it before generating external API definitions.
    std::vector<std::string> ValidateMetadata() const
    {
        std::vector<std::string> errors;
        auto validateCallable = [&errors](const ScriptApiCallable& function, const std::string& owner)
        {
            if (function.name.empty())
                errors.push_back(owner + ": function has no name");
            for (std::size_t i = 0; i < function.parameters.size(); ++i)
            {
                const ScriptApiParameter& parameter = function.parameters[i];
                if (parameter.name.empty())
                    errors.push_back(owner + ": parameter " + std::to_string(i + 1) + " has no name");
                if (parameter.type.empty())
                    errors.push_back(owner + ": parameter '" + parameter.name + "' has no type");
                if (parameter.variadic && i + 1 != function.parameters.size())
                    errors.push_back(owner + ": variadic parameter '" + parameter.name + "' is not last");
            }
            for (const std::string& result : function.returns)
                if (result.empty()) errors.push_back(owner + ": function has an empty return type");
        };

        for (const auto& [name, type] : types_)
        {
            if (name.empty()) errors.push_back("A script type has no Lua name");
            for (const auto& property : type.properties)
            {
                if (property.name.empty()) errors.push_back(name + ": property has no name");
                if (property.type.empty()) errors.push_back(name + "." + property.name + ": property has no type");
            }
            for (const auto& constructor : type.constructors)
                validateCallable(constructor, name + " constructor");
            for (const auto& method : type.methods)
                validateCallable(method, name + "." + method.name);
        }

        for (const auto& [name, table] : tables_)
        {
            if (name.empty()) errors.push_back("A script table has no Lua name");
            for (const auto& function : table.functions)
                validateCallable(function, name + "." + function.name);
        }

        for (const auto& [path, overloads] : globalFunctions_)
            for (const auto& function : overloads)
                validateCallable(function, path);

        for (const auto& [name, enumeration] : enums_)
        {
            std::set<std::string> values;
            for (const auto& value : enumeration.values)
            {
                if (value.name.empty()) errors.push_back(name + ": enum value has no name");
                else if (!values.insert(value.name).second)
                    errors.push_back(name + ": duplicate enum value '" + value.name + "'");
            }
        }

        for (const auto& [path, constant] : constants_)
            if (constant.type.empty()) errors.push_back(path + ": constant has no type");

        return errors;
    }

private:
    static std::string FieldTypeName(FieldType type)
    {
        switch (type)
        {
        case FieldType::Bool: return "boolean";
        case FieldType::Int: return "integer";
        case FieldType::Float: return "number";
        case FieldType::String: return "string";
        case FieldType::Vec2: return "Vector2";
        case FieldType::Vec3: return "Vector3";
        case FieldType::Vec4: return "Vector4";
        case FieldType::Color: return "Color";
        case FieldType::Entity: return "integer";
        case FieldType::EntityRef: return "EntityRef";
        }
        return "any";
    }

    static bool SameSignature(const ScriptApiCallable& a, const ScriptApiCallable& b)
    {
        if (a.parameters.size() != b.parameters.size()) return false;
        for (std::size_t i = 0; i < a.parameters.size(); ++i)
        {
            if (a.parameters[i].type != b.parameters[i].type ||
                a.parameters[i].optional != b.parameters[i].optional ||
                a.parameters[i].variadic != b.parameters[i].variadic)
                return false;
        }
        return true;
    }

    static void MergeCallable(ScriptApiCallable& destination, const ScriptApiCallable& source)
    {
        if (destination.description.empty()) destination.description = source.description;
        if (destination.returns.empty()) destination.returns = source.returns;
        else if (!source.returns.empty() && destination.returns != source.returns)
            throw std::logic_error("Conflicting return types for Script API function '" + destination.name + "'");
        destination.isStatic = destination.isStatic || source.isStatic;
        destination.customBinding = destination.customBinding || source.customBinding;
        if (destination.bindingNotes.empty()) destination.bindingNotes = source.bindingNotes;
        for (std::size_t i = 0; i < destination.parameters.size(); ++i)
        {
            ScriptApiParameter& target = destination.parameters[i];
            const ScriptApiParameter& incoming = source.parameters[i];
            if (target.name.empty()) target.name = incoming.name;
            if (target.defaultValue.empty()) target.defaultValue = incoming.defaultValue;
        }
    }

    static void AddCallable(std::vector<ScriptApiCallable>& functions,
                            const ScriptApiCallable& callable)
    {
        for (auto& existing : functions)
        {
            if (existing.name == callable.name && SameSignature(existing, callable))
            {
                MergeCallable(existing, callable);
                return;
            }
        }
        functions.push_back(callable);
    }

    static void MergeType(ScriptApiType& destination, const ScriptApiType& source)
    {
        if (destination.cppType.empty()) destination.cppType = source.cppType;
        if (destination.description.empty()) destination.description = source.description;
        destination.internal = destination.internal && source.internal;
        destination.component = destination.component || source.component;
        for (const auto& property : source.properties)
        {
            auto it = std::find_if(destination.properties.begin(), destination.properties.end(),
                [&](const ScriptApiProperty& p) { return p.name == property.name; });
            if (it == destination.properties.end())
                destination.properties.push_back(property);
            else
            {
                if (it->type != property.type)
                    throw std::logic_error("Conflicting property type for '" + destination.name + "." + property.name + "'");
                if (it->description.empty()) it->description = property.description;
                it->readOnly = it->readOnly || property.readOnly;
                it->customBinding = it->customBinding || property.customBinding;
                if (it->bindingNotes.empty()) it->bindingNotes = property.bindingNotes;
            }
        }
        for (const auto& constructor : source.constructors)
            AddCallable(destination.constructors, constructor);
        for (const auto& method : source.methods)
            AddCallable(destination.methods, method);
        if (destination.ownership == "value" && source.ownership != "value")
            destination.ownership = source.ownership;
        if (destination.lifetime.empty()) destination.lifetime = source.lifetime;
        for (const std::string& base : source.bases)
            if (std::find(destination.bases.begin(), destination.bases.end(), base) == destination.bases.end())
                destination.bases.push_back(base);
        for (const std::string& op : source.operators)
            if (std::find(destination.operators.begin(), destination.operators.end(), op) == destination.operators.end())
                destination.operators.push_back(op);
    }

    std::map<std::string, ScriptApiType> types_;
    std::map<std::string, ScriptApiTable> tables_;
    std::map<std::string, ScriptApiEnum> enums_;
    std::map<std::string, ScriptApiConstant> constants_;
    std::map<std::string, std::vector<ScriptApiCallable>> globalFunctions_;
};
