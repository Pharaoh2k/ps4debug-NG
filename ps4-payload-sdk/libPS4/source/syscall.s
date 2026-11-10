.intel_syntax noprefix

# __error is now a REAL function (kernel.c) — it forwards to libkernel's __error
# once initKernel() has resolved it, and returns a private fallback slot before
# then.  So it is always safe to call directly here.  (The old libPS4 exposed
# __error as a function-POINTER variable, hence the original `call __error[rip]`;
# that indirection is wrong now and would jump through the first 8 bytes of the
# function's machine code -> garbage control flow.)
.extern __error

.text

.globl syscall
syscall:
	xor rax, rax

.globl syscall_macro
syscall_macro:
	mov r10, rcx
	syscall
	jb _error
	ret

_error:
	push rax
	call __error
	pop rcx
	mov [rax], ecx
	mov rax, -1
	mov rdx, -1
	ret
