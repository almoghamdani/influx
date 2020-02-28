#pragma once
#include <kernel/drivers/driver_manager.h>
#include <kernel/interrupts/interrupt_manager.h>
#include <sys/boot_info.h>

#define OS_NAME "Influx"
#define KERNEL_VERSION "0.3.0"

namespace influx {
class kernel {
   public:
    static void start(const boot_info info);

    inline static interrupts::interrupt_manager *interrupt_manager() { return _interrupt_manager; }
    inline static drivers::driver_manager *driver_manager() { return _driver_manager; }

   private:
    inline static interrupts::interrupt_manager *_interrupt_manager = nullptr;
    inline static drivers::driver_manager *_driver_manager = nullptr;

    static void early_kmain(const boot_info info);
    static void kmain(const boot_info info);
};
};  // namespace influx