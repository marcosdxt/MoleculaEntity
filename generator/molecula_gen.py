#!/usr/bin/env python3
"""
MoleculaEntity Code Generator

Generates C++ entity and repository classes from TOML/JSON schema definitions.

Usage:
    python molecula_gen.py schema.toml [--output-dir ./generated]
    python molecula_gen.py schema.json [--output-dir ./generated]
"""

import argparse
import json
import os
import sys
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any, Dict, List, Optional
from datetime import datetime

try:
    import tomllib
except ImportError:
    try:
        import tomli as tomllib
    except ImportError:
        tomllib = None


@dataclass
class ForeignKey:
    table: str
    column: str


@dataclass
class Column:
    name: str
    cpp_type: str
    molecula_type: str
    nullable: bool = True
    unique: bool = False
    default: Optional[str] = None
    foreign_key: Optional[ForeignKey] = None


@dataclass
class Index:
    name: str
    columns: List[str]
    unique: bool = False


@dataclass
class RepositoryMethod:
    name: str
    where_conditions: List[Dict[str, Any]]
    returns: str  # "optional" or "vector"


@dataclass
class Migration:
    version: int
    description: str
    up_sql: str
    down_sql: str = ""


@dataclass
class Entity:
    name: str
    table: str
    version: int
    columns: List[Column]
    indexes: List[Index] = field(default_factory=list)
    repository_methods: List[RepositoryMethod] = field(default_factory=list)
    migrations: List[Migration] = field(default_factory=list)


@dataclass
class Config:
    namespace: str
    output_dir: str
    include_guard_prefix: str


TYPE_MAPPING = {
    "string": ("std::string", "Text"),
    "int": ("int", "Integer"),
    "int32": ("int32_t", "Integer"),
    "int64": ("int64_t", "BigInt"),
    "double": ("double", "Real"),
    "float": ("float", "Real"),
    "bool": ("bool", "Boolean"),
    "blob": ("std::vector<uint8_t>", "Blob"),
    "timestamp": ("BaseEntity::Timestamp", "Timestamp"),
}

OP_MAPPING = {
    "eq": "Equals",
    "neq": "NotEquals",
    "gt": "GreaterThan",
    "gte": "GreaterThanOrEquals",
    "lt": "LessThan",
    "lte": "LessThanOrEquals",
    "like": "Like",
    "notlike": "NotLike",
    "in": "In",
    "notin": "NotIn",
    "null": "IsNull",
    "notnull": "IsNotNull",
    "between": "Between",
}


def parse_schema(file_path: str) -> tuple[Config, List[Entity]]:
    """Parse TOML or JSON schema file."""
    path = Path(file_path)

    if path.suffix == ".toml":
        if tomllib is None:
            print("Error: tomli package required for TOML support. Install with: pip install tomli")
            sys.exit(1)
        with open(path, "rb") as f:
            data = tomllib.load(f)
    elif path.suffix == ".json":
        with open(path, "r") as f:
            data = json.load(f)
    else:
        print(f"Error: Unsupported file format: {path.suffix}")
        sys.exit(1)

    config = Config(
        namespace=data.get("config", {}).get("namespace", "Generated"),
        output_dir=data.get("config", {}).get("output_dir", "generated"),
        include_guard_prefix=data.get("config", {}).get("include_guard_prefix", "GENERATED"),
    )

    entities = []
    for entity_name, entity_data in data.get("entities", {}).items():
        columns = []
        for col_name, col_data in entity_data.get("columns", {}).items():
            col_type = col_data.get("type", "string")
            cpp_type, molecula_type = TYPE_MAPPING.get(col_type, ("std::string", "Text"))

            fk = None
            if "foreign_key" in col_data:
                fk_data = col_data["foreign_key"]
                fk = ForeignKey(table=fk_data["table"], column=fk_data["column"])

            columns.append(Column(
                name=col_name,
                cpp_type=cpp_type,
                molecula_type=molecula_type,
                nullable=col_data.get("nullable", True),
                unique=col_data.get("unique", False),
                default=col_data.get("default"),
                foreign_key=fk,
            ))

        indexes = []
        for idx_name, idx_data in entity_data.get("indexes", {}).items():
            indexes.append(Index(
                name=idx_name,
                columns=idx_data.get("columns", []),
                unique=idx_data.get("unique", False),
            ))

        repo_methods = []
        for method_name, method_data in entity_data.get("repository", {}).items():
            repo_methods.append(RepositoryMethod(
                name=method_name,
                where_conditions=method_data.get("where", []),
                returns=method_data.get("returns", "vector"),
            ))

        migrations = []
        for ver, mig_data in entity_data.get("migrations", {}).items():
            migrations.append(Migration(
                version=int(ver),
                description=mig_data.get("description", ""),
                up_sql=mig_data.get("up", ""),
                down_sql=mig_data.get("down", ""),
            ))

        entities.append(Entity(
            name=entity_name,
            table=entity_data.get("table", entity_name.lower() + "s"),
            version=entity_data.get("version", 1),
            columns=columns,
            indexes=indexes,
            repository_methods=repo_methods,
            migrations=sorted(migrations, key=lambda m: m.version),
        ))

    return config, entities


def to_snake_case(name: str) -> str:
    """Convert CamelCase to snake_case."""
    result = []
    for i, c in enumerate(name):
        if c.isupper() and i > 0:
            result.append('_')
        result.append(c.lower())
    return ''.join(result)


def to_camel_case(name: str) -> str:
    """Convert snake_case to CamelCase."""
    parts = name.split('_')
    return ''.join(p.capitalize() for p in parts)


def get_getter_name(col: Column) -> str:
    """Get getter method name for a column."""
    camel = to_camel_case(col.name)
    if col.cpp_type == "bool":
        return f"is{camel}"
    return f"get{camel}"


def get_setter_name(col: Column) -> str:
    """Get setter method name for a column."""
    return f"set{to_camel_case(col.name)}"


def get_member_name(col: Column) -> str:
    """Get member variable name for a column."""
    return f"{col.name}_"


def generate_entity_header(entity: Entity, config: Config) -> str:
    """Generate entity header file content."""
    guard = f"{config.include_guard_prefix}_{entity.name.upper()}_ENTITY_HPP"

    lines = [
        "#pragma once",
        "",
        "#include <MoleculaEntity/MoleculaEntity.hpp>",
        "#include <string>",
        "#include <optional>",
        "#include <vector>",
        "",
        f"namespace {config.namespace} {{",
        "",
        f"class {entity.name}Entity : public MoleculaEntity::BaseEntity {{",
        "public:",
        f"    {entity.name}Entity() = default;",
        f"    ~{entity.name}Entity() override = default;",
        "",
    ]

    # Copy constructor
    lines.append(f"    {entity.name}Entity(const {entity.name}Entity& other)")
    lines.append("        : BaseEntity(other)")
    for col in entity.columns:
        lines.append(f"        , {get_member_name(col)}(other.{get_member_name(col)})")
    lines[-1] = lines[-1] + " {}"
    lines.append("")

    # Move constructor
    lines.append(f"    {entity.name}Entity({entity.name}Entity&& other) noexcept")
    lines.append("        : BaseEntity(std::move(other))")
    for col in entity.columns:
        member = get_member_name(col)
        if col.cpp_type == "std::string" or col.cpp_type.startswith("std::vector"):
            lines.append(f"        , {member}(std::move(other.{member}))")
        else:
            lines.append(f"        , {member}(other.{member})")
    lines[-1] = lines[-1] + " {}"
    lines.append("")

    # Copy assignment
    lines.append(f"    {entity.name}Entity& operator=(const {entity.name}Entity& other) {{")
    lines.append("        if (this != &other) {")
    lines.append("            BaseEntity::operator=(other);")
    for col in entity.columns:
        member = get_member_name(col)
        lines.append(f"            {member} = other.{member};")
    lines.append("        }")
    lines.append("        return *this;")
    lines.append("    }")
    lines.append("")

    # Move assignment
    lines.append(f"    {entity.name}Entity& operator=({entity.name}Entity&& other) noexcept {{")
    lines.append("        if (this != &other) {")
    lines.append("            BaseEntity::operator=(std::move(other));")
    for col in entity.columns:
        member = get_member_name(col)
        if col.cpp_type == "std::string" or col.cpp_type.startswith("std::vector"):
            lines.append(f"            {member} = std::move(other.{member});")
        else:
            lines.append(f"            {member} = other.{member};")
    lines.append("        }")
    lines.append("        return *this;")
    lines.append("    }")
    lines.append("")

    # Table info methods
    lines.append(f'    [[nodiscard]] std::string tableName() const override {{ return "{entity.table}"; }}')
    lines.append(f'    [[nodiscard]] int tableVersion() const override {{ return {entity.version}; }}')
    lines.append("")

    # Columns definition
    lines.append("    [[nodiscard]] std::vector<MoleculaEntity::ColumnDefinition> columns() const override {")
    lines.append("        return {")
    for i, col in enumerate(entity.columns):
        fk_table = f'"{col.foreign_key.table}"' if col.foreign_key else "std::nullopt"
        fk_col = f'"{col.foreign_key.column}"' if col.foreign_key else "std::nullopt"
        default = "std::nullopt"
        if col.default:
            # Convert SQL-style quotes to C++ strings
            if col.default.startswith("'") and col.default.endswith("'"):
                # SQL string literal like 'pending' -> C++ string "pending"
                inner = col.default[1:-1]  # Remove surrounding quotes
                default = f'"\'{inner}\'"'  # Result: "'pending'"
            else:
                default = f'"{col.default}"'

        nullable = "true" if col.nullable else "false"
        unique = "true" if col.unique else "false"

        comma = "," if i < len(entity.columns) - 1 else ""
        lines.append(f'            {{"{col.name}", MoleculaEntity::ColumnType::{col.molecula_type}, {nullable}, false, {unique}, {default}, {fk_table}, {fk_col}}}{comma}')
    lines.append("        };")
    lines.append("    }")
    lines.append("")

    # Migrations
    if entity.migrations:
        lines.append("    [[nodiscard]] std::vector<MoleculaEntity::Migration> migrations() const override {")
        lines.append("        return {")
        for i, mig in enumerate(entity.migrations):
            comma = "," if i < len(entity.migrations) - 1 else ""
            lines.append(f'            {{{mig.version}, "{mig.description}", R"({mig.up_sql})", R"({mig.down_sql})"}}{comma}')
        lines.append("        };")
        lines.append("    }")
        lines.append("")

    # Getters
    lines.append("    // Getters")
    for col in entity.columns:
        getter = get_getter_name(col)
        member = get_member_name(col)
        if col.nullable:
            if col.cpp_type in ("int", "int32_t", "int64_t", "double", "float", "bool"):
                lines.append(f"    [[nodiscard]] std::optional<{col.cpp_type}> {getter}() const noexcept {{ return {member}; }}")
            else:
                lines.append(f"    [[nodiscard]] const std::optional<{col.cpp_type}>& {getter}() const noexcept {{ return {member}; }}")
        else:
            if col.cpp_type in ("int", "int32_t", "int64_t", "double", "float", "bool"):
                lines.append(f"    [[nodiscard]] {col.cpp_type} {getter}() const noexcept {{ return {member}; }}")
            else:
                lines.append(f"    [[nodiscard]] const {col.cpp_type}& {getter}() const noexcept {{ return {member}; }}")
    lines.append("")

    # Setters
    lines.append("    // Setters")
    for col in entity.columns:
        setter = get_setter_name(col)
        member = get_member_name(col)
        if col.nullable:
            lines.append(f"    void {setter}(std::optional<{col.cpp_type}> value) {{ {member} = value; }}")
        else:
            if col.cpp_type == "std::string":
                lines.append(f"    void {setter}(const {col.cpp_type}& value) {{ {member} = value; }}")
                lines.append(f"    void {setter}({col.cpp_type}&& value) noexcept {{ {member} = std::move(value); }}")
            else:
                lines.append(f"    void {setter}({col.cpp_type} value) noexcept {{ {member} = value; }}")
    lines.append("")

    # Private members
    lines.append("private:")
    for col in entity.columns:
        member = get_member_name(col)
        if col.nullable:
            lines.append(f"    std::optional<{col.cpp_type}> {member};")
        else:
            if col.cpp_type == "bool":
                default_val = "true" if col.default == "true" else "false"
                lines.append(f"    {col.cpp_type} {member} = {default_val};")
            elif col.cpp_type in ("int", "int32_t", "int64_t"):
                default_val = col.default if col.default else "0"
                lines.append(f"    {col.cpp_type} {member} = {default_val};")
            elif col.cpp_type in ("double", "float"):
                default_val = col.default if col.default else "0.0"
                lines.append(f"    {col.cpp_type} {member} = {default_val};")
            else:
                lines.append(f"    {col.cpp_type} {member};")

    lines.append("};")
    lines.append("")
    lines.append(f"}} // namespace {config.namespace}")
    lines.append("")

    return "\n".join(lines)


def generate_repository_header(entity: Entity, config: Config) -> str:
    """Generate repository header file content."""
    guard = f"{config.include_guard_prefix}_{entity.name.upper()}_REPOSITORY_HPP"
    entity_class = f"{entity.name}Entity"

    lines = [
        "#pragma once",
        "",
        "#include <MoleculaEntity/MoleculaEntity.hpp>",
        f'#include "{entity.name}Entity.hpp"',
        "",
        f"namespace {config.namespace} {{",
        "",
        f"class {entity.name}Repository : public MoleculaEntity::BaseRepository<{entity_class}> {{",
        "public:",
        f"    explicit {entity.name}Repository(MoleculaEntity::DatabaseManagerPtr db)",
        f"        : MoleculaEntity::BaseRepository<{entity_class}>(std::move(db)) {{}}",
        "",
    ]

    # Custom repository methods
    for method in entity.repository_methods:
        params = []
        for cond in method.where_conditions:
            if "value" not in cond:
                col_name = cond["column"]
                col = next((c for c in entity.columns if c.name == col_name), None)
                if col:
                    if cond["op"] == "between":
                        params.append(f"{col.cpp_type} min{to_camel_case(col_name)}")
                        params.append(f"{col.cpp_type} max{to_camel_case(col_name)}")
                    else:
                        params.append(f"const {col.cpp_type}& {col_name}")

        param_str = ", ".join(params)

        if method.returns == "optional":
            ret_type = f"std::optional<{entity_class}>"
        else:
            ret_type = f"std::vector<{entity_class}>"

        lines.append(f"    [[nodiscard]] {ret_type} {method.name}({param_str}) {{")
        lines.append("        MoleculaEntity::QueryBuilder qb;")

        for cond in method.where_conditions:
            col_name = cond["column"]
            op = OP_MAPPING.get(cond["op"], "Equals")

            if "value" in cond:
                value = cond["value"]
                if value == "true":
                    value = "static_cast<int64_t>(1)"
                elif value == "false":
                    value = "static_cast<int64_t>(0)"
                elif value.startswith("'") and value.endswith("'"):
                    value = f'std::string({value.replace(chr(39), chr(34))})'
                lines.append(f'        qb.where("{col_name}", MoleculaEntity::CompareOp::{op}, {value});')
            elif cond["op"] == "between":
                col = next((c for c in entity.columns if c.name == col_name), None)
                if col:
                    cast = ""
                    if col.cpp_type in ("int", "int32_t"):
                        cast = "static_cast<int64_t>"
                    lines.append(f'        qb.whereBetween("{col_name}", {cast}(min{to_camel_case(col_name)}), {cast}(max{to_camel_case(col_name)}));')
            else:
                col = next((c for c in entity.columns if c.name == col_name), None)
                if col:
                    value_expr = col_name
                    if col.cpp_type in ("int", "int32_t"):
                        value_expr = f"static_cast<int64_t>({col_name})"
                    elif col.cpp_type == "bool":
                        value_expr = f"static_cast<int64_t>({col_name} ? 1 : 0)"
                    lines.append(f'        qb.where("{col_name}", MoleculaEntity::CompareOp::{op}, {value_expr});')

        if method.returns == "optional":
            lines.append("        auto results = find(qb);")
            lines.append("        return results.empty() ? std::nullopt : std::make_optional(results[0]);")
        else:
            lines.append("        return find(qb);")

        lines.append("    }")
        lines.append("")

    # Protected methods
    lines.append("protected:")

    # getInsertColumns
    # Parâmetro sem nome: a lista de colunas não depende da instância, e um
    # nome não usado faz o consumidor compilar com -Wunused-parameter aceso.
    # Código gerado tem que passar sob os avisos de quem o inclui.
    lines.append(f"    [[nodiscard]] std::vector<std::string> getInsertColumns(const {entity_class}&) const override {{")
    cols = ["id"] + [col.name for col in entity.columns]
    col_list = ", ".join(f'"{c}"' for c in cols)
    lines.append(f"        return {{{col_list}}};")
    lines.append("    }")
    lines.append("")

    # getInsertValues
    lines.append(f"    [[nodiscard]] std::vector<MoleculaEntity::DbValue> getInsertValues(const {entity_class}& entity) const override {{")
    lines.append("        std::vector<MoleculaEntity::DbValue> values;")
    lines.append("        values.push_back(entity.getId());")
    for col in entity.columns:
        getter = get_getter_name(col)
        if col.nullable:
            lines.append(f"        if (entity.{getter}().has_value()) {{")
            if col.cpp_type in ("int", "int32_t"):
                lines.append(f"            values.push_back(static_cast<int64_t>(entity.{getter}().value()));")
            elif col.cpp_type == "bool":
                lines.append(f"            values.push_back(static_cast<int64_t>(entity.{getter}().value() ? 1 : 0));")
            else:
                lines.append(f"            values.push_back(entity.{getter}().value());")
            lines.append("        } else {")
            lines.append("            values.push_back(nullptr);")
            lines.append("        }")
        else:
            if col.cpp_type in ("int", "int32_t"):
                lines.append(f"        values.push_back(static_cast<int64_t>(entity.{getter}()));")
            elif col.cpp_type == "bool":
                lines.append(f"        values.push_back(static_cast<int64_t>(entity.{getter}() ? 1 : 0));")
            else:
                lines.append(f"        values.push_back(entity.{getter}());")
    lines.append("        return values;")
    lines.append("    }")
    lines.append("")

    # getUpdateColumns
    # Parâmetro sem nome: a lista de colunas não depende da instância, e um
    # nome não usado faz o consumidor compilar com -Wunused-parameter aceso.
    # Código gerado tem que passar sob os avisos de quem o inclui.
    lines.append(f"    [[nodiscard]] std::vector<std::string> getUpdateColumns(const {entity_class}&) const override {{")
    cols = [col.name for col in entity.columns]
    col_list = ", ".join(f'"{c}"' for c in cols)
    lines.append(f"        return {{{col_list}}};")
    lines.append("    }")
    lines.append("")

    # getUpdateValues
    lines.append(f"    [[nodiscard]] std::vector<MoleculaEntity::DbValue> getUpdateValues(const {entity_class}& entity) const override {{")
    lines.append("        std::vector<MoleculaEntity::DbValue> values;")
    for col in entity.columns:
        getter = get_getter_name(col)
        if col.nullable:
            lines.append(f"        if (entity.{getter}().has_value()) {{")
            if col.cpp_type in ("int", "int32_t"):
                lines.append(f"            values.push_back(static_cast<int64_t>(entity.{getter}().value()));")
            elif col.cpp_type == "bool":
                lines.append(f"            values.push_back(static_cast<int64_t>(entity.{getter}().value() ? 1 : 0));")
            else:
                lines.append(f"            values.push_back(entity.{getter}().value());")
            lines.append("        } else {")
            lines.append("            values.push_back(nullptr);")
            lines.append("        }")
        else:
            if col.cpp_type in ("int", "int32_t"):
                lines.append(f"        values.push_back(static_cast<int64_t>(entity.{getter}()));")
            elif col.cpp_type == "bool":
                lines.append(f"        values.push_back(static_cast<int64_t>(entity.{getter}() ? 1 : 0));")
            else:
                lines.append(f"        values.push_back(entity.{getter}());")
    lines.append("        return values;")
    lines.append("    }")
    lines.append("")

    # mapRowToEntity
    lines.append(f"    [[nodiscard]] {entity_class} mapRowToEntity(const MoleculaEntity::DbRow& row) const override {{")
    lines.append(f"        {entity_class} entity;")
    lines.append("")
    lines.append("        // Base columns: idx, id, created_at, updated_at")
    lines.append("        if (row.size() >= 4) {")
    lines.append("            entity.setIdx(getInt64Value(row[0]));")
    lines.append("            entity.setId(getStringValue(row[1]));")
    lines.append("            auto createdAt = parseTimestamp(row[2]);")
    lines.append("            if (createdAt.has_value()) entity.setCreatedAt(createdAt.value());")
    lines.append("            auto updatedAt = parseTimestamp(row[3]);")
    lines.append("            if (updatedAt.has_value()) entity.setUpdatedAt(updatedAt.value());")
    lines.append("        }")
    lines.append("")
    lines.append("        // Entity columns")
    base_offset = 4
    lines.append(f"        if (row.size() >= {base_offset + len(entity.columns)}) {{")
    for i, col in enumerate(entity.columns):
        setter = get_setter_name(col)
        idx = base_offset + i
        if col.nullable:
            lines.append(f"            if (!isNull(row[{idx}])) {{")
            if col.cpp_type == "std::string":
                lines.append(f"                entity.{setter}(getStringValue(row[{idx}]));")
            elif col.cpp_type in ("int", "int32_t"):
                lines.append(f"                entity.{setter}(static_cast<int>(getInt64Value(row[{idx}])));")
            elif col.cpp_type == "int64_t":
                lines.append(f"                entity.{setter}(getInt64Value(row[{idx}]));")
            elif col.cpp_type in ("double", "float"):
                lines.append(f"                entity.{setter}(getDoubleValue(row[{idx}]));")
            elif col.cpp_type == "bool":
                lines.append(f"                entity.{setter}(getInt64Value(row[{idx}]) != 0);")
            lines.append("            }")
        else:
            if col.cpp_type == "std::string":
                lines.append(f"            entity.{setter}(getStringValue(row[{idx}]));")
            elif col.cpp_type in ("int", "int32_t"):
                lines.append(f"            entity.{setter}(static_cast<int>(getInt64Value(row[{idx}])));")
            elif col.cpp_type == "int64_t":
                lines.append(f"            entity.{setter}(getInt64Value(row[{idx}]));")
            elif col.cpp_type in ("double", "float"):
                lines.append(f"            entity.{setter}(getDoubleValue(row[{idx}]));")
            elif col.cpp_type == "bool":
                lines.append(f"            entity.{setter}(getInt64Value(row[{idx}]) != 0);")
    lines.append("        }")
    lines.append("")
    lines.append("        return entity;")
    lines.append("    }")

    lines.append("};")
    lines.append("")
    lines.append(f"}} // namespace {config.namespace}")
    lines.append("")

    return "\n".join(lines)


def generate_bootstrap_header(entities: List[Entity], config: Config) -> str:
    """Generate the database bootstrap/sync header."""
    lines = [
        "#pragma once",
        "",
        "#include <MoleculaEntity/MoleculaEntity.hpp>",
        "",
    ]

    for entity in entities:
        lines.append(f'#include "{entity.name}Entity.hpp"')
        lines.append(f'#include "{entity.name}Repository.hpp"')

    lines.extend([
        "",
        f"namespace {config.namespace} {{",
        "",
        "class DatabaseBootstrap {",
        "public:",
        "    explicit DatabaseBootstrap(MoleculaEntity::DatabaseManagerPtr db)",
        "        : schemaManager_(db)",
    ])

    for i, entity in enumerate(entities):
        var_name = to_snake_case(entity.name)
        if i < len(entities) - 1:
            lines.append(f"        , {var_name}Repo_(db)")
        else:
            lines.append(f"        , {var_name}Repo_(db) {{")

    # O construtor não faz nada que possa falhar. Antes ele chamava
    # `initialize()` aqui dentro, onde um erro não tem como ser reportado sem
    # lançar — e esta biblioteca não lança por erro de banco.
    lines.append("    }")
    lines.append("")

    lines.append("    /// Cria o que falta e aplica as migrações pendentes.")
    lines.append("    ///")
    lines.append("    /// Devolve `false` na PRIMEIRA falha, sem seguir para as tabelas")
    lines.append("    /// seguintes: com o esquema meio aplicado, continuar só produz erros em")
    lines.append("    /// cascata que escondem o primeiro, que é o único que interessa.")
    lines.append("    /// O motivo fica em `lastError()`.")
    lines.append("    [[nodiscard]] bool syncAll() {")
    lines.append("        if (!schemaManager_.initialize()) {")
    lines.append("            return false;")
    lines.append("        }")
    for entity in entities:
        lines.append(f"        if (!schemaManager_.syncEntity<{entity.name}Entity>()) {{")
        lines.append("            return false;")
        lines.append("        }")
    lines.append("        return true;")
    lines.append("    }")
    lines.append("")
    lines.append("    [[nodiscard]] const std::string& lastError() const noexcept {")
    lines.append("        return schemaManager_.lastError();")
    lines.append("    }")
    lines.append("")

    # Repository getters
    for entity in entities:
        var_name = to_snake_case(entity.name)
        lines.append(f"    [[nodiscard]] {entity.name}Repository& {var_name}Repository() noexcept {{ return {var_name}Repo_; }}")
    lines.append("")

    lines.append("    [[nodiscard]] MoleculaEntity::SchemaManager& schemaManager() noexcept { return schemaManager_; }")
    lines.append("")

    lines.append("private:")
    lines.append("    MoleculaEntity::SchemaManager schemaManager_;")
    for entity in entities:
        var_name = to_snake_case(entity.name)
        lines.append(f"    {entity.name}Repository {var_name}Repo_;")

    lines.append("};")
    lines.append("")
    lines.append(f"}} // namespace {config.namespace}")
    lines.append("")

    return "\n".join(lines)


def generate_all_header(entities: List[Entity], config: Config) -> str:
    """Generate the main include header."""
    lines = [
        "#pragma once",
        "",
        "// MoleculaEntity Generated Code",
        f"// Generated at: {datetime.now().isoformat()}",
        "",
        "#include <MoleculaEntity/MoleculaEntity.hpp>",
        "",
    ]

    for entity in entities:
        lines.append(f'#include "{entity.name}Entity.hpp"')
        lines.append(f'#include "{entity.name}Repository.hpp"')

    lines.append("")
    lines.append('#include "DatabaseBootstrap.hpp"')
    lines.append("")

    return "\n".join(lines)


def main():
    parser = argparse.ArgumentParser(
        description="Generate C++ entity and repository classes from TOML/JSON schema"
    )
    parser.add_argument("schema", help="Path to schema file (TOML or JSON)")
    parser.add_argument("--output-dir", "-o", help="Output directory (overrides schema config)")

    args = parser.parse_args()

    if not os.path.exists(args.schema):
        print(f"Error: Schema file not found: {args.schema}")
        sys.exit(1)

    config, entities = parse_schema(args.schema)

    if args.output_dir:
        config.output_dir = args.output_dir

    # Create output directory
    output_path = Path(config.output_dir)
    output_path.mkdir(parents=True, exist_ok=True)

    print(f"Generating code for {len(entities)} entities...")

    # Generate entity and repository files
    for entity in entities:
        entity_file = output_path / f"{entity.name}Entity.hpp"
        repo_file = output_path / f"{entity.name}Repository.hpp"

        with open(entity_file, "w") as f:
            f.write(generate_entity_header(entity, config))
        print(f"  Generated: {entity_file}")

        with open(repo_file, "w") as f:
            f.write(generate_repository_header(entity, config))
        print(f"  Generated: {repo_file}")

    # Generate bootstrap file
    bootstrap_file = output_path / "DatabaseBootstrap.hpp"
    with open(bootstrap_file, "w") as f:
        f.write(generate_bootstrap_header(entities, config))
    print(f"  Generated: {bootstrap_file}")

    # Generate main header
    all_header_file = output_path / "Entities.hpp"
    with open(all_header_file, "w") as f:
        f.write(generate_all_header(entities, config))
    print(f"  Generated: {all_header_file}")

    print(f"\nDone! Files generated in: {output_path}")
    print("\nUsage example:")
    print(f"""
#include "{config.output_dir}/Entities.hpp"

int main() {{
    auto db = MoleculaEntity::SQLite3DatabaseManager::open("app.db");
    if (!db) {{ return 1; }}

    {config.namespace}::DatabaseBootstrap bootstrap(db);

    // Cria as tabelas e aplica as migracoes pendentes. O retorno importa:
    // esquema que nao subiu e tudo o que vem depois rodando contra uma
    // tabela que nao existe.
    if (!bootstrap.syncAll()) {{
        std::fprintf(stderr, "esquema: %s\\n", bootstrap.lastError().c_str());
        return 1;
    }}

    // Use repositories
    auto& userRepo = bootstrap.userRepository();

    {config.namespace}::UserEntity user;
    user.setName("John");
    user.setEmail("john@example.com");

    auto savedUser = userRepo.save(user);

    return 0;
}}
""")


if __name__ == "__main__":
    main()
