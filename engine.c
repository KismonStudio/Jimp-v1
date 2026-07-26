#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <ctype.h>
#include <cjson/cJSON.h>

cJSON *rules, *patterns, *ops, *delimiters, *errors, *colors;
cJSON *string_delimiters, *whitespace, *stmt_separators;

bool is_in_array(char c, cJSON* arr) {
    if (!arr) return false;
    char str_c[2] = {c, '\0'};
    cJSON* item;
    cJSON_ArrayForEach(item, arr) {
        if (item->valuestring && strcmp(item->valuestring, str_c) == 0) return true;
    }
    return false;
}

bool is_str_delim(char c) { return is_in_array(c, string_delimiters); }
bool is_delim(char c) { return is_in_array(c, delimiters); }
bool is_ws(char c) { return is_in_array(c, whitespace); }
bool is_stmt_sep(char c) { return is_in_array(c, stmt_separators); }
bool is_delim_str(const char* s) {
    if (!delimiters || !s) return false;
    cJSON* item;
    cJSON_ArrayForEach(item, delimiters) {
        if (item->valuestring && strcmp(item->valuestring, s) == 0) return true;
    }
    return false;
}

void throwError(const char* errKey, const char* actualFile, int line_num, const char* line_str, const char* opName) {
    cJSON* errorLines = cJSON_GetObjectItemCaseSensitive(errors, errKey);
    if (!errorLines) { printf("Error: %s at line %d\n", errKey, line_num); return; }
    
    char safe_line_str[4096] = "";
    int safe_idx = 0;
    for (int k = 0; line_str[k]; k++) {
        if (line_str[k] == '\n') { safe_line_str[safe_idx++] = '\\'; safe_line_str[safe_idx++] = 'n'; }
        else { safe_line_str[safe_idx++] = line_str[k]; }
        if (safe_idx >= 4000) break;
    }
    safe_line_str[safe_idx] = '\0';
    
    int len = strlen(safe_line_str);
    char pointer[1024];
    if (len >= sizeof(pointer)) len = sizeof(pointer) - 1;
    for (int i = 0; i < len; i++) pointer[i] = '^';
    pointer[len] = '\0';
    
    cJSON* item;
    cJSON_ArrayForEach(item, errorLines) {
        cJSON* text_node = cJSON_GetObjectItemCaseSensitive(item, "text");
        cJSON* color_node = cJSON_GetObjectItemCaseSensitive(item, "color");
        if (!text_node || !text_node->valuestring) continue;
        char* fmt = text_node->valuestring;
        char color_code[32] = "";
        if (color_node && color_node->valuestring && colors) {
            cJSON* col = cJSON_GetObjectItemCaseSensitive(colors, color_node->valuestring);
            if (col && col->valuestring) strcpy(color_code, col->valuestring);
        }
        char reset_code[32] = "";
        if (colors) {
            cJSON* res = cJSON_GetObjectItemCaseSensitive(colors, "reset");
            if (res && res->valuestring) strcpy(reset_code, res->valuestring);
        }
        char buffer[2048] = ""; char temp[2048] = ""; strcpy(buffer, fmt);
        char* p;
        while ((p = strstr(buffer, "{file}")) != NULL) { *p = '\0'; snprintf(temp, sizeof(temp), "%s%s%s", buffer, actualFile, p + 6); strcpy(buffer, temp); }
        char line_str_num[32]; snprintf(line_str_num, sizeof(line_str_num), "%d", line_num);
        while ((p = strstr(buffer, "{line}")) != NULL) { *p = '\0'; snprintf(temp, sizeof(temp), "%s%s%s", buffer, line_str_num, p + 6); strcpy(buffer, temp); }
        while ((p = strstr(buffer, "{code}")) != NULL) { *p = '\0'; snprintf(temp, sizeof(temp), "%s%s%s", buffer, safe_line_str, p + 6); strcpy(buffer, temp); }
        while ((p = strstr(buffer, "{pointer}")) != NULL) { *p = '\0'; snprintf(temp, sizeof(temp), "%s%s%s", buffer, pointer, p + 9); strcpy(buffer, temp); }
        if (opName) {
            while ((p = strstr(buffer, "{op}")) != NULL) { *p = '\0'; snprintf(temp, sizeof(temp), "%s%s%s", buffer, opName, p + 4); strcpy(buffer, temp); }
        }
        printf("%s%s%s\n", color_code, buffer, reset_code);
    }
    printf("\n");
}

void write_escaped(FILE* f, const char* str) {
    if (!str) { fprintf(f, "NULL"); return; } fprintf(f, "\"");
    while (*str) {
        if (*str == '"') fprintf(f, "\\\""); else if (*str == '\\') fprintf(f, "\\\\"); else if (*str == '\n') fprintf(f, "\\n");
        else if (*str == '\r') fprintf(f, "\\r"); else if (*str == '\t') fprintf(f, "\\t"); else fprintf(f, "%c", *str); str++;
    } fprintf(f, "\"");
}

typedef struct {
    char** tokens;
    int tok_count;
    int line;
    char* raw;
} Statement;

int tokenize(const char* src_raw, int length, Statement* stmts) {
    int stmt_count = 0; char curTok[4096] = ""; int cur_idx = 0; char* curStmt[256]; int cur_stmt_count = 0; char inStr = 0; int lineNum = 1, stmtLine = 1;
    for (int i = 0; i < length; i++) {
        char c = src_raw[i]; if (c == '\n') lineNum++;
        if (is_str_delim(c) && (i == 0 || src_raw[i-1] != '\\')) {
            if (inStr == c) { inStr = 0; curTok[cur_idx++] = c; curTok[cur_idx] = '\0'; curStmt[cur_stmt_count++] = strdup(curTok); cur_idx = 0; curTok[0] = '\0'; continue; }
            else if (!inStr) { if (cur_idx > 0 && !is_delim_str(curTok)) { curStmt[cur_stmt_count++] = strdup(curTok); cur_idx = 0; curTok[0] = '\0'; } inStr = c; curTok[cur_idx++] = c; curTok[cur_idx] = '\0'; continue; }
        }
        if (inStr) { curTok[cur_idx++] = c; curTok[cur_idx] = '\0'; continue; }
        if (is_stmt_sep(c)) {
            if (cur_idx > 0) { curStmt[cur_stmt_count++] = strdup(curTok); cur_idx = 0; curTok[0] = '\0'; }
            if (cur_stmt_count > 0) { stmts[stmt_count].tokens = malloc(cur_stmt_count * sizeof(char*)); char raw_buf[4096] = "";
                for(int k=0; k<cur_stmt_count; k++) { stmts[stmt_count].tokens[k] = curStmt[k]; strcat(raw_buf, curStmt[k]); if(k<cur_stmt_count-1) strcat(raw_buf, " "); }
                stmts[stmt_count].raw = strdup(raw_buf); stmts[stmt_count].tok_count = cur_stmt_count; stmts[stmt_count].line = stmtLine; stmt_count++; cur_stmt_count = 0;
            } stmtLine = lineNum; continue;
        }
        if (is_ws(c)) { if (cur_idx > 0) { curStmt[cur_stmt_count++] = strdup(curTok); cur_idx = 0; curTok[0] = '\0'; } continue; }
        if (is_delim(c)) { if (cur_idx > 0) { curStmt[cur_stmt_count++] = strdup(curTok); cur_idx = 0; curTok[0] = '\0'; } char d[2] = {c, '\0'}; curStmt[cur_stmt_count++] = strdup(d); continue; }
        curTok[cur_idx++] = c; curTok[cur_idx] = '\0';
    }
    if (cur_idx > 0) curStmt[cur_stmt_count++] = strdup(curTok);
    if (cur_stmt_count > 0) { stmts[stmt_count].tokens = malloc(cur_stmt_count * sizeof(char*)); char raw_buf[4096] = "";
        for(int k=0; k<cur_stmt_count; k++) { stmts[stmt_count].tokens[k] = curStmt[k]; strcat(raw_buf, curStmt[k]); if(k<cur_stmt_count-1) strcat(raw_buf, " "); }
        stmts[stmt_count].raw = strdup(raw_buf); stmts[stmt_count].tok_count = cur_stmt_count; stmts[stmt_count].line = stmtLine; stmt_count++;
    } return stmt_count;
}

const char* BOILERPLATE_TOP = 
"#include <stdio.h>\n#include <stdlib.h>\n#include <string.h>\n#include <stdbool.h>\n#include <stdint.h>\n#include <unistd.h>\n#include <cjson/cJSON.h>\n"
"cJSON *rules, *patterns, *ops, *delimiters, *errors, *colors, *string_delimiters, *whitespace, *stmt_separators;\n"
"typedef struct { char key[256]; void* val; } MemEntry; MemEntry mem[2048]; int mem_count = 0;\n"
"void* get_mem(const char* k) { for(int i=0;i<mem_count;i++) if(strcmp(mem[i].key,k)==0) return mem[i].val; return NULL; }\n"
"bool has_mem(const char* k) { for(int i=0;i<mem_count;i++) if(strcmp(mem[i].key,k)==0) return true; return false; }\n"
"void set_mem(const char* k, void* v) { for(int i=0;i<mem_count;i++) if(strcmp(mem[i].key,k)==0) { mem[i].val = v; return; } strcpy(mem[mem_count].key, k); mem[mem_count++].val = v; }\n"
"void* coerce(void* v) { if(!v) return NULL; if((uintptr_t)v < 0x10000) return v; char* endptr; long n = strtol((char*)v, &endptr, 10); if(*endptr=='\\0' && ((char*)v)[0]!='\\0') return (void*)(intptr_t)n; return v; }\n"
"typedef struct { char t; bool cond; bool skipBefore; int L; } StackItem;\n"
"typedef struct { int L; bool skip; StackItem stack[1024]; int stack_length; } EnvState; EnvState _ENV;\n"
"typedef struct { const char* name; void* (*func)(void**, EnvState*); } OpMapEntry; extern OpMapEntry op_map[]; extern int op_map_len;\n"
"bool is_in_array(char c, cJSON* arr) { if(!arr) return false; char s[2]={c,'\\0'}; cJSON* i; cJSON_ArrayForEach(i,arr) if(i->valuestring && strcmp(i->valuestring,s)==0) return true; return false; }\n"
"bool is_str_delim(char c) { return is_in_array(c, string_delimiters); }\n"
"bool is_delim(char c) { return is_in_array(c, delimiters); }\n"
"bool is_ws(char c) { return is_in_array(c, whitespace); }\n"
"bool is_stmt_sep(char c) { return is_in_array(c, stmt_separators); }\n"
"bool is_delim_str(const char* s) { if(!delimiters||!s) return false; cJSON* i; cJSON_ArrayForEach(i,delimiters) if(i->valuestring && strcmp(i->valuestring,s)==0) return true; return false; }\n"
"char* unescape_string(const char* str) { int slen = strlen(str); char* unq = calloc(1, slen); int i = 0; for(int u=1; u<slen-1; u++) { if(str[u]=='\\\\' && (str[u+1]=='\"' || str[u+1]=='\\'' || str[u+1]=='`')) unq[i++] = str[++u]; else unq[i++] = str[u]; } return unq; }\n"
"typedef struct { char** tokens; int tok_count; int line; char* raw; } Statement;\n"
"int tokenize(const char* src_raw, int length, Statement* stmts) {\n"
"    int stmt_count = 0; char curTok[4096] = \"\"; int cur_idx = 0; char* curStmt[256]; int cur_stmt_count = 0; char inStr = 0; int lineNum = 1, stmtLine = 1;\n"
"    for (int i = 0; i < length; i++) { char c = src_raw[i]; if (c == '\\n') lineNum++;\n"
"        if (is_str_delim(c) && (i == 0 || src_raw[i-1] != '\\\\')) {\n"
"            if (inStr == c) { inStr = 0; curTok[cur_idx++] = c; curTok[cur_idx] = '\\0'; curStmt[cur_stmt_count++] = strdup(curTok); cur_idx = 0; curTok[0] = '\\0'; continue; }\n"
"            else if (!inStr) { if (cur_idx > 0 && !is_delim_str(curTok)) { curStmt[cur_stmt_count++] = strdup(curTok); cur_idx = 0; curTok[0] = '\\0'; } inStr = c; curTok[cur_idx++] = c; curTok[cur_idx] = '\\0'; continue; }\n"
"        } if (inStr) { curTok[cur_idx++] = c; curTok[cur_idx] = '\\0'; continue; }\n"
"        if (is_stmt_sep(c)) { if (cur_idx > 0) { curStmt[cur_stmt_count++] = strdup(curTok); cur_idx = 0; curTok[0] = '\\0'; }\n"
"            if (cur_stmt_count > 0) { stmts[stmt_count].tokens = malloc(cur_stmt_count * sizeof(char*)); char raw_buf[4096] = \"\";\n"
"                for(int k=0; k<cur_stmt_count; k++) { stmts[stmt_count].tokens[k] = curStmt[k]; strcat(raw_buf, curStmt[k]); if(k<cur_stmt_count-1) strcat(raw_buf, \" \"); }\n"
"                stmts[stmt_count].raw = strdup(raw_buf); stmts[stmt_count].tok_count = cur_stmt_count; stmts[stmt_count].line = stmtLine; stmt_count++; cur_stmt_count = 0;\n"
"            } stmtLine = lineNum; continue; }\n"
"        if (is_ws(c)) { if (cur_idx > 0) { curStmt[cur_stmt_count++] = strdup(curTok); cur_idx = 0; curTok[0] = '\\0'; } continue; }\n"
"        if (is_delim(c)) { if (cur_idx > 0) { curStmt[cur_stmt_count++] = strdup(curTok); cur_idx = 0; curTok[0] = '\\0'; } char d[2] = {c, '\\0'}; curStmt[cur_stmt_count++] = strdup(d); continue; }\n"
"        curTok[cur_idx++] = c; curTok[cur_idx] = '\\0';\n"
"    } if (cur_idx > 0) curStmt[cur_stmt_count++] = strdup(curTok);\n"
"    if (cur_stmt_count > 0) { stmts[stmt_count].tokens = malloc(cur_stmt_count * sizeof(char*)); char raw_buf[4096] = \"\";\n"
"        for(int k=0; k<cur_stmt_count; k++) { stmts[stmt_count].tokens[k] = curStmt[k]; strcat(raw_buf, curStmt[k]); if(k<cur_stmt_count-1) strcat(raw_buf, \" \"); }\n"
"        stmts[stmt_count].raw = strdup(raw_buf); stmts[stmt_count].tok_count = cur_stmt_count; stmts[stmt_count].line = stmtLine; stmt_count++;\n"
"    } return stmt_count;\n"
"}\n\n";

const char* BOILERPLATE_THROW =
"void throwError(const char* errKey, const char* actualFile, int line_num, const char* line_str, const char* opName) {\n"
"    if (!errors) { printf(\"Error: %s at line %d\\n\", errKey, line_num); return; }\n"
"    cJSON* errorLines = cJSON_GetObjectItemCaseSensitive(errors, errKey); if (!errorLines) return;\n"
"    char safe_line_str[4096] = \"\"; int safe_idx = 0;\n"
"    for (int k = 0; line_str[k] && safe_idx < 4000; k++) { if (line_str[k] == '\\n') { safe_line_str[safe_idx++] = '\\\\'; safe_line_str[safe_idx++] = 'n'; } else { safe_line_str[safe_idx++] = line_str[k]; } }\n"
"    safe_line_str[safe_idx] = '\\0'; int len = strlen(safe_line_str); char pointer[1024]; if (len >= sizeof(pointer)) len = sizeof(pointer) - 1;\n"
"    for (int i = 0; i < len; i++) pointer[i] = '^'; pointer[len] = '\\0'; cJSON* item;\n"
"    cJSON_ArrayForEach(item, errorLines) {\n"
"        cJSON* t_node = cJSON_GetObjectItemCaseSensitive(item, \"text\"); cJSON* c_node = cJSON_GetObjectItemCaseSensitive(item, \"color\"); if (!t_node || !t_node->valuestring) continue;\n"
"        char c_code[32] = \"\"; if (c_node && c_node->valuestring && colors) { cJSON* col = cJSON_GetObjectItemCaseSensitive(colors, c_node->valuestring); if (col && col->valuestring) strcpy(c_code, col->valuestring); }\n"
"        char r_code[32] = \"\"; if (colors) { cJSON* res = cJSON_GetObjectItemCaseSensitive(colors, \"reset\"); if (res && res->valuestring) strcpy(r_code, res->valuestring); }\n"
"        char buf[2048] = \"\"; char tmp[2048] = \"\"; strcpy(buf, t_node->valuestring); char* p;\n"
"        while ((p = strstr(buf, \"{file}\")) != NULL) { *p = '\\0'; snprintf(tmp, sizeof(tmp), \"%s%s%s\", buf, actualFile, p + 6); strcpy(buf, tmp); }\n"
"        char line_str_num[32]; snprintf(line_str_num, sizeof(line_str_num), \"%d\", line_num);\n"
"        while ((p = strstr(buf, \"{line}\")) != NULL) { *p = '\\0'; snprintf(tmp, sizeof(tmp), \"%s%s%s\", buf, line_str_num, p + 6); strcpy(buf, tmp); }\n"
"        while ((p = strstr(buf, \"{code}\")) != NULL) { *p = '\\0'; snprintf(tmp, sizeof(tmp), \"%s%s%s\", buf, safe_line_str, p + 6); strcpy(buf, tmp); }\n"
"        while ((p = strstr(buf, \"{pointer}\")) != NULL) { *p = '\\0'; snprintf(tmp, sizeof(tmp), \"%s%s%s\", buf, pointer, p + 9); strcpy(buf, tmp); }\n"
"        if (opName) while ((p = strstr(buf, \"{op}\")) != NULL) { *p = '\\0'; snprintf(tmp, sizeof(tmp), \"%s%s%s\", buf, opName, p + 4); strcpy(buf, tmp); }\n"
"        printf(\"%s%s%s\\n\", c_code, buf, r_code);\n"
"    } printf(\"\\n\");\n"
"}\n\n";

const char* BOILERPLATE_RUN =
"void run(const char* filename) {\n"
"    char act[512]; int idx = 0; for (int i=0; filename[i]; i++) if (!is_str_delim(filename[i])) act[idx++] = filename[i]; act[idx] = '\\0';\n"
"    FILE* f = fopen(act, \"rb\"); if (!f) return; fseek(f, 0, SEEK_END); long len = ftell(f); fseek(f, 0, SEEK_SET);\n"
"    char* src = malloc(len + 1); fread(src, 1, len, f); src[len] = '\\0'; fclose(f);\n"
"    Statement stmts[4096]; int count = tokenize(src, len, stmts);\n"
"    int err_c = 0, max_errs = 10;\n"
"    EnvState prev = _ENV; _ENV.L = 0; _ENV.skip = false; _ENV.stack_length = 0;\n"
"    while (_ENV.L < count) {\n"
"        Statement* s = &stmts[_ENV.L]; if (s->tok_count == 0) { _ENV.L++; continue; }\n"
"        bool m = false; cJSON* p_rule;\n"
"        cJSON_ArrayForEach(p_rule, patterns) {\n"
"            cJSON* pat = cJSON_GetArrayItem(p_rule, 0); char* opN = strdup(cJSON_GetArrayItem(p_rule, 1)->valuestring);\n"
"            cJSON* tn = cJSON_GetArrayItem(p_rule, 3); char* targ = (tn && !cJSON_IsNull(tn)) ? tn->valuestring : NULL;\n"
"            cJSON* ic = cJSON_GetArrayItem(p_rule, 4); bool isC = (ic && cJSON_IsTrue(ic));\n"
"            int p_len = cJSON_GetArraySize(pat); cJSON* lp = cJSON_GetArrayItem(pat, p_len - 1);\n"
"            bool hasW = (lp && strcmp(lp->valuestring, \"{...}\") == 0);\n"
"            if (!hasW && p_len != s->tok_count) { free(opN); continue; } if (hasW && s->tok_count < p_len - 1) { free(opN); continue; }\n"
"            struct { char k[64]; char v[256]; } ctx[64]; int c_c = 0; bool ok = true;\n"
"            for (int k = 0; k < p_len; k++) { char* ps = cJSON_GetArrayItem(pat, k)->valuestring; if (strcmp(ps, \"{...}\") == 0) break;\n"
"                if (ps[0] == '{') { strcpy(ctx[c_c].k, ps); strcpy(ctx[c_c].v, s->tokens[k]); c_c++; }\n"
"                else if (strcmp(ps, s->tokens[k]) != 0) { ok = false; break; } }\n"
"            if (!ok) { free(opN); continue; } m = true; if (_ENV.skip && !isC) { free(opN); break; }\n"
"            if (opN[0] == '{') { for (int c = 0; c < c_c; c++) if (strcmp(ctx[c].k, opN) == 0) { free(opN); opN = strdup(ctx[c].v); break; } }\n"
"            cJSON* op_e = cJSON_GetObjectItemCaseSensitive(ops, opN);\n"
"            if (!op_e) { throwError(\"ERR_UNDEF\", act, s->line, s->raw, opN); free(opN); err_c++; break; }\n"
"            cJSON* var = cJSON_GetArrayItem(op_e, 0); void* fArgs[16] = {0}; int fA_c = 0;\n"
"            for (int si = 1; si < cJSON_GetArraySize(var); si++) {\n"
"                char* arg = cJSON_GetArrayItem(var, si)->valuestring; if (strcmp(arg, \"null\") == 0) { fArgs[fA_c++] = NULL; continue; }\n"
"                char* v = arg; if (arg[0] == '{') for (int c = 0; c < c_c; c++) if (strcmp(ctx[c].k, arg) == 0) { v = ctx[c].v; break; }\n"
"                fArgs[fA_c++] = coerce(has_mem(v) ? get_mem(v) : (void*)v); }\n"
"            void* pArgs[16] = {0};\n"
"            for (int a = 0; a < fA_c; a++) { char* as = (char*)fArgs[a];\n"
"                if (as && (uintptr_t)as > 0x10000 && is_str_delim(as[0])) pArgs[a] = unescape_string(as); else pArgs[a] = fArgs[a]; }\n"
"            void* (*f_ptr)(void**, EnvState*) = NULL;\n"
"            for (int m = 0; m < op_map_len; m++) if (strcmp(op_map[m].name, opN) == 0) { f_ptr = op_map[m].func; break; }\n"
"            void* res = f_ptr ? f_ptr(pArgs, &_ENV) : NULL;\n"
"            if (targ) { char* k = targ; if (targ[0] == '{') for (int c = 0; c < c_c; c++) if (strcmp(ctx[c].k, targ) == 0) { k = ctx[c].v; break; } set_mem(k, res); }\n"
"            free(opN); break;\n"
"        }\n"
"        if (!m && s->tok_count > 0 && !_ENV.skip) { throwError(\"ERR_SYNTAX\", act, s->line, s->raw, NULL); err_c++; }\n"
"        if (err_c >= max_errs) break;\n"
"        _ENV.L++;\n"
"    } _ENV = prev; free(src);\n"
"    if (err_c > 0) { printf(\"error: aborting due to %d previous error%s\\n\\n\", err_c, err_c != 1 ? \"s\" : \"\"); if (max_errs == 10) printf(\"Hint: use --max-errors=50 to increase the limit.\\n\"); }\n"
"}\n\n";

const char* BOILERPLATE_MAIN_TOP =
"typedef struct { void* (*func)(void**, EnvState*); const char* args[16]; const char* target; bool isControl; } Instruction;\n"
"int main(int argc, char** argv) {\n"
"    FILE* f = fopen(\"core.json\", \"rb\");\n"
"    if (f) {\n"
"        fseek(f, 0, SEEK_END); long length = ftell(f); fseek(f, 0, SEEK_SET);\n"
"        char* json_data = malloc(length + 1); fread(json_data, 1, length, f); json_data[length] = '\\0'; fclose(f);\n"
"        rules = cJSON_Parse(json_data);\n"
"        patterns = cJSON_GetArrayItem(rules, 0); ops = cJSON_GetArrayItem(rules, 1); delimiters = cJSON_GetArrayItem(rules, 2);\n"
"        errors = cJSON_GetArrayItem(rules, 3); colors = cJSON_GetArrayItem(rules, 4); string_delimiters = cJSON_GetArrayItem(rules, 5);\n"
"        whitespace = cJSON_GetArrayItem(rules, 6); stmt_separators = cJSON_GetArrayItem(rules, 7);\n"
"    }\n"
"    Instruction prog[] = {\n";

const char* BOILERPLATE_MAIN_BOTTOM =
"    };\n"
"    int prog_len = sizeof(prog) / sizeof(Instruction);\n"
"    _ENV.L = 0; _ENV.skip = false; _ENV.stack_length = 0;\n"
"    while (_ENV.L < prog_len) {\n"
"        if (_ENV.L < 0) break;\n"
"        Instruction* inst = &prog[_ENV.L]; if (_ENV.skip && !inst->isControl) { _ENV.L++; continue; }\n"
"        void* resolved_args[16] = {0};\n"
"        for (int i = 0; i < 16 && inst->args[i]; i++) {\n"
"            const char* as = inst->args[i];\n"
"            if (is_str_delim(as[0])) resolved_args[i] = unescape_string(as);\n"
"            else if (strcmp(as, \"null\") == 0) resolved_args[i] = NULL;\n"
"            else resolved_args[i] = coerce(has_mem(as) ? get_mem(as) : (void*)as);\n"
"        }\n"
"        void* res = inst->func ? inst->func(resolved_args, &_ENV) : NULL;\n"
"        if (inst->target) set_mem(inst->target, res);\n"
"        _ENV.L++;\n"
"    }\n"
"    if (rules) cJSON_Delete(rules);\n"
"    return 0;\n}\n";

int main(int argc, char** argv) {
    int max_errors = 10;
    char* target_file = NULL;

    for (int i = 1; i < argc; i++) {
        if (strncmp(argv[i], "--max-errors=", 13) == 0) {
            max_errors = atoi(argv[i] + 13);
        } else if (!target_file) {
            target_file = argv[i];
        }
    }

    if (!target_file) { printf("Usage: %s [--max-errors=N] file.jpp\n", argv[0]); return 1; }
    
    FILE* fc = fopen("core.json", "rb"); 
    if (!fc) { printf("core.json not found!\n"); return 1; }
    fseek(fc, 0, SEEK_END); long length = ftell(fc); fseek(fc, 0, SEEK_SET);
    char* json_data = malloc(length + 1); fread(json_data, 1, length, fc); json_data[length] = '\0'; fclose(fc);
    
    rules = cJSON_Parse(json_data); 
    if (!rules) return 1;
    patterns = cJSON_GetArrayItem(rules, 0); 
    ops = cJSON_GetArrayItem(rules, 1); 
    delimiters = cJSON_GetArrayItem(rules, 2);
    errors = cJSON_GetArrayItem(rules, 3); 
    colors = cJSON_GetArrayItem(rules, 4); 
    string_delimiters = cJSON_GetArrayItem(rules, 5);
    whitespace = cJSON_GetArrayItem(rules, 6); 
    stmt_separators = cJSON_GetArrayItem(rules, 7);

    char baseName[256]; strcpy(baseName, target_file);
    char* dot = strrchr(baseName, '.'); if (dot && strcmp(dot, ".jpp") == 0) *dot = '\0'; else strcat(baseName, "_out");
    char outC[300]; snprintf(outC, sizeof(outC), "%s.c", baseName);

    FILE* fj = fopen(target_file, "rb"); 
    if (!fj) { printf("File %s not found!\n", target_file); return 1; }
    fseek(fj, 0, SEEK_END); long j_len = ftell(fj); fseek(fj, 0, SEEK_SET);
    char* src_raw = malloc(j_len + 1); fread(src_raw, 1, j_len, fj); src_raw[j_len] = '\0'; fclose(fj);

    FILE* out = fopen(outC, "w");
    fprintf(out, "%s%s%s", BOILERPLATE_TOP, BOILERPLATE_THROW, BOILERPLATE_RUN);

    cJSON* op;
    cJSON_ArrayForEach(op, ops) {
        char* opName = op->string; cJSON* variant = cJSON_GetArrayItem(op, 0); cJSON* c_code_node = cJSON_GetArrayItem(variant, 0);
        if (!c_code_node || !c_code_node->valuestring) continue;
        char safeName[256]; int j = 0; for (int i = 0; opName[i]; i++) safeName[j++] = isalnum(opName[i]) ? opName[i] : '_'; safeName[j] = '\0';
        fprintf(out, "static void* op_%s(void** args, EnvState* env) {\n    %s\n}\n\n", safeName, c_code_node->valuestring);
    }
    
    fprintf(out, "OpMapEntry op_map[] = {\n");
    cJSON_ArrayForEach(op, ops) {
        char* opName = op->string; char safeName[256]; int j = 0;
        for (int i = 0; opName[i]; i++) safeName[j++] = isalnum(opName[i]) ? opName[i] : '_'; safeName[j] = '\0';
        fprintf(out, "    {\"%s\", op_%s},\n", opName, safeName);
    } 
    fprintf(out, "};\nint op_map_len = sizeof(op_map) / sizeof(OpMapEntry);\n\n%s", BOILERPLATE_MAIN_TOP);

    Statement statements[4096];
    int stmt_count = tokenize(src_raw, j_len, statements);
    int error_count = 0;
    
    for (int L = 0; L < stmt_count; L++) {
        Statement* stmt = &statements[L]; if (stmt->tok_count == 0) continue;
        bool matched = false; cJSON* pat_rule;
        
        cJSON_ArrayForEach(pat_rule, patterns) {
            cJSON* pat = cJSON_GetArrayItem(pat_rule, 0); char* opName = strdup(cJSON_GetArrayItem(pat_rule, 1)->valuestring);
            cJSON* tn = cJSON_GetArrayItem(pat_rule, 3); char* target = (tn && !cJSON_IsNull(tn)) ? tn->valuestring : NULL;
            cJSON* isC_node = cJSON_GetArrayItem(pat_rule, 4); bool isControl = (isC_node && cJSON_IsTrue(isC_node));
            
            int pat_len = cJSON_GetArraySize(pat); cJSON* last_pat = cJSON_GetArrayItem(pat, pat_len - 1);
            bool hasWildcard = (last_pat && strcmp(last_pat->valuestring, "{...}") == 0);
            
            if (!hasWildcard && pat_len != stmt->tok_count) { free(opName); continue; }
            if (hasWildcard && stmt->tok_count < pat_len - 1) { free(opName); continue; }
            
            struct { char k[64]; char v[256]; } ctx[64]; int ctx_count = 0; bool ok = true;
            for (int k = 0; k < pat_len; k++) {
                char* p_str = cJSON_GetArrayItem(pat, k)->valuestring; if (strcmp(p_str, "{...}") == 0) break;
                if (p_str[0] == '{') { strcpy(ctx[ctx_count].k, p_str); strcpy(ctx[ctx_count].v, stmt->tokens[k]); ctx_count++; }
                else if (strcmp(p_str, stmt->tokens[k]) != 0) { ok = false; break; }
            }
            
            if (!ok) { free(opName); continue; } matched = true;
            
            if (opName[0] == '{') {
                for (int c = 0; c < ctx_count; c++) {
                    if (strcmp(ctx[c].k, opName) == 0) { free(opName); opName = strdup(ctx[c].v); break; }
                }
            }
            
            cJSON* op_entry = cJSON_GetObjectItemCaseSensitive(ops, opName);
            if (!op_entry) { 
                throwError("ERR_UNDEF", target_file, stmt->line, stmt->raw, opName); 
                free(opName); 
                error_count++; 
                break; 
            }
            
            char safeName[256]; int sj = 0; 
            for (int si = 0; opName[si]; si++) safeName[sj++] = isalnum(opName[si]) ? opName[si] : '_'; safeName[sj] = '\0';
            
            cJSON* variant = cJSON_GetArrayItem(op_entry, 0); 
            fprintf(out, "        { op_%s, { ", safeName);
            for (int s_idx = 1; s_idx < cJSON_GetArraySize(variant); s_idx++) {
                char* s = cJSON_GetArrayItem(variant, s_idx)->valuestring; char* v = s;
                if (s[0] == '{') for (int c = 0; c < ctx_count; c++) if (strcmp(ctx[c].k, s) == 0) { v = ctx[c].v; break; }
                write_escaped(out, v); fprintf(out, ", ");
            }
            fprintf(out, "NULL }, ");
            if (target) { 
                char* t_v = target; 
                if (target[0] == '{') for (int c = 0; c < ctx_count; c++) if (strcmp(ctx[c].k, target) == 0) { t_v = ctx[c].v; break; } 
                write_escaped(out, t_v); 
            } else { fprintf(out, "NULL"); }
            fprintf(out, ", %s },\n", isControl ? "true" : "false");
            
            free(opName); break;
        }

        if (!matched && stmt->tok_count > 0) { 
            throwError("ERR_SYNTAX", target_file, stmt->line, stmt->raw, NULL); 
            error_count++; 
        }

        if (error_count >= max_errors) break;
    }

    if (error_count > 0) {
        printf("error: aborting due to %d previous error%s\n\n", error_count, error_count != 1 ? "s" : "");
        if (max_errors == 10) printf("Hint: use --max-errors=50 to increase the limit.\n");
        fclose(out);
        free(src_raw); cJSON_Delete(rules); free(json_data);
        remove(outC); 
        return 1;
    }

    fprintf(out, "%s", BOILERPLATE_MAIN_BOTTOM); fclose(out);
    free(src_raw); cJSON_Delete(rules); free(json_data);

    char cmd[1024]; snprintf(cmd, sizeof(cmd), "gcc -O2 \"%s\" -lcjson -o \"%s\"", outC, baseName);
    printf("[Compiler] Generating executable '%s'...\n", baseName);
    if (system(cmd) == 0) { printf("[Compiler] Success! Run using: ./%s\n", baseName); remove(outC); }
    else { printf("[Compiler] Failed to compile the final executable.\n"); }
    return 0;
}
