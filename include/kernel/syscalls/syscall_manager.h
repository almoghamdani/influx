#pragma once
#include <kernel/interrupts/interrupt_regs.h>
#include <kernel/logger.h>
#include <kernel/syscalls/syscall.h>

#define SYSCALL_INTERRUPT 0x80

namespace influx {
namespace syscalls {
void syscall_interrupt_handler(interrupts::regs *context);

class syscall_manager {
   public:
    syscall_manager();

   private:
    logger _log;

    int64_t handle_syscall(syscall syscall, uint64_t arg1, uint64_t arg2, uint64_t arg3,
                           uint64_t arg4, interrupts::regs *context);

    friend void syscall_interrupt_handler(interrupts::regs *context);
};
};  // namespace syscalls
};  // namespace influx