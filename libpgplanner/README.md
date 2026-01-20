# libpgplanner - Standalone PostgreSQL Query Planner

A C library that exposes the PostgreSQL query planner for use by external databases (e.g., Rust-based databases via FFI) without requiring a running PostgreSQL instance.

## Overview

`libpgplanner` extracts PostgreSQL's query planner and makes it available as a standalone library. This allows other database systems to leverage PostgreSQL's sophisticated query optimization capabilities.

### Features

- **No PostgreSQL Server Required**: The library operates completely standalone
- **Full SQL Parser**: Uses PostgreSQL's battle-tested SQL parser
- **Support for Complex Queries**:
  - SELECT with WHERE, ORDER BY, LIMIT
  - JOINs (INNER, LEFT, RIGHT, FULL)
  - Aggregates (COUNT, SUM, AVG, MIN, MAX)
  - GROUP BY and HAVING
  - Window functions (ROW_NUMBER, SUM OVER, etc.)
  - CTEs (Common Table Expressions)
  - Subqueries
- **Thread Safety**: Protected by mutex (single planning operation at a time)
- **FFI-Safe Memory Management**: Safe for use across language boundaries

## Building

### Prerequisites

- PostgreSQL source code (this library must be built within the PostgreSQL tree)
- GCC or compatible C compiler
- GNU Make

### Build Steps

```bash
# First, build PostgreSQL
cd /path/to/postgres
./configure
make

# Then build libpgplanner
cd libpgplanner
make
```

This creates:
- `libpgplanner.so` - Shared library
- `libpgplanner.a` - Static library

### Running Tests

```bash
make tests
make check
```

### Building Examples

```bash
make examples
./examples/simple_select
./examples/join_query
./examples/aggregate_query
```

## Quick Start

```c
#include "pgplanner.h"

int main() {
    // Initialize the library
    if (pgplanner_init() != PGPLANNER_OK) {
        fprintf(stderr, "Failed to initialize\n");
        return 1;
    }

    // Register a table schema
    PgColumnDef cols[] = {
        {"id", "int4", -1, true},
        {"name", "text", -1, false},
    };

    PgTableDef table = {
        .table_name = "users",
        .num_columns = 2,
        .columns = cols,
    };

    pgplanner_register_table(&table);

    // Plan a query
    PgPlanResult *result = pgplanner_plan("SELECT * FROM users WHERE id = 1");

    if (result->error_code == PGPLANNER_OK) {
        printf("Plan: %s\n", result->plan_text);
    } else {
        printf("Error: %s\n", result->error_message);
    }

    pgplanner_free_result(result);
    pgplanner_shutdown();
    return 0;
}
```

## API Reference

### Library Lifecycle

```c
// Initialize the library (call once at startup)
int pgplanner_init(void);

// Shutdown and free all resources
void pgplanner_shutdown(void);

// Check if initialized
bool pgplanner_is_initialized(void);

// Get version string
const char *pgplanner_version(void);
```

### Table Registration

```c
// Column definition
typedef struct PgColumnDef {
    const char *name;       // Column name
    const char *type_name;  // Type: "int4", "text", "timestamp", etc.
    int32_t typmod;         // Type modifier (-1 for none)
    bool not_null;          // NOT NULL constraint
} PgColumnDef;

// Index definition
typedef struct PgIndexDef {
    const char *name;           // Index name
    int num_columns;            // Number of columns
    const int *column_nums;     // 1-based column numbers
    const char *access_method;  // "btree", "hash"
    bool is_unique;
    bool is_primary;
} PgIndexDef;

// Table definition
typedef struct PgTableDef {
    const char *schema_name;    // NULL for "public"
    const char *table_name;
    int num_columns;
    const PgColumnDef *columns;
    int num_indexes;
    const PgIndexDef *indexes;
} PgTableDef;

// Register a table
PgOid pgplanner_register_table(const PgTableDef *table);

// Find a table by name
PgOid pgplanner_find_table(const char *schema_name, const char *table_name);

// List all registered tables
PgOid *pgplanner_list_tables(void);
```

### Query Planning

```c
// Plan result structure
typedef struct PgPlanResult {
    char *plan_text;      // Plan in nodeToString format
    int error_code;       // 0 = success
    char *error_message;  // Error message if failed
} PgPlanResult;

// Plan a SQL query
PgPlanResult *pgplanner_plan(const char *sql);

// Free a plan result
void pgplanner_free_result(PgPlanResult *result);
```

### Supported Types

```c
// Check if a type is supported
bool pgplanner_type_exists(const char *type_name);

// List all supported types
const char **pgplanner_list_types(void);
```

**Built-in types:**
- Boolean: `bool`
- Integers: `int2`, `int4`, `int8`
- Floating point: `float4`, `float8`, `numeric`
- Text: `text`, `varchar`, `bpchar`, `char`, `name`
- Date/Time: `date`, `time`, `timetz`, `timestamp`, `timestamptz`, `interval`
- Binary: `bytea`
- Other: `oid`, `uuid`, `json`, `jsonb`

### Error Handling

```c
// Error codes
enum PgPlannerError {
    PGPLANNER_OK = 0,
    PGPLANNER_ERROR_NOT_INITIALIZED = 1,
    PGPLANNER_ERROR_ALREADY_INITIALIZED = 2,
    PGPLANNER_ERROR_PARSE_ERROR = 3,
    PGPLANNER_ERROR_ANALYZE_ERROR = 4,
    PGPLANNER_ERROR_PLAN_ERROR = 5,
    PGPLANNER_ERROR_INVALID_TABLE = 6,
    PGPLANNER_ERROR_INVALID_TYPE = 7,
    PGPLANNER_ERROR_OUT_OF_MEMORY = 8,
    PGPLANNER_ERROR_INTERNAL = 99
};

// Get last error message
const char *pgplanner_get_last_error(void);

// Get error name string
const char *pgplanner_error_name(int error_code);
```

## Rust FFI Usage

Here's how to use the library from Rust:

```rust
use std::ffi::{CStr, CString};
use std::os::raw::{c_char, c_int};

#[repr(C)]
pub struct PgPlanResult {
    plan_text: *mut c_char,
    error_code: c_int,
    error_message: *mut c_char,
}

extern "C" {
    fn pgplanner_init() -> c_int;
    fn pgplanner_shutdown();
    fn pgplanner_plan(sql: *const c_char) -> *mut PgPlanResult;
    fn pgplanner_free_result(result: *mut PgPlanResult);
}

fn main() {
    unsafe {
        if pgplanner_init() != 0 {
            panic!("Failed to initialize");
        }

        // Register tables here...

        let sql = CString::new("SELECT * FROM users").unwrap();
        let result = pgplanner_plan(sql.as_ptr());

        if (*result).error_code == 0 {
            let plan = CStr::from_ptr((*result).plan_text);
            println!("Plan: {}", plan.to_str().unwrap());
        }

        pgplanner_free_result(result);
        pgplanner_shutdown();
    }
}
```

## Thread Safety

The library uses a global mutex to serialize planning operations. Only one query can be planned at a time. This is due to PostgreSQL's use of global state.

**Safe usage patterns:**
- Single-threaded: No restrictions
- Multi-threaded: All calls are automatically serialized

## Limitations

1. **No Query Execution**: This library only plans queries; it does not execute them
2. **No Statistics**: Uses default statistics estimates (no pg_statistic data)
3. **No Parallel Query**: Parallel query is disabled in standalone mode
4. **No Views/Rules**: Views and rules are not supported
5. **Limited Function Support**: Only built-in functions are available

## Plan Output Format

Plans are returned in PostgreSQL's `nodeToString()` format, which is a serialized representation of the plan tree. Key node types you'll see:

- `PLANNEDSTMT`: Top-level planned statement
- `SEQSCAN`: Sequential table scan
- `INDEXSCAN`: Index scan
- `NESTLOOP`, `HASHJOIN`, `MERGEJOIN`: Join methods
- `SORT`: Sorting operation
- `AGG`: Aggregation
- `WINDOWAGG`: Window function processing
- `LIMIT`: LIMIT/OFFSET processing

## License

This library is part of PostgreSQL and is released under the PostgreSQL License.

## Contributing

Bug reports and pull requests are welcome. Please ensure:
1. Code follows PostgreSQL coding conventions
2. Tests are added for new functionality
3. Documentation is updated
