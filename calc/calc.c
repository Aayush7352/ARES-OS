/*==========================================================================*/
/* ARES OS - Scientific Calculator                                          */
/*                                                                          */
/* Interactive REPL that parses infix expressions with PEMDAS, supports     */
/* basic transcendental functions (sin/cos/tan/sqrt/log/ln/abs/floor/ceil)  */
/* via Taylor series and Newton's method, and exposes `ans` for last result.*/
/*==========================================================================*/

#include <stdint.h>
#include <stddef.h>

#include "calc.h"
#include "../kernel/console.h"
#include "../drivers/keyboard.h"

/*--------------------------------------------------------------------------*/
/* Local math helpers (no libm in kernel).                                  */
/*--------------------------------------------------------------------------*/

static double calc_fabs(double x) {
    return x < 0.0 ? -x : x;
}

/* ln(x) via Newton's method on exp(y) - x = 0 — forward declared for log() */
static double calc_ln(double x);

/* Sin via Taylor series: sin(x) = x - x^3/3! + x^5/5! - ... */
static double calc_sin(double x) {
    double sum = 0.0, term = x;
    for (int n = 1; n < 15; n += 2) {
        sum += term;
        term = -term * x * x / ((double)(n + 1) * (double)(n + 2));
    }
    return sum;
}

static double calc_cos(double x) {
    return calc_sin(3.141592653589793 / 2.0 - x);
}

static double calc_tan(double x) {
    return calc_sin(x) / calc_cos(x);
}

static double calc_sqrt(double x) {
    if (x < 0.0) return 0.0 / 0.0; /* NaN */
    if (x < 1e-15) return 0.0;
    double r = x;
    for (int i = 0; i < 20; i++) {
        r = (r + x / r) / 2.0;
    }
    return r;
}

/*--------------------------------------------------------------------------*/
/* Tokenizer                                                                */
/*--------------------------------------------------------------------------*/

enum token_type {
    TOK_NUM, TOK_PLUS, TOK_MINUS, TOK_MUL, TOK_DIV,
    TOK_MOD, TOK_POW, TOK_LPAREN, TOK_RPAREN,
    TOK_SIN, TOK_COS, TOK_TAN, TOK_SQRT, TOK_LOG, TOK_LN,
    TOK_ABS, TOK_FLOOR, TOK_CEIL, TOK_ANS,
    TOK_END, TOK_ERROR
};

static enum token_type tok_type;
static double tok_value;

static double g_ans = 0.0;
static const char *g_src;
static size_t g_pos;

static int is_func_char(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}

static int calc_strcmp(const char *a, const char *b) {
    while (*a && *a == *b) { a++; b++; }
    return (int)(uint8_t)*a - (int)(uint8_t)*b;
}

static void next_token(void) {
    while (g_src[g_pos] == ' ') g_pos++;
    if (g_src[g_pos] == '\0') { tok_type = TOK_END; return; }

    char c = g_src[g_pos];
    if ((c >= '0' && c <= '9') || c == '.') {
        /* Parse number */
        double n = 0.0;
        while (g_src[g_pos] >= '0' && g_src[g_pos] <= '9') {
            n = n * 10.0 + (double)(g_src[g_pos] - '0');
            g_pos++;
        }
        if (g_src[g_pos] == '.') {
            g_pos++;
            double frac = 1.0;
            while (g_src[g_pos] >= '0' && g_src[g_pos] <= '9') {
                frac /= 10.0;
                n += frac * (double)(g_src[g_pos] - '0');
                g_pos++;
            }
        }
        tok_type = TOK_NUM;
        tok_value = n;
        return;
    }

    /* Functions and variable names */
    if (is_func_char(c)) {
        char name[16];
        size_t i = 0;
        while (is_func_char(g_src[g_pos]) && i < 15) {
            name[i++] = g_src[g_pos++];
        }
        name[i] = '\0';

        if (calc_strcmp(name, "ans") == 0)  { tok_type = TOK_ANS; return; }
        if (calc_strcmp(name, "sin") == 0)  { tok_type = TOK_SIN; return; }
        if (calc_strcmp(name, "cos") == 0)  { tok_type = TOK_COS; return; }
        if (calc_strcmp(name, "tan") == 0)  { tok_type = TOK_TAN; return; }
        if (calc_strcmp(name, "sqrt") == 0) { tok_type = TOK_SQRT; return; }
        if (calc_strcmp(name, "log") == 0)  { tok_type = TOK_LOG; return; }
        if (calc_strcmp(name, "ln") == 0)   { tok_type = TOK_LN; return; }
        if (calc_strcmp(name, "abs") == 0)  { tok_type = TOK_ABS; return; }
        if (calc_strcmp(name, "floor") == 0) { tok_type = TOK_FLOOR; return; }
        if (calc_strcmp(name, "ceil") == 0) { tok_type = TOK_CEIL; return; }
        if (calc_strcmp(name, "pi") == 0)   { tok_type = TOK_NUM; tok_value = 3.141592653589793; return; }
        if (calc_strcmp(name, "e") == 0)    { tok_type = TOK_NUM; tok_value = 2.718281828459045; return; }
        tok_type = TOK_ERROR;
        return;
    }

    g_pos++;
    switch (c) {
        case '+': tok_type = TOK_PLUS; break;
        case '-': tok_type = TOK_MINUS; break;
        case '*': tok_type = TOK_MUL; break;
        case '/': tok_type = TOK_DIV; break;
        case '%': tok_type = TOK_MOD; break;
        case '^': tok_type = TOK_POW; break;
        case '(': tok_type = TOK_LPAREN; break;
        case ')': tok_type = TOK_RPAREN; break;
        default:  tok_type = TOK_ERROR; break;
    }
}

/*--------------------------------------------------------------------------*/
/* Recursive descent parser (precedence climbing).                          */
/*--------------------------------------------------------------------------*/

static double parse_expr(int min_prec);

static double parse_primary(void) {
    if (tok_type == TOK_NUM) {
        double v = tok_value;
        next_token();
        return v;
    }
    if (tok_type == TOK_ANS) {
        next_token();
        return g_ans;
    }
    if (tok_type == TOK_LPAREN) {
        next_token();
        double v = parse_expr(0);
        if (tok_type == TOK_RPAREN) next_token();
        return v;
    }

    /* Unary function calls: sin(expr), cos(expr), etc. */
    enum token_type func = tok_type;
    if (func >= TOK_SIN && func <= TOK_CEIL) {
        next_token();
        if (tok_type == TOK_LPAREN) next_token();
        double arg = parse_expr(0);
        if (tok_type == TOK_RPAREN) next_token();

        switch (func) {
            case TOK_SIN:  return calc_sin(arg);
            case TOK_COS:  return calc_cos(arg);
            case TOK_TAN:  return calc_tan(arg);
            case TOK_SQRT: return calc_sqrt(arg);
            case TOK_LOG:  return 2.302585092994046 * calc_ln(arg);
            case TOK_LN:   return calc_ln(arg);
            case TOK_ABS:  return calc_fabs(arg);
            case TOK_FLOOR: { double f = (double)((int64_t)arg); return (arg >= f) ? f : f - 1.0; }
            case TOK_CEIL: { double f = (double)((int64_t)arg); return (arg <= f) ? f : f + 1.0; }
            default: return 0.0;
        }
    }

    if (tok_type == TOK_MINUS) {
        next_token();
        return -parse_primary();
    }
    if (tok_type == TOK_PLUS) {
        next_token();
        return parse_primary();
    }

    return 0.0;
}

/* ln(x) via Newton's method on exp(y) - x = 0 */
static double calc_ln(double x) {
    if (x <= 0.0) return 0.0 / 0.0;
    double y = (x - 1.0) / (x + 1.0);
    double sum = 0.0;
    double term = y;
    for (int n = 1; n < 100; n += 2) {
        sum += term / (double)n;
        term *= y * y;
        if (calc_fabs(term / (double)n) < 1e-12) break;
    }
    return 2.0 * sum;
}

/* Pow for real exponents: x^y = exp(y * ln(|x|)) */
static double calc_pow_real(double x, double y) {
    if (x < 0 && y != (int64_t)y) return 0.0 / 0.0;
    if (x == 0.0) return 0.0;
    int neg = (x < 0);
    double mag = calc_ln(calc_fabs(x));
    double r = 0.0;
    /* exp via Taylor: e^x = sum(x^n/n!) */
    double exp_arg = y * mag;
    double term_e = 1.0;
    for (int n = 1; n < 20; n++) {
        r += term_e;
        term_e *= exp_arg / (double)n;
    }
    return neg ? -r : r;
}

static int get_prec(enum token_type t) {
    switch (t) {
        case TOK_PLUS:
        case TOK_MINUS: return 1;
        case TOK_MUL:
        case TOK_DIV:
        case TOK_MOD: return 2;
        case TOK_POW: return 3;
        default: return -1;
    }
}

static double apply_binop(enum token_type op, double a, double b) {
    switch (op) {
        case TOK_PLUS:  return a + b;
        case TOK_MINUS: return a - b;
        case TOK_MUL:   return a * b;
        case TOK_DIV:   return (b != 0.0) ? a / b : 0.0 / 0.0;
        case TOK_MOD:   return (double)((int64_t)a % (int64_t)b);
        case TOK_POW:   return calc_pow_real(a, b);
        default: return 0.0;
    }
}

static double parse_expr(int min_prec) {
    double lhs = parse_primary();
    int prec = get_prec(tok_type);

    while (prec >= min_prec) {
        enum token_type op = tok_type;
        next_token();
        double rhs = parse_expr(prec + 1);
        lhs = apply_binop(op, lhs, rhs);
        prec = get_prec(tok_type);
    }
    return lhs;
}

/*--------------------------------------------------------------------------*/
/* Format a double into a string (no %f in our printf).                     */
/*--------------------------------------------------------------------------*/

static void format_double(char *buf, size_t cap, double val) {
    if (cap < 2) return;
    if (val != val) {
        buf[0] = 'N'; buf[1] = 'a'; buf[2] = 'N'; buf[3] = '\0';
        return;
    }
    if (val < 0) { buf[0] = '-'; buf++; cap--; val = -val; }

    uint64_t int_part = (uint64_t)val;
    double frac_part = val - (double)int_part;
    size_t pos = 0;

    /* Print integer part (reverse digit extraction) */
    char digits[32];
    int nd = 0;
    if (int_part == 0) { digits[nd++] = '0'; }
    while (int_part > 0 && nd < 32) {
        digits[nd++] = (char)('0' + (int)(int_part % 10));
        int_part /= 10;
    }
    for (int i = nd - 1; i >= 0 && pos < cap - 1; i--) {
        buf[pos++] = digits[i];
    }
    if (pos < cap - 1 && frac_part > 1e-10) {
        buf[pos++] = '.';
        for (int i = 0; i < 6 && pos < cap - 2; i++) {
            frac_part *= 10.0;
            int d = (int)frac_part;
            buf[pos++] = (char)('0' + d);
            frac_part -= (double)d;
        }
    }
    buf[pos] = '\0';
}

/*--------------------------------------------------------------------------*/
/* REPL loop                                                                */
/*--------------------------------------------------------------------------*/

void calc_run(void) {
    char line[CALC_INPUT_MAX];
    size_t pos = 0;
    char result[64];

    console_clear();
    console_printf("ARES Scientific Calculator\n");
    console_printf("Type expressions or 'quit' to exit.\n\n");

    while (1) {
        console_printf("> ");
        pos = 0;

        while (1) {
            __asm__ volatile("hlt");
            while (keyboard_has_data()) {
                uint8_t c = keyboard_getchar();
                if (c == 0) continue;
                if (c == KEY_ENTER) { console_putchar('\n'); goto line_done; }
                if (c == KEY_BACKSPACE) {
                    if (pos > 0) {
                        pos--;
                        console_putchar('\b');
                        console_putchar(' ');
                        console_putchar('\b');
                    }
                    continue;
                }
                if (c >= 0x20 && c < 0x7F && pos < CALC_INPUT_MAX - 1) {
                    line[pos++] = (char)c;
                    console_putchar((char)c);
                }
            }
        }
    line_done:
        line[pos] = '\0';

        /* Trim whitespace at end */
        while (pos > 0 && line[pos - 1] == ' ') { line[--pos] = '\0'; }
        if (pos == 0) continue;

        /* Check for quit/exit */
        if (calc_strcmp(line, "quit") == 0 || calc_strcmp(line, "exit") == 0) break;

        /* Parse and evaluate */
        g_src = line;
        g_pos = 0;
        next_token();
        double val = parse_expr(0);

        if (tok_type == TOK_ERROR) {
            console_printf("Error\n");
        } else {
            g_ans = val;
            format_double(result, sizeof(result), val);
            console_printf(" = %s\n", result);
        }
    }

    console_clear();
}
