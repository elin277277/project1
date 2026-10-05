#pragma once
// Deliberately small, single-threaded host fake: not a ThreadX scheduler or ABI test.
#include <cstdint>

using UINT = unsigned int;
using ULONG = std::uint32_t;
using UCHAR = unsigned char;
using CHAR = char;

constexpr UINT TX_SUCCESS = 0;
constexpr UINT TX_NOT_AVAILABLE = 1;
constexpr UINT TX_NO_INSTANCE = 2;
constexpr UINT TX_INHERIT = 1;
constexpr ULONG TX_NO_WAIT = 0;
constexpr ULONG TX_WAIT_FOREVER = UINT32_MAX;
constexpr ULONG TX_NO_TIME_SLICE = 0;
constexpr UINT TX_AUTO_START = 1;
constexpr UINT TX_INT_ENABLE = 0;
constexpr UINT TX_INT_DISABLE = 1;
constexpr ULONG TX_TIMER_TICKS_PER_SECOND = 1000;

struct TX_MUTEX { bool created{}; bool locked{}; UINT inherit{}; };
struct TX_SEMAPHORE { bool created{}; ULONG count{}; };
struct TX_THREAD {
    bool created{};
    void (*entry)(ULONG){};
    void *stack{};
    ULONG stack_size{};
    UINT priority{};
};

UINT tx_mutex_create(TX_MUTEX *, CHAR *, UINT);
UINT tx_mutex_get(TX_MUTEX *, ULONG);
UINT tx_mutex_put(TX_MUTEX *);
UINT tx_semaphore_create(TX_SEMAPHORE *, CHAR *, ULONG);
UINT tx_semaphore_get(TX_SEMAPHORE *, ULONG);
UINT tx_semaphore_put(TX_SEMAPHORE *);
UINT tx_interrupt_control(UINT);
UINT tx_thread_create(TX_THREAD *, CHAR *, void (*)(ULONG), ULONG, void *, ULONG,
                      UINT, UINT, ULONG, UINT);
UINT tx_thread_sleep(ULONG);
void tx_kernel_enter();
extern "C" void tx_application_define(void *);
