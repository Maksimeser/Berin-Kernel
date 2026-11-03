extern kernel_rsp_saved

section .text
bits 64

global switch_to
switch_to:
    push r15
    push r14
    push r13
    push r12
    push rbp
    push rbx
    push rax

    mov [rdi + 32], rsp
    mov rsp, [rsi + 32]

    pop rax
    pop rbx
    pop rbp
    pop r12
    pop r13
    pop r14
    pop r15
    ret

global task_ret_to_user
task_ret_to_user:
    ; На вершине стека лежит iretq-фрейм
    iretq

global exit_to_kernel
exit_to_kernel:
    mov rax, rdi
    mov rsp, [kernel_rsp_saved]
    ret

section .note.GNU-stack noalloc noexec nowrite progbits
