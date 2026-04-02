# Tyne Standard Library

This directory contains the Tyne standard library, implemented as `.tyne` files that can be imported into Tyne programs.

## Modules

### Core Modules
- `stdlib.tyne` - Main stdlib file that imports all modules
- `io.tyne` - Input/output operations
- `math.tyne` - Mathematical functions
- `string.tyne` - String manipulation
- `list.tyne` - List operations and complex list syntax

### Utility Modules
- `hash.tyne` - Hashing functions
- `crypto.tyne` - Cryptographic operations
- `filesystem.tyne` - File system operations
- `collections.tyne` - Advanced data structures (HashMap, HashSet, Queue, Stack)
- `networking.tyne` - Network communication
- `time.tyne` - Time and date operations

## Usage

```tyne
import "stdlib"

# Now all stdlib functions are available
printf("Hello, World!\n")
list numbers = [int32 1, int32 2, int32 3]
listAppend(numbers, int32 4)
```

## Complex List Syntax

Tyne supports complex list initialization:

```tyne
# Mixed types
list mixed = [int32 5, string "hello", list [int32 1, int32 2]]

# Typed repetition
list repeated = [int32:string 3:"abc"]  # ["abc", "abc", "abc"]
```

## Runtime Implementation

The runtime implementation is in `../src/runtime.cpp`, which provides C++ implementations for all builtin functions.