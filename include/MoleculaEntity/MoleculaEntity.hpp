#pragma once

// Core types
#include "IDatabaseDriver.hpp"
#include "ColumnMeta.hpp"
#include "BaseEntity.hpp"

// Query and Repository
#include "QueryBuilder.hpp"
#include "Repository.hpp"

// Provider (main entry point)
#include "Provider.hpp"

// Utilities
#include "UuidGenerator.hpp"

// Legacy compatibility aliases
namespace MoleculaEntity {
    using IDatabaseManager = IDatabaseDriver;
    using DatabaseManagerPtr = DatabaseDriverPtr;
}
