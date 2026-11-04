#pragma once

// LuaLS definition-file generator for Mirage's ScriptApiRegistry.
// This header intentionally depends only on the metadata model and the C++
// standard library. It does not create a Lua state or depend on Sol2.

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#include "mirage_core/script_api.h"

struct LuaApiCodegenOptions
{
    // The generated file is a LuaLS meta/library file, not a runtime module.
    std::string generatedNotice = "Generated from Mirage ScriptApiRegistry. Do not edit by hand.";
    bool includeInternal = false;
    bool includeOwnershipNotes = true;
};

namespace mirage_lua_api_codegen_detail {

inline std::string Trim(std::string value)
{
    const auto first = std::find_if_not(value.begin(), value.end(), [](unsigned char c) { return std::isspace(c); });
    if (first == value.end()) return {};
    const auto last = std::find_if_not(value.rbegin(), value.rend(), [](unsigned char c) { return std::isspace(c); }).base();
    return std::string(first, last);
}

inline std::vector<std::string> SplitTopLevel(const std::string& value, char separator)
{
    std::vector<std::string> result;
    std::size_t start = 0;
    int braces = 0;
    int parens = 0;
    int angles = 0;
    bool quoted = false;
    char quote = '\0';
    bool escaped = false;

    for (std::size_t i = 0; i < value.size(); ++i)
    {
        const char c = value[i];
        if (quoted)
        {
            if (escaped) escaped = false;
            else if (c == '\\') escaped = true;
            else if (c == quote) quoted = false;
            continue;
        }
        if (c == '\'' || c == '"') { quoted = true; quote = c; continue; }
        if (c == '{') ++braces;
        else if (c == '}') --braces;
        else if (c == '(') ++parens;
        else if (c == ')') --parens;
        else if (c == '<') ++angles;
        else if (c == '>') --angles;
        else if (c == separator && braces == 0 && parens == 0 && angles == 0)
        {
            result.push_back(Trim(value.substr(start, i - start)));
            start = i + 1;
        }
    }
    result.push_back(Trim(value.substr(start)));
    return result;
}

inline bool ParseInlineTable(const std::string& raw,
                             std::vector<std::pair<std::string, std::string>>& fields)
{
    const std::string value = Trim(raw);
    if (value.size() < 2 || value.front() != '{' || value.back() != '}')
        return false;

    const std::string body = Trim(value.substr(1, value.size() - 2));
    if (body.empty()) return false;

    for (const std::string& member : SplitTopLevel(body, ','))
    {
        const auto pair = SplitTopLevel(member, ':');
        if (pair.size() < 2 || pair.front().empty() || pair[1].empty())
            return false;
        fields.emplace_back(pair.front(), pair[1]);
    }
    return !fields.empty();
}

inline void AddShapeIfNeeded(const std::string& raw, std::map<std::string, std::vector<std::pair<std::string, std::string>>>& shapes)
{
    std::vector<std::pair<std::string, std::string>> fields;
    if (ParseInlineTable(raw, fields)) shapes.emplace(Trim(raw), std::move(fields));
}

inline void CollectCallableShapes(const ScriptApiCallable& callable,
                                  std::map<std::string, std::vector<std::pair<std::string, std::string>>>& shapes)
{
    for (const auto& parameter : callable.parameters) AddShapeIfNeeded(parameter.type, shapes);
    for (const auto& result : callable.returns) AddShapeIfNeeded(result, shapes);
}

inline std::map<std::string, std::vector<std::pair<std::string, std::string>>> CollectShapes(const ScriptApiRegistry& registry)
{
    std::map<std::string, std::vector<std::pair<std::string, std::string>>> shapes;
    for (const auto& [_, type] : registry.Types())
    {
        for (const auto& property : type.properties) AddShapeIfNeeded(property.type, shapes);
        for (const auto& callable : type.constructors) CollectCallableShapes(callable, shapes);
        for (const auto& callable : type.methods) CollectCallableShapes(callable, shapes);
    }
    for (const auto& [_, table] : registry.Tables())
        for (const auto& callable : table.functions) CollectCallableShapes(callable, shapes);
    for (const auto& [_, overloads] : registry.GlobalFunctions())
        for (const auto& callable : overloads) CollectCallableShapes(callable, shapes);
    for (const auto& [_, constant] : registry.Constants()) AddShapeIfNeeded(constant.type, shapes);
    return shapes;
}

inline std::string NormalizeType(
    const std::string& raw,
    const std::map<std::string, std::string>& shapeAliases)
{
    const std::string value = Trim(raw);
    const auto alias = shapeAliases.find(value);
    if (alias != shapeAliases.end()) return alias->second;

    // LuaLS accepts union notation. Normalize spacing without changing quoted
    // literal types such as "Transform" used by GetComponent overloads.
    const auto unionParts = SplitTopLevel(value, '|');
    if (unionParts.size() > 1)
    {
        std::string result;
        for (const std::string& part : unionParts)
        {
            if (!result.empty()) result += "|";
            result += NormalizeType(part, shapeAliases);
        }
        return result;
    }
    return value.empty() ? "any" : value;
}

inline std::string OneLine(std::string text)
{
    for (char& c : text)
        if (c == '\r' || c == '\n') c = ' ';
    return Trim(std::move(text));
}

inline void EmitDoc(std::ostringstream& out, const std::string& text)
{
    if (text.empty()) return;
    std::istringstream lines(text);
    std::string line;
    while (std::getline(lines, line)) out << "--- " << line << '\n';
}

inline std::string SafeParameterName(const std::string& raw, std::size_t index)
{
    if (raw == "..." || raw == "_" ) return raw;
    if (raw.empty()) return "arg" + std::to_string(index + 1);
    std::string result;
    result.reserve(raw.size());
    for (std::size_t i = 0; i < raw.size(); ++i)
    {
        const unsigned char c = static_cast<unsigned char>(raw[i]);
        if ((i == 0 && (std::isalpha(c) || c == '_')) ||
            (i > 0 && (std::isalnum(c) || c == '_')))
            result += static_cast<char>(c);
        else
            result += '_';
    }
    if (result.empty() || std::isdigit(static_cast<unsigned char>(result.front())))
        result.insert(result.begin(), '_');
    return result;
}

inline std::string ParameterType(const ScriptApiParameter& parameter,
                                 const std::map<std::string, std::string>& shapeAliases)
{
    return NormalizeType(parameter.type, shapeAliases);
}

inline std::string CallableType(const ScriptApiCallable& callable,
                                const std::map<std::string, std::string>& shapeAliases)
{
    std::ostringstream signature;
    signature << "fun(";
    bool first = true;
    for (std::size_t i = 0; i < callable.parameters.size(); ++i)
    {
        const ScriptApiParameter& parameter = callable.parameters[i];
        if (!first) signature << ", ";
        first = false;
        if (parameter.variadic || parameter.name == "...")
            signature << "...: " << NormalizeType(parameter.type, shapeAliases);
        else
        {
            signature << SafeParameterName(parameter.name, i);
            if (parameter.optional) signature << "?";
            signature << ": " << ParameterType(parameter, shapeAliases);
        }
    }
    signature << ")";
    if (!callable.returns.empty())
    {
        signature << ": ";
        for (std::size_t i = 0; i < callable.returns.size(); ++i)
        {
            if (i) signature << ", ";
            signature << NormalizeType(callable.returns[i], shapeAliases);
        }
    }
    return signature.str();
}

inline std::string FunctionArguments(const ScriptApiCallable& callable)
{
    std::ostringstream args;
    bool first = true;
    for (std::size_t i = 0; i < callable.parameters.size(); ++i)
    {
        const ScriptApiParameter& parameter = callable.parameters[i];
        if (!first) args << ", ";
        first = false;
        if (parameter.variadic || parameter.name == "...") args << "...";
        else args << SafeParameterName(parameter.name, i);
    }
    return args.str();
}

// True for the broad, string-keyed GetComponent declaration whose return type
// is the opaque ComponentHandle|nil (EntityRef:GetComponent, ScriptContext:
// GetComponent and the global GetComponent all register this shape).
inline bool IsDynamicGetComponent(const ScriptApiCallable& primary,
                                  const std::map<std::string, std::string>& shapeAliases)
{
    return primary.name == "GetComponent" &&
        primary.parameters.size() == 1 &&
        primary.parameters.front().type == "string" &&
        primary.returns.size() == 1 &&
        NormalizeType(primary.returns.front(), shapeAliases) == "ComponentHandle|nil";
}

inline void EmitCallableAnnotations(std::ostringstream& out,
                                    const ScriptApiCallable& primary,
                                    const std::vector<ScriptApiCallable>& overloads,
                                    const std::map<std::string, std::string>& shapeAliases)
{
    EmitDoc(out, primary.description);

    // GetComponent is emitted as a LuaLS generic instead of a list of
    // literal-name overloads. In a backtick-generic parameter (`T`) LuaLS uses
    // the string literal's VALUE as the type name, so GetComponent("Sprite")
    // returns Sprite|nil and `sprite.` completes flipX, tint, layer...
    //
    // Literal-name @overload lines do not work here: LuaLS picks the broad
    // `string` signature for a literal argument and ignores the overloads. That
    // left two bad choices, `ComponentHandle|nil` (rejects every component
    // field access) or `any` (accepts anything but offers no completion).
    //
    // Requires each component's Lua class name to equal its script name, which
    // is how the component classes are already emitted. A name that is not a
    // known class, or a non-literal string, resolves to an unknown type without
    // raising a diagnostic, so dynamic lookups keep working. The literal-name
    // descriptors stay in the registry (for docs) but are not emitted here.
    if (IsDynamicGetComponent(primary, shapeAliases))
    {
        out << "---@generic T: ComponentHandle\n";
        out << "---@param " << SafeParameterName(primary.parameters.front().name, 0) << " `T`\n";
        out << "---@return T|nil\n";
        return;
    }

    // The first descriptor supplies the concrete declaration below; every
    // additional descriptor is emitted as a LuaLS overload.
    for (std::size_t i = 1; i < overloads.size(); ++i)
        out << "---@overload " << CallableType(overloads[i], shapeAliases) << '\n';

    for (std::size_t i = 0; i < primary.parameters.size(); ++i)
    {
        const ScriptApiParameter& parameter = primary.parameters[i];
        if (parameter.variadic || parameter.name == "...")
        {
            out << "---@param ... " << NormalizeType(parameter.type, shapeAliases) << '\n';
            continue;
        }
        out << "---@param " << SafeParameterName(parameter.name, i);
        if (parameter.optional) out << "?";
        out << " " << ParameterType(parameter, shapeAliases);
        if (!parameter.defaultValue.empty())
            out << " # default: " << OneLine(parameter.defaultValue);
        out << '\n';
    }

    for (const std::string& result : primary.returns)
        out << "---@return " << NormalizeType(result, shapeAliases) << '\n';
}

inline void EmitCallableGroup(std::ostringstream& out,
                              const std::string& declaration,
                              const std::vector<ScriptApiCallable>& overloads,
                              const std::map<std::string, std::string>& shapeAliases,
                              bool useColon = false)
{
    if (overloads.empty()) return;
    const ScriptApiCallable& primary = overloads.front();
    EmitCallableAnnotations(out, primary, overloads, shapeAliases);
    out << "function " << declaration;
    if (useColon && !primary.isStatic) out << ":";
    else out << ".";
    out << primary.name << "(" << FunctionArguments(primary) << ") end\n\n";
}

inline void OrderCallableOverloads(const std::string& name,
                                   std::vector<ScriptApiCallable>& overloads)
{
    if (name != "GetComponent") return;
    auto literalComponentOverload = [](const ScriptApiCallable& call) {
        if (call.parameters.size() != 1) return false;
        const std::string& type = call.parameters.front().type;
        return type.size() >= 2 &&
            ((type.front() == '\"' && type.back() == '\"') ||
             (type.front() == '\'' && type.back() == '\''));
    };
    // Keep the broad string signature first: EmitCallableAnnotations recognises
    // it (IsDynamicGetComponent) and emits the backtick generic from it. A
    // concrete literal-name descriptor must never be primary, or stable
    // ordering would make the alphabetically first component (often Game) the
    // apparent signature for every GetComponent call.
    std::stable_sort(overloads.begin(), overloads.end(),
        [&](const ScriptApiCallable& a, const ScriptApiCallable& b) {
            return !literalComponentOverload(a) && literalComponentOverload(b);
        });
}

inline std::map<std::string, std::vector<ScriptApiCallable>> GroupCallables(
    const std::vector<ScriptApiCallable>& callables)
{
    std::map<std::string, std::vector<ScriptApiCallable>> grouped;
    for (const ScriptApiCallable& callable : callables)
        grouped[callable.name].push_back(callable);
    for (auto& [name, overloads] : grouped)
        OrderCallableOverloads(name, overloads);
    return grouped;
}

inline std::string OperatorAnnotation(const std::string& op, const std::string& typeName)
{
    if (op == "+") return "---@operator add(" + typeName + "|number): " + typeName;
    if (op == "-") return "---@operator sub(" + typeName + "|number): " + typeName;
    if (op == "*") return "---@operator mul(" + typeName + "|number): " + typeName;
    if (op == "/") return "---@operator div(" + typeName + "|number): " + typeName;
    if (op == "unary -") return "---@operator unm: " + typeName;
    if (op == "==") return "---@operator eq(" + typeName + "): boolean";
    if (op == "tostring") return "---@operator tostring: string";
    return {};
}

inline bool IsValidPath(const std::string& path)
{
    if (path.empty()) return false;
    bool expectIdentifierStart = true;
    for (unsigned char c : path)
    {
        if (c == '.')
        {
            if (expectIdentifierStart) return false;
            expectIdentifierStart = true;
        }
        else if (expectIdentifierStart)
        {
            if (!(std::isalpha(c) || c == '_')) return false;
            expectIdentifierStart = false;
        }
        else if (!(std::isalnum(c) || c == '_')) return false;
    }
    return !expectIdentifierStart;
}

} // namespace mirage_lua_api_codegen_detail

class LuaApiCodeGenerator
{
public:
    explicit LuaApiCodeGenerator(LuaApiCodegenOptions options = {})
        : options_(std::move(options)) {}

    std::string Generate(const ScriptApiRegistry& registry) const
    {
        const std::vector<std::string> errors = registry.ValidateMetadata();
        if (!errors.empty())
        {
            std::string message = "Cannot generate _engine.lua: Script API metadata is invalid";
            for (const std::string& error : errors) message += "\n - " + error;
            throw std::runtime_error(message);
        }

        using namespace mirage_lua_api_codegen_detail;
        const auto shapes = CollectShapes(registry);
        std::map<std::string, std::string> shapeAliases;
        std::size_t shapeIndex = 0;
        for (const auto& [shape, _] : shapes)
            shapeAliases.emplace(shape, "MirageTable" + std::to_string(++shapeIndex));

        std::ostringstream out;
        out << "---@meta\n";
        out << "--- " << OneLine(options_.generatedNotice) << "\n";
        out << "--- Regenerate with: Mirage.exe --generate-lua-api [output-path]\n\n";

        // Anonymous table return types become named structural aliases so that
        // LuaLS can offer field completion instead of degrading everything to
        // the generic `table` type.
        for (const auto& [shape, fields] : shapes)
        {
            const std::string aliasName = shapeAliases.at(shape);
            std::string body = "{";
            for (std::size_t i = 0; i < fields.size(); ++i)
            {
                if (i) body += ", ";
                body += fields[i].first + ": " + NormalizeType(fields[i].second, shapeAliases);
            }
            body += "}";
            out << "---@alias " << aliasName << " " << body << "\n";
        }
        if (!shapes.empty()) out << '\n';

        // Native value types, EntityRef and reflected components.
        for (const auto& [name, type] : registry.Types())
        {
            if (type.internal && !options_.includeInternal) continue;
            EmitDoc(out, type.description);
            out << "---@class " << name;
            if (!type.bases.empty())
            {
                out << ": ";
                for (std::size_t i = 0; i < type.bases.size(); ++i)
                {
                    if (i) out << ", ";
                    out << type.bases[i];
                }
            }
            out << '\n';
            if (options_.includeOwnershipNotes)
            {
                if (!type.ownership.empty()) out << "---Ownership: " << OneLine(type.ownership) << '\n';
                if (!type.lifetime.empty()) out << "---Lifetime: " << OneLine(type.lifetime) << '\n';
            }
            for (const ScriptApiProperty& property : type.properties)
            {
                out << "---@field " << property.name << " "
                    << NormalizeType(property.type, shapeAliases);
                std::string note = OneLine(property.description);
                if (property.readOnly) note = note.empty() ? "read-only" : "read-only; " + note;
                if (!note.empty()) out << " # " << note;
                out << '\n';
            }
            for (const std::string& op : type.operators)
            {
                const std::string annotation = OperatorAnnotation(op, name);
                if (!annotation.empty()) out << annotation << '\n';
            }
            // Constructor overloads also describe the callable class table,
            // allowing both Vector3.new(...) and Vector3(...) to be completed.
            for (const auto& constructor : type.constructors)
                out << "---@overload " << CallableType(constructor, shapeAliases) << '\n';
            out << name << " = {}\n\n";

            const auto constructors = GroupCallables(type.constructors);
            for (const auto& [_, overloads] : constructors)
                EmitCallableGroup(out, name, overloads, shapeAliases, false);

            const auto methods = GroupCallables(type.methods);
            for (const auto& [methodName, overloads] : methods)
            {
                if (overloads.empty()) continue;
                const ScriptApiCallable& primary = overloads.front();
                EmitCallableAnnotations(out, primary, overloads, shapeAliases);
                out << "function " << name;
                if (primary.isStatic) out << ".";
                else out << ":";
                out << methodName << "(" << FunctionArguments(primary) << ") end\n\n";
            }
        }

        // Lua tables such as Entities, Events, input and the per-script self
        // context. ScriptContext is an instance passed to the factory, not a
        // global object, so declare a local placeholder for its methods.
        for (const auto& [name, table] : registry.Tables())
        {
            if (table.internal && !options_.includeInternal) continue;
            EmitDoc(out, table.description);
            out << "---@class " << name << '\n';
            if (name == "ScriptContext") out << "local " << name << " = {}\n\n";
            else out << name << " = {}\n\n";

            const auto grouped = GroupCallables(table.functions);
            for (const auto& [_, overloads] : grouped)
            {
                if (overloads.empty()) continue;
                const ScriptApiCallable& primary = overloads.front();
                EmitCallableAnnotations(out, primary, overloads, shapeAliases);
                out << "function " << name;
                if (name == "ScriptContext" && !primary.isStatic) out << ":";
                else out << ".";
                out << primary.name << "(" << FunctionArguments(primary) << ") end\n\n";
            }
        }

        // Registered global functions may be nested in built-in tables (e.g. math.clamp).
        for (const auto& [path, overloads] : registry.GlobalFunctions())
        {
            if (!IsValidPath(path))
                throw std::runtime_error("Cannot generate _engine.lua: invalid Lua function path '" + path + "'");
            if (overloads.empty()) continue;
            std::vector<ScriptApiCallable> orderedOverloads = overloads;
            const std::size_t dot = path.rfind('.');
            const std::string name = dot == std::string::npos ? path : path.substr(dot + 1);
            OrderCallableOverloads(name, orderedOverloads);
            EmitCallableAnnotations(out, orderedOverloads.front(), orderedOverloads, shapeAliases);
            const std::string scope = dot == std::string::npos ? std::string{} : path.substr(0, dot + 1);
            out << "function " << scope << name << "(" << FunctionArguments(orderedOverloads.front()) << ") end\n\n";
        }

        // Enum tables are emitted as typed Lua tables with integer-valued fields.
        for (const auto& [name, enumeration] : registry.Enums())
        {
            if (enumeration.internal && !options_.includeInternal) continue;
            EmitDoc(out, enumeration.description);
            out << "---@class " << name << '\n';
            out << name << " = {}\n";
            for (const ScriptApiEnumValue& value : enumeration.values)
            {
                if (!value.description.empty()) out << "--- " << OneLine(value.description) << '\n';
                out << "---@type integer\n";
                out << name << "." << value.name << " = " << value.value << '\n';
            }
            out << '\n';
        }

        // Constants use their declared Lua-visible path and type. If no literal
        // expression was supplied, nil is used as a harmless meta-file value.
        for (const auto& [path, constant] : registry.Constants())
        {
            if (constant.internal && !options_.includeInternal) continue;
            if (!IsValidPath(path))
                throw std::runtime_error("Cannot generate _engine.lua: invalid Lua constant path '" + path + "'");
            EmitDoc(out, constant.description);
            out << "---@type " << NormalizeType(constant.type, shapeAliases) << '\n';
            out << path << " = "
                << (constant.valueExpression.empty() ? "nil" : constant.valueExpression)
                << "\n\n";
        }

        return out.str();
    }

    // Returns true when the file was created or changed, false when its
    // contents were already up to date.
    bool WriteFile(const ScriptApiRegistry& registry, const std::filesystem::path& outputPath) const
    {
        if (outputPath.empty()) throw std::invalid_argument("Lua API output path cannot be empty");
        const std::string generated = Generate(registry);

        {
            std::ifstream existing(outputPath, std::ios::binary);
            if (existing)
            {
                std::ostringstream contents;
                contents << existing.rdbuf();
                if (contents.str() == generated) return false;
            }
        }

        const std::filesystem::path parent = outputPath.parent_path();
        if (!parent.empty())
        {
            std::error_code ec;
            std::filesystem::create_directories(parent, ec);
            if (ec) throw std::runtime_error("Could not create Lua API output directory: " + ec.message());
        }

        std::filesystem::path temporary = outputPath;
        temporary += ".tmp";
        {
            std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
            if (!file) throw std::runtime_error("Could not open Lua API temporary output file: " + temporary.string());
            file.write(generated.data(), static_cast<std::streamsize>(generated.size()));
            if (!file) throw std::runtime_error("Failed while writing Lua API output: " + temporary.string());
        }

        std::error_code ec;
        std::filesystem::rename(temporary, outputPath, ec);
        if (ec)
        {
            // Windows does not replace an existing destination with rename().
            std::error_code removeError;
            std::filesystem::remove(outputPath, removeError);
            ec.clear();
            std::filesystem::rename(temporary, outputPath, ec);
        }
        if (ec)
        {
            std::error_code ignored;
            std::filesystem::remove(temporary, ignored);
            throw std::runtime_error("Could not replace Lua API output file: " + ec.message());
        }
        return true;
    }

private:
    LuaApiCodegenOptions options_;
};