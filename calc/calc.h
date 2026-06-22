#ifndef ARES_CALC_H
#define ARES_CALC_H

#include <stddef.h>
#include <stdint.h>

/*------------------------------------------------------------------------*/
/* ARES OS - Scientific Calculator                                        */
/*                                                                        */
/* An interactive REPL that parses infix expressions with PEMDAS, supports*/
/* basic transcendental functions implemented in-house (Taylor series for */
/* sin/cos/tan, Newton's method for sqrt, ln/log via change-of-base) and  */
/* exposes a single `ans` variable holding the last result.               */
/*                                                                        */
/* Supported tokens:                                                      */
/*   numbers   integer or decimal (3, 3.14, .5, 5.)                       */
/*   operators + - * / % ^                                                */
/*   functions sin cos tan sqrt log ln abs floor ceil                     */
/*   parens    ( )                                                        */
/*   variable  ans                                                        */
/*                                                                        */
/* Type `quit` (or `exit`) at the prompt to return to the shell.          */
/*------------------------------------------------------------------------*/

#define CALC_INPUT_MAX      128U
#define CALC_TOKEN_MAX      64U
#define CALC_STACK_MAX      64U

/* Public entry point. Runs the interactive REPL and returns when the     */
/* user enters `quit`, `exit` or an empty line followed by EOF (Ctrl+X on */
/* this platform).                                                        */
void calc_run(void);

#endif /* ARES_CALC_H */
