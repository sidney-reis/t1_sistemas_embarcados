/**
 * @file main.c
 * @author Sergio Johann Filho
 * @date January 2016
 *
 * @section LICENSE
 *
 * This source code is licensed under the GNU General Public License,
 * Version 2.  See the file 'doc/license/gpl-2.0.txt' for more details.
 *
 * @section DESCRIPTION
 *
 * The HellfireOS realtime operating system kernel.
 *
 */

#include <hal.h>
#include <libc.h>
#include <kprintf.h>
#include <malloc.h>
#include <queue.h>
#include <kernel.h>
#include <panic.h>
#include <scheduler.h>
#include <task.h>
#include <processor.h>
#include <main.h>
#include <ecodes.h>

static void print_config(void)
{
  kprintf("\n===========================================================");
  kprintf("\nHellfireOS %s [%s, %s]", KERN_VER, __DATE__, __TIME__);
  kprintf("\nEmbedded Systems Group - GSE, PUCRS - [2007 - 2017]");
  kprintf("\n===========================================================\n");
  kprintf("\narch:          %s", CPU_ARCH);
  kprintf("\nsys clk:       %d kHz", CPU_SPEED/1000);
  if (TIME_SLICE != 0)
    kprintf("\ntime slice:    %d us", TIME_SLICE);
  kprintf("\nheap size:     %d bytes", sizeof(krnl_heap));
  kprintf("\nmax tasks:     %d\n", MAX_TASKS);
}

static void clear_tcb(void)
{
  uint16_t i;

  for(i = 0; i < MAX_TASKS; i++){
    krnl_task = &krnl_tcb[i];
    krnl_task->id = -1;
    memset(krnl_task->name, 0, sizeof(krnl_task->name));
    krnl_task->state = TASK_IDLE;
    krnl_task->priority = 0;
    krnl_task->priority_rem = 0;
    krnl_task->delay = 0;
    krnl_task->rtjobs = 0;
    krnl_task->bgjobs = 0;
    krnl_task->deadline_misses = 0;
    krnl_task->period = 0;
    krnl_task->capacity = 0;
    krnl_task->deadline = 0;
    krnl_task->capacity_rem = 0;
    krnl_task->deadline_rem = 0;
    krnl_task->ptask = NULL;
    krnl_task->pstack = NULL;
    krnl_task->stack_size = 0;
    krnl_task->other_data = 0;
  }

  krnl_tasks = 0;
  krnl_current_task = 0;
  krnl_schedule = 0;
}

static void clear_pcb(void)
{
  krnl_pcb.sched_rt = sched_rma;
  krnl_pcb.sched_be = sched_priorityrr;
  krnl_pcb.coop_cswitch = 0;
  krnl_pcb.preempt_cswitch = 0;
  krnl_pcb.interrupts = 0;
  krnl_pcb.tick_time = 0;
}

static void init_queues(void)
{
  krnl_run_queue = hf_queue_create(MAX_TASKS);
  if (krnl_run_queue == NULL) panic(PANIC_OOM);
  krnl_delay_queue = hf_queue_create(MAX_TASKS);
  if (krnl_delay_queue == NULL) panic(PANIC_OOM);
  krnl_rt_queue = hf_queue_create(MAX_TASKS);
  if (krnl_rt_queue == NULL) panic(PANIC_OOM);
  krnl_aperiodic_queue = hf_queue_create(MAX_TASKS);
  if (krnl_aperiodic_queue == NULL) panic(PANIC_OOM);
}

static void idletask(void)
{
  kprintf("\nKERNEL: free heap: %d bytes", krnl_free);
  kprintf("\nKERNEL: HellfireOS is running\n");

  hf_schedlock(0);

  for (;;){
    _cpu_idle();
  }
}

static void polling_server_task(void)
{
  int32_t polling_id = hf_selfid();
  struct aperiodic_entry *polling_task;
  polling_task = &krnl_tcb[polling_id];
	int16_t SERVER_CAPACITY = polling_task->capacity;
	int16_t server_fuel = SERVER_CAPACITY;
  int32_t rc;
  int32_t status;

  for(;;)
  {
  	if(server_fuel == 0)
  		server_fuel = SERVER_CAPACITY;

    kprintf("\n-- POLLING SERVER  --\n");

    struct aperiodic_entry *next_aperiodic;

    if(hf_queue_count(krnl_aperiodic_queue)==0){
    	kprintf("No aperiodic task to run\n");
    	hf_yield();
    	continue;
    }
    next_aperiodic = polling_server_scheduler();

    if(server_fuel >= next_aperiodic->capacity){
    	server_fuel -= next_aperiodic->capacity;
    }
    else{
    	next_aperiodic->capacity -= server_fuel;
    	server_fuel = 0;
    	hf_queue_addtail(krnl_aperiodic_queue, next_aperiodic);
    }

    polling_task->state = TASK_READY;
    next_aperiodic->state = TASK_RUNNING;
    status = _di();
    krnl_task = next_aperiodic;
    krnl_current_task = next_aperiodic->id;
    rc = setjmp(polling_task->task_context);
    if(rc != 0){
    	continue;
    	_ei(status);
    }
    kprintf("Running aperiodic task - id:%d\n", krnl_current_task);
    _restoreexec(krnl_task->task_context, 1, krnl_current_task);
  }
}

static void dummy_task(void)
{
	int i;
  for(i=0;i<20000;++i){}
}

static void aperiodic_task_generator(void)
{
  for(;;)
  {
    int random_delay = random() % 140 + 60;
    kprintf("\nGenerating Aperiodic Task\n");
    hf_spawn(dummy_task, 0, 18, 0, "dummy task", 1024);
    delay_ms(random_delay);
  }
}



/**
 * @internal
 * @brief HellfireOS kernel entry point and system initialization.
 *
 * @return should not return.
 *
 * We assume that the following machine state has been already set
 * before this routine.
 *  - Kernel BSS section is filled with 0.
 *  - Kernel stack is configured.
 *  - All interrupts are disabled.
 *  - Minimum page table is set. (MMU systems only)
 */
int main(void)
{
  static uint32_t oops=0xbaadd00d;

  _hardware_init();
  hf_schedlock(1);
  _di();
  kprintf("\nKERNEL: booting...");
  if (oops == 0xbaadd00d){
    oops = 0;
    print_config();
    _vm_init();
    clear_tcb();
    clear_pcb();
    init_queues();
    _sched_init();
    _irq_init();
    _timer_init();
    _timer_reset();
    hf_spawn(idletask, 0, 0, 0, "idle task", 1024);
    hf_spawn(polling_server_task, 20, 6, 20, "polling server", 1024);
    hf_spawn(aperiodic_task_generator, 10, 2, 10, "aperiodic task generator", 1024);
    _device_init();
    _task_init();
    app_main();
    _restoreexec(krnl_task->task_context, 1, krnl_current_task);
    panic(PANIC_ABORTED);
  }else{
    panic(PANIC_GPF);
  }

  return 0;
}
