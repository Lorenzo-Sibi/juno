# JSON Implementation Status

This document tracks the compliance of this library with **RFC 8259 for the JavaScript Object Notation JSON** and outlines the roadmap for full **JSON-RPC 2.0** readiness.

Current State: **Alpha / Prototype** 
Target Standards: **RFC 8259**, **JSON-RPC 2.0**

---

## RFC 8259 Compliance Matrix

This section details which parts of the official JSON specification are implemented, which are missing, and which are partially complete.

### 1. Grammar & Encodings
| Feature | RFC Section | Status | Notes |
| :--- | :---: | :---: | :--- |
| **UTF-8 Encoding** | §8.1 | ✅ | Lexer assumes and validates UTF-8. Escaped unicode (`\uXXXX`) is correctly decoded to UTF-8. |
| **JSON Text (Root)** | §2 | ✅ | **Violation**: Parser currently enforces the root element to be an `Object`. RFC allows any JSON value (Array, String, Number, etc.) at the root. |
| **Whitespace** | §2 | ✅ | Correctly skipped between tokens. |

### 2. Values
| Feature | RFC Section | Status | Notes |
| :--- | :---: | :---: | :--- |
| **Objects** | §4 | ✅ | Implemented. |
| **Arrays** | §5 | ✅ | Implemented. |
| **Numbers** | §6 | ✅ | **Partial**: All numbers are stored as C `double`. While valid per RFC, this causes precision loss for large integers (> $2^{53}$) often used in JSON-RPC IDs. |
| **Strings** | §7 | ✅ | Implemented, including escape sequence parsing (`\"`, `\\`, `\b`, `\f`, `\n`, `\r`, `\t`, `\uXXXX`). |
| **Literals** | §3 | ✅ | `true`, `false`, and `null` are correctly tokenized and stored. |

### 3. Parsing Logic & Safety
| Feature | RFC Section | Status | Notes |
| :--- | :---: | :---: | :--- |
| **Recursion Depth** | N/A | ✅ | Implemented. Further testing needed. |
| **Duplicate Keys** | §4 | ⚠️ | **Permissive**: The parser allows duplicate keys (e.g., `{"a":1, "a":2}`). RFC states names *SHOULD* be unique. Current behavior preserves both. |
| **Serialization** | N/A | ❌ | **Missing**: There is a debug print (`jp_print_ast`), but no standard `json_stringify` function to produce valid JSON text. |

---

## (small) Development Roadmap

The following tasks are prioritized to move the library from prototype to production-ready for JSON-RPC.

### Priority 1: Core Specification Compliance
- [x] **~~Fix Root Element Restriction~~**
    - *Current*: `jp_parse` calls `jp_parse_obj`.
    - *Goal*: `jp_parse` should call `jp_parse_value` to allow any root type.
    - *Ref*: `jsonp.c:166`
- [x] **~~Precise Integer Support~~**
    - *Current*: `union` uses only `double nvalue`.
    - *Goal*: Add `int64_t ivalue` to `JsonNode`. Update lexer to distinguish integers from floats. Essential for 64-bit IDs.

### Priority 2: Architecture & Stability
- [ ] **Refactor Build System**
    - *Task*: Split `jsont.h` into interface (`.h`) and implementation (`.c`).
    - *Task*: Remove `main()` from `jsonp.c` to allow linking as a library. Move demo code to `demo.c`.
- [x] ~~**Fix Test Suite**~~
    - *Task*: `tests/test_main.c` uses a non-existent API. Rewrite it to use `jp_parse` and `JsonNode` fields directly.
- [ ] **Thread-Safe Error Handling and gneral errors**
    - *Current*: Uses global `error_node`.
    - *Goal*: Return errors via a context struct or return value to support concurrent request handling.
    - Find a way to get rid of the *err_msg in the JToken struct so to save memory.
### Priority 3: JSON-RPC Features
- [ ] **Implement Serialization (`stringify`)**
    - *Task*: Create `char* jp_stringify(JsonNode *root)` to convert AST back to string. Required for sending JSON-RPC responses.
- [ ] **Implement Safe Accessors**
    - *Task*: Add helper functions:
        - `JsonNode* jp_get(JsonNode *obj, const char *key)`
        - `int64_t jp_get_int(JsonNode *node, int64_t default)`
        - `const char* jp_get_str(JsonNode *node, const char *default)`

### Priority 4: Tests tests tests!
---

## Contribution (if any ;)) Guide

### How to use the code
1. **Lexer (`jsont.h`)**: A single-header style tokenizer.
2. **Parser (`jsonp.c`)**: Builds a DOM-style AST.
    * Nodes are linked lists (`first_child` -> `next_sibling`).
    * Use `jp_parse(string, len)` to get the root node.
    * Use `jp_free_ast(root)` to clean up.

### Coding Style
* **C Standard**: C99
* **Memory**: All nodes are heap-allocated. Strings are copies.
* **Error Handling**: Currently returns a node with `type == JND_ERROR`. Always check `jp_is_error(node)` after parsing.

### Adding a Feature
1. **Pick a task** from the checklist above.
2. **Write a test** in `tests/` that reproduces the missing feature or bug.
3. **Implement** the feature.
4. **Run tests**: `make test` (once Makefile is fixed).