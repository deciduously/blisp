# blisp

Repo for following along with the [Build Your Own Lisp](http://www.buildyourownlisp.com/) book, and subsequent tinkering.

Includes `mpc.c` and `mpc.h` from the [`mpc`](https://github.com/orangeduck/mpc) repo.

# Requirements

* A C compiler

# Usage

`cc --std=c99 -Wall parsing.c mpc.c -lreadline -lm -o parsing && ./parsing`