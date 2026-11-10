// SPDX-License-Identifier: GPL-3.0-only

#ifndef SYSCALL_H
#define SYSCALL_H

#define SYSCALL(name, number)        \
  __asm__(".intel_syntax noprefix"); \
  __asm__(".globl " #name "");       \
  __asm__("" #name ":");             \
  __asm__("mov rax, " #number "");  \
  __asm__("jmp syscall_macro");

unsigned long syscall(unsigned long n, ...);

#endif
