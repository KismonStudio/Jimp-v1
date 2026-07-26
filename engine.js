const fs = require('fs');
const rules = JSON.parse(fs.readFileSync('core.json', 'utf8'));
const patterns = rules[0];
const ops = rules[1];
const delimiters = rules[2] || [];
const errors = rules[3] || {}; 
const colors = rules[4] || {}; 
const string_delimiters = rules[5] || []; 
const whitespace = rules[6] || [];
const statement_separators = rules[7] || [];
const mem = {};

function coerce(v) {
  if (typeof v === 'string' && !isNaN(v) && v !== '') return Number(v);
  return v;
}

function run(filename, maxErrors = 10) {
  const actualFile = typeof filename === 'string' ? filename.replace(/["'`]/g,'') : filename;
  
  if (!fs.existsSync(actualFile)) {
      if (errors["ERR_FILE"]) {
          errors["ERR_FILE"].forEach(item => {
              const formattedText = item.text.replace(/\{file\}/g, actualFile);
              console.log((colors[item.color] || "") + formattedText + (colors["reset"] || ""));
          });
      } else {
          console.log("Error: File not found: " + actualFile);
      }
      return;
  }

  const rawSrc = fs.readFileSync(actualFile, 'utf8');
  
  const statements = [];
  let currentToken = "";
  let currentStatement = [];
  let insideString = null;
  let lineNum = 1;
  let stmtLine = 1;

  for (let i = 0; i < rawSrc.length; i++) {
    let char = rawSrc[i];

    if (char === '\n') {
        lineNum++;
    }

    if (string_delimiters.includes(char) && (i === 0 || rawSrc[i-1] !== '\\')) {
        if (insideString === char) {
            insideString = null;
            currentToken += char;
            currentStatement.push(currentToken);
            currentToken = "";
            continue;
        } else if (!insideString) {
            if (currentToken !== "" && !delimiters.includes(currentToken)) {
                currentStatement.push(currentToken);
                currentToken = "";
            }
            insideString = char;
            currentToken += char;
            continue;
        }
    }

    if (insideString) {
        currentToken += char;
        continue;
    }

    if (statement_separators.includes(char)) {
        if (currentToken !== "") {
            currentStatement.push(currentToken);
            currentToken = "";
        }
        if (currentStatement.length > 0) {
            statements.push({ tokens: currentStatement, line: stmtLine, raw: currentStatement.join(" ") });
            currentStatement = [];
        }
        stmtLine = lineNum; 
        continue;
    }

    if (whitespace.includes(char)) {
        if (currentToken !== "") {
            currentStatement.push(currentToken);
            currentToken = "";
        }
        continue;
    }

    if (delimiters.includes(char)) {
        if (currentToken !== "") {
            currentStatement.push(currentToken);
            currentToken = "";
        }
        currentStatement.push(char);
        continue;
    }

    currentToken += char;
  }

  if (currentToken !== "") {
      currentStatement.push(currentToken);
  }
  if (currentStatement.length > 0) {
      statements.push({ tokens: currentStatement, line: stmtLine, raw: currentStatement.join(" ") });
  }

  const prevEnv = globalThis._ENV || { L: 0, skip: false, stack: [] };
  globalThis._ENV = { L: 0, skip: false, stack: prevEnv.stack || [] };
  
  let errorCount = 0;

  try {
      while (globalThis._ENV.L < statements.length) {
        const stmt = statements[globalThis._ENV.L];
        const tokens = stmt.tokens;

        if (tokens.length === 0) {
            globalThis._ENV.L++;
            continue; 
        }

        function throwError(errKey, opName = "") {
            const errorLines = errors[errKey] || [];
            if (errorLines.length === 0) {
                console.log(`Error: ${errKey} at line ${stmt.line}`);
                return;
            }
            const lineStr = stmt.raw;
            const pointerLen = lineStr.length > 50 ? 50 : lineStr.length;
            const pointer = "^".repeat(pointerLen);

            errorLines.forEach(item => {
                const formattedText = item.text
                    .replace(/\{file\}/g, actualFile)
                    .replace(/\{line\}/g, stmt.line)
                    .replace(/\{code\}/g, lineStr.length > 50 ? lineStr.slice(0, 50) + "..." : lineStr.replace(/\n/g, '\\n'))
                    .replace(/\{pointer\}/g, pointer)
                    .replace(/\{op\}/g, opName);
                
                const colorCode = colors[item.color] || "";
                console.log(colorCode + formattedText + (colors["reset"] || ""));
            });
            console.log();
        }

        let matched = false;
        for (let r = 0; r < patterns.length; r++) {
          const pat = patterns[r][0];
          let opName = patterns[r][1];
          const target = patterns[r][3];
          const isControl = patterns[r][4] === true; 

          const hasWildcard = pat[pat.length - 1] === "{...}";
          if (!hasWildcard && pat.length !== tokens.length) continue;
          if (hasWildcard && tokens.length < pat.length - 1) continue;

          const ctx = {};
          let ok = true;
          for (let k = 0; k < pat.length; k++) {
            if (pat[k] === "{...}") break; 
            if (pat[k][0] === '{') {
                ctx[pat[k]] = tokens[k];
            }
            else if (pat[k] !== tokens[k]) { ok = false; break; }
          }
          if (!ok) continue;

          matched = true;

          if (globalThis._ENV.skip && !isControl) break; 

          if (opName[0] === '{') opName = ctx[opName];
          
          if (!ops[opName]) {
              throwError("ERR_UNDEF", opName);
              errorCount++;
              break;
          }

          const variant = (ops[opName][1] ?? ops[opName][0]);
          const fnArgs = variant.slice(1).map(s => {
            if (s === "null") return null;
            const v = (typeof s === 'string' && s[0] === '{') ? ctx[s] : s;
            const resolved = (mem[v] !== undefined) ? mem[v] : v;
            return coerce(resolved);
          });

          const fnName = variant[0];
          try {
              const func = eval(fnName);
              if (func) {
                  const processedArgs = fnArgs.map(a => {
                      if (typeof a === 'string' && string_delimiters.includes(a[0])) {
                          return a.slice(1, -1).replace(/\\(["'`])/g, '$1');
                      }
                      return a;
                  });
                  const result = func(...processedArgs);
                  if (target) {
                    const key = (target[0] === '{') ? ctx[target] : target;
                    mem[key] = result;
                  }
              }
          } catch (jsErr) {
              if (!globalThis._ENV.skip) {
                  throwError("ERR_JS", opName);
              }
          }
          break;
        }

        if (!matched && tokens.length > 0 && !globalThis._ENV.skip) {
            throwError("ERR_SYNTAX");
            errorCount++;
        }

        if (errorCount >= maxErrors) break;
        
        globalThis._ENV.L++; 
      }
  } catch (fatalErr) {
      if (errors["ERR_JS"]) {
          console.log((colors["red_bold"] || "") + "Fatal Engine Error suppressed." + (colors["reset"] || ""));
      }
  }
  
  if (errorCount > 0) {
      console.log(`error: aborting due to ${errorCount} previous error${errorCount !== 1 ? 's' : ''}\n`);
      if (maxErrors === 10) console.log("Hint: use --max-errors=50 to increase the limit.\n");
      process.exit(1);
  }

  globalThis._ENV = prevEnv;
}

globalThis.run = run;
globalThis.runJPP = run;

let maxErrors = 10;
let targetFile = null;

for (let i = 2; i < process.argv.length; i++) {
    if (process.argv[i].startsWith("--max-errors=")) {
        maxErrors = parseInt(process.argv[i].split("=")[1]) || 10;
    } else if (!targetFile) {
        targetFile = process.argv[i];
    }
}

if (targetFile) {
    run(targetFile, maxErrors);
}
