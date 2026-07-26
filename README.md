# Jimp-v1

Building and Running JPP

1. Build the Engine

gcc -O2 engine.c -lcjson -o engine

Explanation

- "gcc" → The GNU C compiler.
- "-O2" → Enables compiler optimizations for better performance.
- "engine.c" → The engine source code.
- "-lcjson" → Links the cJSON library required by the engine.
- "-o engine" → Sets the output executable name to engine.

After running this command, an executable named "engine" will be created.

---

2. Run a JPP Program

./engine app.jpp

Explanation

- "./engine" → Starts the JPP engine.
- "app.jpp" → The JPP source file to execute.

The engine reads the "app.jpp" file, parses its contents, and executes the program.

Example:

PRINT "Hello, World!"

Run it with:

./engine app.jpp

Output:

Hello, World!
