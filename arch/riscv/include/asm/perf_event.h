/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2018 SiFive
 * Copyright (C) 2018 Andes Technology Corporation
 *
 */

#ifndef _ASM_RISCV_PERF_EVENT_H
#define _ASM_RISCV_PERF_EVENT_H

#include <linux/perf_event.h>
#include <linux/ptrace.h>
#include <linux/interrupt.h>

#define RISCV_BASE_COUNTERS	2

/*
 * The RISCV_MAX_COUNTERS parameter should be specified.
 */

#ifdef CONFIG_RISCV_BASE_PMU
#define RISCV_MAX_COUNTERS	2
#endif

#ifdef CONFIG_ARIANE_PMU
#undef  RISCV_MAX_COUNTERS
#define RISCV_MAX_COUNTERS	16
#endif

#ifndef RISCV_MAX_COUNTERS
#error "Please provide a valid RISCV_MAX_COUNTERS for the PMU."
#endif

/*
 * These are the indexes of bits in counteren register *minus* 1,
 * except for cycle.  It would be coherent if it can directly mapped
 * to counteren bit definition, but there is a *time* register at
 * counteren[1].  Per-cpu structure is scarce resource here.
 *
 * According to the spec, an implementation can support counter up to
 * mhpmcounter31, but many high-end processors has at most 6 general
 * PMCs, we give the definition to MHPMCOUNTER8 here.
 */
#define RISCV_PMU_CYCLE		0
#define RISCV_PMU_INSTRET	1
#define RISCV_PMU_MHPMCOUNTER3	2
#define RISCV_PMU_MHPMCOUNTER4	3
#define RISCV_PMU_MHPMCOUNTER5	4
#define RISCV_PMU_MHPMCOUNTER6	5
#define RISCV_PMU_MHPMCOUNTER7	6
#define RISCV_PMU_MHPMCOUNTER8	7

#define RISCV_OP_UNSUPP		(-EOPNOTSUPP)

#ifdef CONFIG_ARIANE_PMU

#define  RISCV_OP_L1_ICACHE_MISS   2    // L1 Instr Cache Miss
#define  RISCV_OP_L1_DCACHE_MISS   3    // L1 Data Cache Miss
#define  RISCV_OP_ITLB_MISS        4    // ITLB Miss
#define  RISCV_OP_DTLB_MISS        5    // DTLB Miss
#define  RISCV_OP_LOAD             6    // Loads
#define  RISCV_OP_STORE            7    // Stores
#define  RISCV_OP_EXCEPTION        8    // Taken exceptions
#define  RISCV_OP_EXCEPTION_RET    9    // Exception return
#define  RISCV_OP_BRANCH_JUMP      10    // Software change of PC
#define  RISCV_OP_CALL             11    // Procedure call
#define  RISCV_OP_RET              12    // Procedure Return
#define  RISCV_OP_MIS_PREDICT      13    // Branch mis-predicted
#define  RISCV_OP_SB_FULL          14    // Scoreboard full
#define  RISCV_OP_IF_EMPTY         15    // instruction fetch queue empty
// #define  RISCV_OP_DCACHE_RW		   16

#endif

struct cpu_hw_events {
	/* # currently enabled events*/
	int			n_events;
	/* currently enabled events */
	struct perf_event	*events[RISCV_MAX_COUNTERS];
	/* vendor-defined PMU data */
	void			*platform;
};

struct riscv_pmu {
	struct pmu	*pmu;

	/* generic hw/cache events table */
	const int	*hw_events;
	const int	(*cache_events)[PERF_COUNT_HW_CACHE_MAX]
				       [PERF_COUNT_HW_CACHE_OP_MAX]
				       [PERF_COUNT_HW_CACHE_RESULT_MAX];
	/* method used to map hw/cache events */
	int		(*map_hw_event)(u64 config);
	int		(*map_cache_event)(u64 config);

	/* max generic hw events in map */
	int		max_events;
	/* number total counters, 2(base) + x(general) */
	int		num_counters;
	/* the width of the counter */
	int		counter_width;

	/* vendor-defined PMU features */
	void		*platform;

	irqreturn_t	(*handle_irq)(int irq_num, void *dev);
	int		irq;
};

#endif /* _ASM_RISCV_PERF_EVENT_H */
