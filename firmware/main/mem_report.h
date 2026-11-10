#ifndef MEM_REPORT_H
#define MEM_REPORT_H

// Writes to the log how much internal and external memory is free, the biggest free block, the lowest the free memory ever was and every task with its
// stack (name, state, priority, stack never used, number). `why` says when it was asked. Used for the memory audit (docs/IMPROVEMENT_PLAN.md, section 5)
// and by BUGCFG {"mem":1}.
void mem_report(const char *why);

// A task calls this once when it starts, so the report can say how much of its stack it has really used (no need for the costly task list:
// turning FreeRTOS's trace facility on takes about 7 KB of internal RAM by itself, measured 2026-11-10).
void mem_task_note(const char *name, unsigned stack_bytes);

#endif
