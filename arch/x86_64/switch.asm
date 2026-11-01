extern kernel_rsp_saved

section .text
bits 64

global switch_to
switch_to:
    ; rdi = prev, rsi = next
    push rbp
    push rbx
    push r12
    push r13
    push r14
    push r15

    ; Сохраняем текущий RSP в prev->kernel_rsp (смещение 16)
    mov [rdi + 24], rsp
    mov rsp, [rsi + 24]

    ; Восстанавливаем регистры нового процесса
    pop r15
    pop r14
    pop r13
    pop r12
    pop rbx
    pop rbp
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
