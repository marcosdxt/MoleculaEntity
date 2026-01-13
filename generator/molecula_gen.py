#!/usr/bin/env python3
"""
MoleculaEntity v2 Code Generator

Generates C++ entity classes from JSON schema definitions.
Each entity is placed in its own directory (lower-case-with-hyphen).

Usage:
    python molecula_gen.py schema.json [--output-dir ./generated]
"""

import argparse
import json
import os
import re
import sys
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any, Dict, List, Optional
from datetime import datetime


@dataclass
class ForeignKey:
    entity: str
    column: str = "id"
    on_delete: str = "NO ACTION"
    on_update: str = "NO ACTION"


@dataclass
class Column:
    name: str
    cpp_type: str
    column_type: str  # ColumnType enum value
    nullable: bool = False
    unique: bool = False
    default: Optional[str] = None
    fk: Optional[ForeignKey] = None


@dataclass
class Relation:
    name: str
    entity: str
    fk_column: str


@dataclass
class Index:
    columns: List[str]
    unique: bool = False


@dataclass
class Entity:
    name: str
    table: str
    version: int
    columns: List[Column]
    relations: List[Relation] = field(default_factory=list)
    indexes: List[Index] = field(default_factory=list)


@dataclass
class Config:
    namespace: str
    output_dir: str


# Type mapping: JSON type -> (C++ type, ColumnType enum)
TYPE_MAPPING = {
    "string": ("std::string", "String"),
    "int": ("int", "Int"),
    "int32": ("int32_t", "Int"),
    "int64": ("int64_t", "Int64"),
    "double": ("double", "Double"),
    "float": ("float", "Double"),
    "bool": ("bool", "Bool"),
    "blob": ("std::vector<uint8_t>", "Blob"),
    "timestamp": ("BaseEntity::Timestamp", "Timestamp"),
}


def to_snake_case(name: str) -> str:
    """Convert CamelCase to snake_case."""
    s1 = re.sub('(.)([A-Z][a-z]+)', r'\1_\2', name)
    return re.sub('([a-z0-9])([A-Z])', r'\1_\2', s1).lower()


def to_kebab_case(name: str) -> str:
    """Convert CamelCase to kebab-case."""
    return to_snake_case(name).replace('_', '-')


def to_camel_case(name: str) -> str:
    """Convert snake_case to CamelCase."""
    parts = name.split('_')
    return ''.join(p.capitalize() for p in parts)


def get_getter_name(col: Column) -> str:
    """Get getter method name for a column."""
    camel = to_camel_case(col.name)
    if col.cpp_type == "bool" and not col.nullable:
        return f"is{camel}"
    return f"get{camel}"


def get_setter_name(col: Column) -> str:
    """Get setter method name for a column."""
    return f"set{to_camel_case(col.name)}"


def parse_schema(file_path: str) -> tuple:
    """Parse JSON schema file."""
    with open(file_path, "r") as f:
        data = json.load(f)

    config = Config(
        namespace=data.get("config", {}).get("namespace", "Generated"),
        output_dir=data.get("config", {}).get("output_dir", "generated"),
    )

    entities = []
    entity_tables = {}  # Map entity name -> table name

    # First pass: collect entity names and tables
    for entity_name, entity_data in data.get("entities", {}).items():
        table = entity_data.get("table", to_snake_case(entity_name))
        entity_tables[entity_name] = table

    # Second pass: parse full entities
    for entity_name, entity_data in data.get("entities", {}).items():
        columns = []
        for col_name, col_data in entity_data.get("columns", {}).items():
            if isinstance(col_data, str):
                # Simple format: "type": "string"
                col_type = col_data
                col_data = {"type": col_type}

            col_type = col_data.get("type", "string")
            cpp_type, column_type = TYPE_MAPPING.get(col_type, ("std::string", "String"))

            fk = None
            if "fk" in col_data:
                fk_data = col_data["fk"]
                fk_entity = fk_data.get("entity")
                fk = ForeignKey(
                    entity=fk_entity,
                    column=fk_data.get("column", "id"),
                    on_delete=fk_data.get("onDelete", "NO ACTION"),
                    on_update=fk_data.get("onUpdate", "NO ACTION"),
                )
                # FK references the table name, not entity name
                fk.table = entity_tables.get(fk_entity, to_snake_case(fk_entity))

            columns.append(Column(
                name=col_name,
                cpp_type=cpp_type,
                column_type=column_type,
                nullable=col_data.get("nullable", False),
                unique=col_data.get("unique", False),
                default=col_data.get("default"),
                fk=fk,
            ))

        relations = []
        for rel_data in entity_data.get("relations", []):
            relations.append(Relation(
                name=rel_data["name"],
                entity=rel_data["entity"],
                fk_column=rel_data["fkColumn"],
            ))

        indexes = []
        for idx_data in entity_data.get("indexes", []):
            indexes.append(Index(
                columns=idx_data.get("columns", []),
                unique=idx_data.get("unique", False),
            ))

        entities.append(Entity(
            name=entity_name,
            table=entity_data.get("table", to_snake_case(entity_name)),
            version=entity_data.get("version", 1),
            columns=columns,
            relations=relations,
            indexes=indexes,
        ))

    return config, entities


def generate_entity_header(entity: Entity, config: Config, all_entities: List[Entity]) -> str:
    """Generate entity header file content."""
    entity_class = f"{entity.name}Entity"

    # Find forward declarations needed for relations
    forward_decls = []
    for rel in entity.relations:
        forward_decls.append(f"class {rel.entity}Entity;")

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
    ]

    # Forward declarations
    for decl in forward_decls:
        lines.append(decl)
    if forward_decls:
        lines.append("")

    lines.extend([
        f"class {entity_class} : public MoleculaEntity::BaseEntity {{",
        "public:",
        f"    // Table metadata",
        f'    static constexpr const char* tableName = "{entity.table}";',
        f"    static constexpr int tableVersion = {entity.version};",
        "",
    ])

    # columns() static method
    lines.append("    static const std::vector<MoleculaEntity::ColumnMeta>& columns() {")
    lines.append("        static std::vector<MoleculaEntity::ColumnMeta> cols = {")

    for i, col in enumerate(entity.columns):
        nullable = "true" if col.nullable else "false"
        unique = "true" if col.unique else "false"
        default = f'"{col.default}"' if col.default else "std::nullopt"

        if col.fk:
            fk_str = f'MoleculaEntity::ForeignKey{{"{col.fk.table}", "{col.fk.column}", "{col.fk.on_delete}", "{col.fk.on_update}"}}'
        else:
            fk_str = "std::nullopt"

        comma = "," if i < len(entity.columns) - 1 else ""
        lines.append(f'            {{"{col.name}", MoleculaEntity::ColumnType::{col.column_type}, {nullable}, {default}, {fk_str}, {unique}}}{comma}')

    lines.append("        };")
    lines.append("        return cols;")
    lines.append("    }")
    lines.append("")

    # toValues() method
    lines.append("    [[nodiscard]] std::vector<MoleculaEntity::DbValue> toValues() const {")
    lines.append("        std::vector<MoleculaEntity::DbValue> values;")

    for col in entity.columns:
        getter = get_getter_name(col)
        if col.nullable:
            lines.append(f"        if ({col.name}_.has_value()) {{")
            if col.cpp_type in ("int", "int32_t"):
                lines.append(f"            values.push_back(static_cast<int64_t>({col.name}_.value()));")
            elif col.cpp_type == "bool":
                lines.append(f"            values.push_back(static_cast<int64_t>({col.name}_.value() ? 1 : 0));")
            else:
                lines.append(f"            values.push_back({col.name}_.value());")
            lines.append("        } else {")
            lines.append("            values.push_back(nullptr);")
            lines.append("        }")
        else:
            if col.cpp_type in ("int", "int32_t"):
                lines.append(f"        values.push_back(static_cast<int64_t>({col.name}_));")
            elif col.cpp_type == "bool":
                lines.append(f"        values.push_back(static_cast<int64_t>({col.name}_ ? 1 : 0));")
            else:
                lines.append(f"        values.push_back({col.name}_);")

    lines.append("        return values;")
    lines.append("    }")
    lines.append("")

    # fromRow() static method
    lines.append(f"    static {entity_class} fromRow(const MoleculaEntity::DbRow& row, size_t offset = 0) {{")
    lines.append(f"        {entity_class} entity;")
    lines.append("        entity.loadBaseFields(row, offset);")
    lines.append(f"        size_t i = offset + MoleculaEntity::BASE_COLUMN_COUNT;")
    lines.append("")

    for col in entity.columns:
        if col.nullable:
            lines.append(f"        if (i < row.size() && !MoleculaEntity::isNull(row[i])) {{")
            if col.cpp_type == "std::string":
                lines.append(f"            entity.{col.name}_ = MoleculaEntity::getString(row[i]);")
            elif col.cpp_type in ("int", "int32_t"):
                lines.append(f"            entity.{col.name}_ = static_cast<{col.cpp_type}>(MoleculaEntity::getInt64(row[i]));")
            elif col.cpp_type == "int64_t":
                lines.append(f"            entity.{col.name}_ = MoleculaEntity::getInt64(row[i]);")
            elif col.cpp_type in ("double", "float"):
                lines.append(f"            entity.{col.name}_ = MoleculaEntity::getDouble(row[i]);")
            elif col.cpp_type == "bool":
                lines.append(f"            entity.{col.name}_ = MoleculaEntity::getBool(row[i]);")
            else:
                lines.append(f"            entity.{col.name}_ = MoleculaEntity::getString(row[i]);")
            lines.append("        }")
            lines.append("        ++i;")
        else:
            lines.append(f"        if (i < row.size()) {{")
            if col.cpp_type == "std::string":
                lines.append(f"            entity.{col.name}_ = MoleculaEntity::getString(row[i]);")
            elif col.cpp_type in ("int", "int32_t"):
                lines.append(f"            entity.{col.name}_ = static_cast<{col.cpp_type}>(MoleculaEntity::getInt64(row[i]));")
            elif col.cpp_type == "int64_t":
                lines.append(f"            entity.{col.name}_ = MoleculaEntity::getInt64(row[i]);")
            elif col.cpp_type in ("double", "float"):
                lines.append(f"            entity.{col.name}_ = MoleculaEntity::getDouble(row[i]);")
            elif col.cpp_type == "bool":
                lines.append(f"            entity.{col.name}_ = MoleculaEntity::getBool(row[i]);")
            else:
                lines.append(f"            entity.{col.name}_ = MoleculaEntity::getString(row[i]);")
            lines.append("        }")
            lines.append("        ++i;")
        lines.append("")

    lines.append("        return entity;")
    lines.append("    }")
    lines.append("")

    # Getters
    lines.append("    // Getters")
    for col in entity.columns:
        getter = get_getter_name(col)
        if col.nullable:
            if col.cpp_type in ("int", "int32_t", "int64_t", "double", "float", "bool"):
                lines.append(f"    [[nodiscard]] std::optional<{col.cpp_type}> {getter}() const noexcept {{ return {col.name}_; }}")
            else:
                lines.append(f"    [[nodiscard]] const std::optional<{col.cpp_type}>& {getter}() const noexcept {{ return {col.name}_; }}")
        else:
            if col.cpp_type in ("int", "int32_t", "int64_t", "double", "float", "bool"):
                lines.append(f"    [[nodiscard]] {col.cpp_type} {getter}() const noexcept {{ return {col.name}_; }}")
            else:
                lines.append(f"    [[nodiscard]] const {col.cpp_type}& {getter}() const noexcept {{ return {col.name}_; }}")
    lines.append("")

    # Setters
    lines.append("    // Setters")
    for col in entity.columns:
        setter = get_setter_name(col)
        if col.nullable:
            if col.cpp_type == "std::string":
                lines.append(f"    void {setter}(const {col.cpp_type}& value) {{ {col.name}_ = value; }}")
                lines.append(f"    void {setter}(std::nullopt_t) {{ {col.name}_ = std::nullopt; }}")
            else:
                lines.append(f"    void {setter}({col.cpp_type} value) {{ {col.name}_ = value; }}")
                lines.append(f"    void {setter}(std::nullopt_t) {{ {col.name}_ = std::nullopt; }}")
        else:
            if col.cpp_type == "std::string":
                lines.append(f"    void {setter}(const {col.cpp_type}& value) {{ {col.name}_ = value; }}")
            else:
                lines.append(f"    void {setter}({col.cpp_type} value) {{ {col.name}_ = value; }}")
    lines.append("")

    # Relations getters/setters
    if entity.relations:
        lines.append("    // Relations")
        for rel in entity.relations:
            rel_entity = f"{rel.entity}Entity"
            getter_name = f"get{to_camel_case(rel.name)}"
            setter_name = f"set{to_camel_case(rel.name)}"
            member_name = f"{rel.name}_"

            lines.append(f"    [[nodiscard]] const std::vector<{rel_entity}>& {getter_name}() const noexcept {{ return {member_name}; }}")
            lines.append(f"    void {setter_name}(std::vector<{rel_entity}> value) {{ {member_name} = std::move(value); }}")
        lines.append("")

    # Private members
    lines.append("private:")
    for col in entity.columns:
        if col.nullable:
            lines.append(f"    std::optional<{col.cpp_type}> {col.name}_;")
        else:
            if col.cpp_type == "bool":
                default_val = "false"
                lines.append(f"    {col.cpp_type} {col.name}_{{{default_val}}};")
            elif col.cpp_type in ("int", "int32_t", "int64_t"):
                lines.append(f"    {col.cpp_type} {col.name}_{{0}};")
            elif col.cpp_type in ("double", "float"):
                lines.append(f"    {col.cpp_type} {col.name}_{{0.0}};")
            else:
                lines.append(f"    {col.cpp_type} {col.name}_;")

    # Relation members
    for rel in entity.relations:
        rel_entity = f"{rel.entity}Entity"
        lines.append(f"    std::vector<{rel_entity}> {rel.name}_;")

    lines.append("};")
    lines.append("")
    lines.append(f"}} // namespace {config.namespace}")
    lines.append("")

    return "\n".join(lines)


def generate_entities_header(entities: List[Entity], config: Config) -> str:
    """Generate the main Entities.hpp include file."""
    lines = [
        "#pragma once",
        "",
        "// MoleculaEntity v2 Generated Code",
        f"// Generated at: {datetime.now().isoformat()}",
        "",
        "#include <MoleculaEntity/MoleculaEntity.hpp>",
        "",
    ]

    for entity in entities:
        dir_name = to_kebab_case(entity.name)
        lines.append(f'#include "{dir_name}/{entity.name}Entity.hpp"')

    lines.append("")

    return "\n".join(lines)


def generate_schema_example() -> str:
    """Generate example schema.json content."""
    return """{
  "config": {
    "namespace": "MyApp",
    "output_dir": "src/models"
  },
  "entities": {
    "User": {
      "table": "users",
      "version": 1,
      "columns": {
        "name": { "type": "string" },
        "email": { "type": "string", "unique": true },
        "age": { "type": "int", "nullable": true }
      },
      "relations": [
        {
          "name": "orders",
          "entity": "Order",
          "fkColumn": "user_id"
        }
      ],
      "indexes": [
        { "columns": ["email"], "unique": true }
      ]
    },
    "Order": {
      "table": "orders",
      "version": 1,
      "columns": {
        "user_id": {
          "type": "string",
          "fk": {
            "entity": "User",
            "column": "id",
            "onDelete": "CASCADE"
          }
        },
        "total": { "type": "int64" },
        "status": { "type": "string" }
      },
      "indexes": [
        { "columns": ["user_id"] }
      ]
    }
  }
}
"""


def main():
    parser = argparse.ArgumentParser(
        description="MoleculaEntity v2 - Generate C++ entity classes from JSON schema"
    )
    parser.add_argument("schema", nargs="?", help="Path to schema.json file")
    parser.add_argument("--output-dir", "-o", help="Output directory (overrides schema config)")
    parser.add_argument("--example", action="store_true", help="Print example schema.json")

    args = parser.parse_args()

    if args.example:
        print(generate_schema_example())
        return

    if not args.schema:
        parser.print_help()
        sys.exit(1)

    if not os.path.exists(args.schema):
        print(f"Error: Schema file not found: {args.schema}")
        sys.exit(1)

    config, entities = parse_schema(args.schema)

    if args.output_dir:
        config.output_dir = args.output_dir

    output_path = Path(config.output_dir)
    output_path.mkdir(parents=True, exist_ok=True)

    print(f"MoleculaEntity v2 Generator")
    print(f"Generating {len(entities)} entities to {output_path}/")
    print()

    # Generate entity files in their own directories
    for entity in entities:
        dir_name = to_kebab_case(entity.name)
        entity_dir = output_path / dir_name
        entity_dir.mkdir(parents=True, exist_ok=True)

        entity_file = entity_dir / f"{entity.name}Entity.hpp"
        with open(entity_file, "w") as f:
            f.write(generate_entity_header(entity, config, entities))
        print(f"  {dir_name}/{entity.name}Entity.hpp")

    # Generate main Entities.hpp
    entities_file = output_path / "Entities.hpp"
    with open(entities_file, "w") as f:
        f.write(generate_entities_header(entities, config))
    print(f"  Entities.hpp")

    print()
    print("Done!")
    print()
    print("Usage example:")
    print(f"""
#include <MoleculaEntity/MoleculaEntity.hpp>
#include <MoleculaEntity/drivers/SQLite3Driver.hpp>
#include "{config.output_dir}/Entities.hpp"

int main() {{
    // Setup provider
    MoleculaEntity::Provider provider;
    provider.setDriver(std::make_shared<MoleculaEntity::SQLite3Driver>("app.db"));

    // Register entities (order matters for FK dependencies)
    provider.registerEntities<{', '.join(f'{config.namespace}::{e.name}Entity' for e in entities)}>();

    // Sync schema
    provider.synchronize();

    // Use repositories
    auto& repo = provider.getRepository<{config.namespace}::{entities[0].name}Entity>();

    return 0;
}}
""")


if __name__ == "__main__":
    main()
