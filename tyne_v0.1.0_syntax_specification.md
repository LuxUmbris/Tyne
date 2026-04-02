# Tyne Version 0.1.0 Syntax Specification

## COMMENTS

Single‑line:

    # This is a comment

Block comment:

    ##
    This is a multi‑line comment.
    It ends when another '##' appears.
    ##


## IMPORTS

Import a module:

    import io;

Import nested modules:

    import std.math;

Import by string:

    import "path/to/module";


## TYPES

Built‑in types:

    int32
    int64
    int128
    uint32
    uint64
    uint128
    float
    double
    list

Examples:

    int32 x = 10;
    double y = 3.14;
    list values = [int32 1, int32 2, int32 3];


## VARIABLES

Declaration:

    int32 count;
    double pi = 3.14159;

Assignment:

    count = 42;
    obj.value = 99;


## EXPRESSIONS

Basic:

    1 + 2 * 3
    (a + b) * c
    x >= 10 && y < 5

Member access:

    obj.field
    a.b.c

Function call:

    print("Hello");
    sqrt(16.0);

Typed value:

    int32 5
    double 3.14

List literal:
    [1, 2, 3]
    [int32 42, string "hello", list [1, 2, 3]]


## CONTROL FLOW

### IF/ELSE
    if (x > 0) {
        println("positive");
    } else {
        println("non‑positive");
    }

### WHILE
    while (i < 10) {
        i = i + 1;
    }

### FOR
    for (i = 0; i < 10; i = i + 1) {
        println(i);
    }


## FUNCTIONS

Function declaration:

    int32 factorial(int32 n) {
        if (n <= 1) {
            return 1;
        }
        return n * factorial(n - 1);
    }

Multiple parameters:

    double mix(double a, double b, double t) {
        return a * (1 - t) + b * t;
    }


## CLASSES

A. Inline constructor:

    class Point(int32 x, int32 y) {
        {
            # constructor body
            println("Point created");
        }

        int32 sum() {
            return x + y;
        }
    }

B. Named constructor:

    class Person {
        function Person(string name, int32 age) {
            {
                println("Constructing person");
            }
        }

        string greet() {
            return "Hello!";
        }
    }

Field declarations inside class:

    class Data {
        int32 value;
        double weight;
    }


## STRUCTS

Struct with fields:

    struct Point {
        int32 x;
        int32 y;
    }


## NAMESPACES

Nested namespaces:

    namespace math.geometry {
        struct Point {
            int32 x;
            int32 y;
        }
    }


## ENTRY POINT

Program entry:

    entry {
        println("Hello, Tyne!");
    }


## FULL EXAMPLE PROGRAM

```
import io;
import math;

int32 factorial(int32 n) {
    if (n <= 1) {
        return 1;
    }
    return n * factorial(n - 1);
}

class Foo(int32 a, int32 b) {
    {
        println("Constructing Foo");
    }

    int32 sum() {
        return a + b;
    }
}

entry {
    println("Hello, Tyne!");

    int32 num = 5;
    int32 result = factorial(num);

    println(result);

    list mixed = [int32 42, string "hello", list [1, 2, 3]];
    println(length(mixed));

    Foo f = Foo(3, 4);
    println(f.sum());
}
```
