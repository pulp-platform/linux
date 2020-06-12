/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2008 Thomas Gleixner <tglx@linutronix.de>
 * Copyright (C) 2008-2009 Red Hat, Inc., Ingo Molnar
 * Copyright (C) 2009 Jaswinder Singh Rajput
 * Copyright (C) 2009 Advanced Micro Devices, Inc., Robert Richter
 * Copyright (C) 2008-2009 Red Hat, Inc., Peter Zijlstra
 * Copyright (C) 2009 Intel Corporation, <markus.t.metzger@intel.com>
 * Copyright (C) 2009 Google, Inc., Stephane Eranian
 * Copyright 2014 Tilera Corporation. All Rights Reserved.
 * Copyright (C) 2018 Andes Technology Corporation
 *
 * Perf_events support for RISC-V platforms.
 *
 * Since the spec. (as of now, Priv-Spec 1.10) does not provide enough
 * functionality for perf event to fully work, this file provides
 * the very basic framework only.
 *
 * For platform portings, please check Documentations/riscv/pmu.txt.
 *
 * The Copyright line includes x86 and tile ones.
 */

#include <linux/kprobes.h>
#include <linux/kernel.h>
#include <linux/kdebug.h>
#include <linux/mutex.h>
#include <linux/bitmap.h>
#include <linux/irq.h>
#include <linux/perf_event.h>
#include <linux/atomic.h>
#include <linux/of.h>
#include <asm/perf_event.h>
#include <asm/sbi.h>

#ifdef CONFIG_ARIANE_PMU
#define  USE_M_MODE 1

#if USE_M_MODE

#define         CSR_CYCLE            0xB00
#define         CSR_INSTRET          0xB02
// Performance counters (Machine Mode)
#define         CSR_L1_ICACHE_MISS   0xB03    // L1 Instr Cache Miss
#define         CSR_L1_DCACHE_MISS   0xB04    // L1 Data Cache Miss
#define         CSR_ITLB_MISS        0xB05    // ITLB Miss
#define         CSR_DTLB_MISS        0xB06    // DTLB Miss
#define         CSR_LOAD             0xB07    // Loads
#define         CSR_STORE            0xB08    // Stores
#define         CSR_EXCEPTION        0xB09    // Taken exceptions
#define         CSR_EXCEPTION_RET    0xB0A    // Exception return
#define         CSR_BRANCH_JUMP      0xB0B    // Software change of PC
#define         CSR_CALL             0xB0C    // Procedure call
#define         CSR_RET              0xB0D    // Procedure Return
#define         CSR_MIS_PREDICT      0xB0E    // Branch mis-predicted
#define         CSR_SB_FULL          0xB0F    // Scoreboard full
#define         CSR_IF_EMPTY         0xB10    // instruction fetch queue empty

#else

// Counters and Timers (User Mode - R/O Shadows)
#define         CSR_CYCLE             0xC00
#define         CSR_TIME              0xC01
#define         CSR_INSTRET           0xC02
  // Performance counters (User Mode - R/O Shadows)
#define         CSR_L1_ICACHE_MISS    0xC03  // L1 Instr Cache Miss
#define         CSR_L1_DCACHE_MISS    0xC04  // L1 Data Cache Miss
#define         CSR_ITLB_MISS         0xC05  // ITLB Miss
#define         CSR_DTLB_MISS         0xC06  // DTLB Miss
#define         CSR_LOAD              0xC07  // Loads
#define         CSR_STORE             0xC08  // Stores
#define         CSR_EXCEPTION         0xC09  // Taken exceptions
#define         CSR_EXCEPTION_RET     0xC0A  // Exception return
#define         CSR_BRANCH_JUMP       0xC0B  // Software change of PC
#define         CSR_CALL              0xC0C  // Procedure call
#define         CSR_RET               0xC0D  // Procedure Return
#define         CSR_MIS_PREDICT       0xC0E  // Branch mis-predicted
#define         CSR_SB_FULL           0xC0F  // Scoreboard full
#define         CSR_IF_EMPTY          0xC10  // instruction fetch queue empty

#endif
#endif

static const struct riscv_pmu *riscv_pmu __read_mostly;
static DEFINE_PER_CPU(struct cpu_hw_events, cpu_hw_events);

#ifdef CONFIG_ARIANE_PMU
static const int riscv_event_idx_csr_map[] = {
	[RISCV_PMU_CYCLE]		= CSR_CYCLE,
	[RISCV_PMU_INSTRET]		= CSR_INSTRET,
	[RISCV_OP_L1_ICACHE_MISS]	= CSR_L1_ICACHE_MISS,
	[RISCV_OP_L1_DCACHE_MISS]		= CSR_L1_DCACHE_MISS,
	[RISCV_OP_ITLB_MISS]	= CSR_ITLB_MISS,
	[RISCV_OP_DTLB_MISS]		= CSR_DTLB_MISS,
	[RISCV_OP_LOAD]		= CSR_LOAD,
	[RISCV_OP_STORE]		= CSR_STORE,
	[RISCV_OP_EXCEPTION]		= CSR_EXCEPTION,
	[RISCV_OP_EXCEPTION_RET]	= CSR_EXCEPTION_RET,
	[RISCV_OP_BRANCH_JUMP]		= CSR_BRANCH_JUMP,
	[RISCV_OP_CALL]	= CSR_CALL,
	[RISCV_OP_RET]		= CSR_RET,
	[RISCV_OP_MIS_PREDICT]		= CSR_MIS_PREDICT,
	[RISCV_OP_SB_FULL]		= CSR_SB_FULL,
	[RISCV_OP_IF_EMPTY]		= CSR_IF_EMPTY,
	//[RISCV_OP_DCACHE_RW]	= 0xC11,
};
#endif

/*
 * Hardware & cache maps and their methods
 */
#ifdef CONFIG_ARIANE_PMU
static const int riscv_hw_event_map[] = {
 	[PERF_COUNT_HW_CPU_CYCLES]		= RISCV_PMU_CYCLE,
 	[PERF_COUNT_HW_INSTRUCTIONS]		= RISCV_PMU_INSTRET,
 	[PERF_COUNT_HW_CACHE_REFERENCES]	= RISCV_OP_UNSUPP,
	[PERF_COUNT_HW_CACHE_MISSES]		= RISCV_OP_UNSUPP,
	[PERF_COUNT_HW_BRANCH_INSTRUCTIONS]	= RISCV_OP_BRANCH_JUMP,
	[PERF_COUNT_HW_BRANCH_MISSES]		= RISCV_OP_MIS_PREDICT,
 	[PERF_COUNT_HW_BUS_CYCLES]		= RISCV_OP_UNSUPP,
 };
#else
static const int riscv_hw_event_map[] = {
	[PERF_COUNT_HW_CPU_CYCLES]		= RISCV_PMU_CYCLE,
	[PERF_COUNT_HW_INSTRUCTIONS]		= RISCV_PMU_INSTRET,
	[PERF_COUNT_HW_CACHE_REFERENCES]	= RISCV_OP_UNSUPP,
	[PERF_COUNT_HW_CACHE_MISSES]		= RISCV_OP_UNSUPP,
	[PERF_COUNT_HW_BRANCH_INSTRUCTIONS]	= RISCV_OP_UNSUPP,
	[PERF_COUNT_HW_BRANCH_MISSES]		= RISCV_OP_UNSUPP,
	[PERF_COUNT_HW_BUS_CYCLES]		= RISCV_OP_UNSUPP,
};
#endif

#define C(x) PERF_COUNT_HW_CACHE_##x
#ifdef CONFIG_ARIANE_PMU
static const int riscv_cache_event_map[PERF_COUNT_HW_CACHE_MAX]
[PERF_COUNT_HW_CACHE_OP_MAX]
[PERF_COUNT_HW_CACHE_RESULT_MAX] = {
	[C(L1D)] = {
		[C(OP_READ)] = {
			[C(RESULT_ACCESS)] = RISCV_OP_LOAD,
			[C(RESULT_MISS)] = RISCV_OP_L1_DCACHE_MISS,
		},
		[C(OP_WRITE)] = {
			[C(RESULT_ACCESS)] = RISCV_OP_STORE,
			[C(RESULT_MISS)] = RISCV_OP_L1_DCACHE_MISS,
		},
		[C(OP_PREFETCH)] = {
			[C(RESULT_ACCESS)] = RISCV_OP_LOAD,
			[C(RESULT_MISS)] = RISCV_OP_L1_DCACHE_MISS,
		},
	},
	[C(L1I)] = {
		[C(OP_READ)] = {
			[C(RESULT_ACCESS)] = RISCV_OP_IF_EMPTY,
			[C(RESULT_MISS)] = RISCV_OP_L1_ICACHE_MISS,
		},
		[C(OP_WRITE)] = {
			[C(RESULT_ACCESS)] = RISCV_OP_IF_EMPTY,
			[C(RESULT_MISS)] = RISCV_OP_L1_ICACHE_MISS,
		},
		[C(OP_PREFETCH)] = {
			[C(RESULT_ACCESS)] = RISCV_OP_IF_EMPTY,
			[C(RESULT_MISS)] = RISCV_OP_L1_ICACHE_MISS,
		},
	},
	[C(LL)] = {
		[C(OP_READ)] = {
			[C(RESULT_ACCESS)] = RISCV_OP_UNSUPP,
			[C(RESULT_MISS)] = RISCV_OP_UNSUPP,
		},
		[C(OP_WRITE)] = {
			[C(RESULT_ACCESS)] = RISCV_OP_UNSUPP,
			[C(RESULT_MISS)] = RISCV_OP_UNSUPP,
		},
		[C(OP_PREFETCH)] = {
			[C(RESULT_ACCESS)] = RISCV_OP_UNSUPP,
			[C(RESULT_MISS)] = RISCV_OP_UNSUPP,
		},
	},
	[C(DTLB)] = {
		[C(OP_READ)] = {
			[C(RESULT_ACCESS)] =  RISCV_OP_UNSUPP,
			[C(RESULT_MISS)] =  RISCV_OP_DTLB_MISS,
		},
		[C(OP_WRITE)] = {
			[C(RESULT_ACCESS)] = RISCV_OP_UNSUPP,
			[C(RESULT_MISS)] = RISCV_OP_DTLB_MISS,
		},
		[C(OP_PREFETCH)] = {
			[C(RESULT_ACCESS)] = RISCV_OP_UNSUPP,
			[C(RESULT_MISS)] = RISCV_OP_DTLB_MISS,
		},
	},
	[C(ITLB)] = {
		[C(OP_READ)] = {
			[C(RESULT_ACCESS)] = RISCV_OP_UNSUPP,
			[C(RESULT_MISS)] = RISCV_OP_ITLB_MISS,
		},
		[C(OP_WRITE)] = {
			[C(RESULT_ACCESS)] = RISCV_OP_UNSUPP,
			[C(RESULT_MISS)] = RISCV_OP_ITLB_MISS,
		},
		[C(OP_PREFETCH)] = {
			[C(RESULT_ACCESS)] = RISCV_OP_UNSUPP,
			[C(RESULT_MISS)] = RISCV_OP_ITLB_MISS,
		},
	},
	[C(BPU)] = {
		[C(OP_READ)] = {
			[C(RESULT_ACCESS)] = RISCV_OP_UNSUPP,
			[C(RESULT_MISS)] = RISCV_OP_UNSUPP,
		},
		[C(OP_WRITE)] = {
			[C(RESULT_ACCESS)] = RISCV_OP_UNSUPP,
			[C(RESULT_MISS)] = RISCV_OP_UNSUPP,
		},
		[C(OP_PREFETCH)] = {
			[C(RESULT_ACCESS)] = RISCV_OP_UNSUPP,
			[C(RESULT_MISS)] = RISCV_OP_UNSUPP,
		},
	},
};
#else
static const int riscv_cache_event_map[PERF_COUNT_HW_CACHE_MAX]
[PERF_COUNT_HW_CACHE_OP_MAX]
[PERF_COUNT_HW_CACHE_RESULT_MAX] = {
	[C(L1D)] = {
		[C(OP_READ)] = {
			[C(RESULT_ACCESS)] = RISCV_OP_UNSUPP,
			[C(RESULT_MISS)] = RISCV_OP_UNSUPP,
		},
		[C(OP_WRITE)] = {
			[C(RESULT_ACCESS)] = RISCV_OP_UNSUPP,
			[C(RESULT_MISS)] = RISCV_OP_UNSUPP,
		},
		[C(OP_PREFETCH)] = {
			[C(RESULT_ACCESS)] = RISCV_OP_UNSUPP,
			[C(RESULT_MISS)] = RISCV_OP_UNSUPP,
		},
	},
	[C(L1I)] = {
		[C(OP_READ)] = {
			[C(RESULT_ACCESS)] = RISCV_OP_UNSUPP,
			[C(RESULT_MISS)] = RISCV_OP_UNSUPP,
		},
		[C(OP_WRITE)] = {
			[C(RESULT_ACCESS)] = RISCV_OP_UNSUPP,
			[C(RESULT_MISS)] = RISCV_OP_UNSUPP,
		},
		[C(OP_PREFETCH)] = {
			[C(RESULT_ACCESS)] = RISCV_OP_UNSUPP,
			[C(RESULT_MISS)] = RISCV_OP_UNSUPP,
		},
	},
	[C(LL)] = {
		[C(OP_READ)] = {
			[C(RESULT_ACCESS)] = RISCV_OP_UNSUPP,
			[C(RESULT_MISS)] = RISCV_OP_UNSUPP,
		},
		[C(OP_WRITE)] = {
			[C(RESULT_ACCESS)] = RISCV_OP_UNSUPP,
			[C(RESULT_MISS)] = RISCV_OP_UNSUPP,
		},
		[C(OP_PREFETCH)] = {
			[C(RESULT_ACCESS)] = RISCV_OP_UNSUPP,
			[C(RESULT_MISS)] = RISCV_OP_UNSUPP,
		},
	},
	[C(DTLB)] = {
		[C(OP_READ)] = {
			[C(RESULT_ACCESS)] =  RISCV_OP_UNSUPP,
			[C(RESULT_MISS)] =  RISCV_OP_UNSUPP,
		},
		[C(OP_WRITE)] = {
			[C(RESULT_ACCESS)] = RISCV_OP_UNSUPP,
			[C(RESULT_MISS)] = RISCV_OP_UNSUPP,
		},
		[C(OP_PREFETCH)] = {
			[C(RESULT_ACCESS)] = RISCV_OP_UNSUPP,
			[C(RESULT_MISS)] = RISCV_OP_UNSUPP,
		},
	},
	[C(ITLB)] = {
		[C(OP_READ)] = {
			[C(RESULT_ACCESS)] = RISCV_OP_UNSUPP,
			[C(RESULT_MISS)] = RISCV_OP_UNSUPP,
		},
		[C(OP_WRITE)] = {
			[C(RESULT_ACCESS)] = RISCV_OP_UNSUPP,
			[C(RESULT_MISS)] = RISCV_OP_UNSUPP,
		},
		[C(OP_PREFETCH)] = {
			[C(RESULT_ACCESS)] = RISCV_OP_UNSUPP,
			[C(RESULT_MISS)] = RISCV_OP_UNSUPP,
		},
	},
	[C(BPU)] = {
		[C(OP_READ)] = {
			[C(RESULT_ACCESS)] = RISCV_OP_UNSUPP,
			[C(RESULT_MISS)] = RISCV_OP_UNSUPP,
		},
		[C(OP_WRITE)] = {
			[C(RESULT_ACCESS)] = RISCV_OP_UNSUPP,
			[C(RESULT_MISS)] = RISCV_OP_UNSUPP,
		},
		[C(OP_PREFETCH)] = {
			[C(RESULT_ACCESS)] = RISCV_OP_UNSUPP,
			[C(RESULT_MISS)] = RISCV_OP_UNSUPP,
		},
	},
};
#endif

static int riscv_map_hw_event(u64 config)
{
	if (config >= riscv_pmu->max_events)
		return -EINVAL;

	return riscv_pmu->hw_events[config];
}

int riscv_map_cache_decode(u64 config, unsigned int *type,
			   unsigned int *op, unsigned int *result)
{
#ifdef CONFIG_ARIANE_PMU
	*type = config & 0xFF;
	*op = (config >> 8) & 0xFF;
	*result = (config >> 16) & 0xFF;
	return 0;
#else
	return -ENOENT;
#endif
}

static int riscv_map_cache_event(u64 config)
{
	unsigned int type, op, result;
	int err = -ENOENT;
		int code;

	err = riscv_map_cache_decode(config, &type, &op, &result);
	if (!riscv_pmu->cache_events || err)
		return err;

	if (type >= PERF_COUNT_HW_CACHE_MAX ||
	    op >= PERF_COUNT_HW_CACHE_OP_MAX ||
	    result >= PERF_COUNT_HW_CACHE_RESULT_MAX)
		return -EINVAL;

	code = (*riscv_pmu->cache_events)[type][op][result];
	if (code == RISCV_OP_UNSUPP)
		return -EINVAL;

	return code;
}

/*
 * Low-level functions: reading/writing counters
 */

static inline u64 read_counter(int idx)
{
	u64 val = 0;

#ifdef CONFIG_ARIANE_PMU
	if (idx >= RISCV_PMU_CYCLE && idx <= RISCV_OP_IF_EMPTY) {
#if USE_M_MODE
		if (RISCV_OP_BRANCH_JUMP == idx) {
			//val = sbi_pmu_csr_read(riscv_event_idx_csr_map[RISCV_OP_EXCEPTION_RET]);
			//val += sbi_pmu_csr_read(riscv_event_idx_csr_map[RISCV_OP_RET]);
			//val += sbi_pmu_csr_read(riscv_event_idx_csr_map[RISCV_OP_CALL]);
			val = sbi_pmu_csr_read(riscv_event_idx_csr_map[RISCV_OP_BRANCH_JUMP]);
		// } else if (RISCV_OP_DCACHE_RW == idx) {
		// 	val = sbi_pmu_csr_read(riscv_event_idx_csr_map[RISCV_OP_LOAD]);
		// 	val += sbi_pmu_csr_read(riscv_event_idx_csr_map[RISCV_OP_STORE]);
		} else
			val = sbi_pmu_csr_read(riscv_event_idx_csr_map[idx]);
#else
		switch(riscv_event_idx_csr_map[idx]) {
			case CSR_CYCLE:
				val = csr_read(CSR_CYCLE);
				break;
			case CSR_TIME:
				val = csr_read(CSR_TIME);
				break;
			case CSR_INSTRET:
				val = csr_read(CSR_INSTRET);
				break;
			case CSR_L1_ICACHE_MISS:
				val = csr_read(CSR_L1_ICACHE_MISS);
				break;
			case CSR_L1_DCACHE_MISS:
				val = csr_read(CSR_L1_DCACHE_MISS);
				break;
			case CSR_ITLB_MISS:
				val = csr_read(CSR_ITLB_MISS);
				break;
			case CSR_DTLB_MISS:
				val = csr_read(CSR_DTLB_MISS);
				break;
			case CSR_LOAD:
				val = csr_read(CSR_LOAD);
				break;
			case CSR_STORE:
				val = csr_read(CSR_STORE);
				break;
			// case 0xC11:
			// 	val = csr_read(CSR_LOAD);
			// 	val += csr_read(CSR_STORE);
			// 	break;
			case CSR_EXCEPTION:
				val = csr_read(CSR_EXCEPTION);
				break;
			case CSR_EXCEPTION_RET:
				val = csr_read(CSR_EXCEPTION_RET);
				break;
			case CSR_BRANCH_JUMP:
				//val = csr_read(CSR_EXCEPTION_RET);
				//val += csr_read(CSR_RET);
				//val += csr_read(CSR_CALL);
				val = csr_read(CSR_BRANCH_JUMP);
				break;
			case CSR_CALL:
				val = csr_read(CSR_CALL);
				break;
			case CSR_RET:
				val = csr_read(CSR_RET);
				break;
			case CSR_MIS_PREDICT:
				val = csr_read(CSR_MIS_PREDICT);
				break;
			case CSR_SB_FULL:
				val = csr_read(CSR_SB_FULL);
				break;
			case CSR_IF_EMPTY:
				val = csr_read(CSR_IF_EMPTY);
				break;
			default:
				break;
		}
#endif
	} else {
		WARN_ON_ONCE(idx < 0 ||	idx > RISCV_MAX_COUNTERS);
		return -EINVAL;
	}
#else
	switch (idx) {
	case RISCV_PMU_CYCLE:
		val = csr_read(cycle);
		break;
	case RISCV_PMU_INSTRET:
		val = csr_read(instret);
		break;
	default:
		WARN_ON_ONCE(idx < 0 ||	idx > RISCV_MAX_COUNTERS);
		return -EINVAL;
	}
#endif

	return val;
}

static inline void write_counter(int idx, u64 value)
{
	/* currently not supported */
	WARN_ON_ONCE(1);
}

/*
 * pmu->read: read and update the counter
 *
 * Other architectures' implementation often have a xxx_perf_event_update
 * routine, which can return counter values when called in the IRQ, but
 * return void when being called by the pmu->read method.
 */
static void riscv_pmu_read(struct perf_event *event)
{
	struct hw_perf_event *hwc = &event->hw;
	u64 prev_raw_count, new_raw_count;
	u64 oldval;
	int idx = hwc->idx;
	u64 delta;

	do {
		prev_raw_count = local64_read(&hwc->prev_count);
		new_raw_count = read_counter(idx);

		oldval = local64_cmpxchg(&hwc->prev_count, prev_raw_count,
					 new_raw_count);
	} while (oldval != prev_raw_count);

	/*
	 * delta is the value to update the counter we maintain in the kernel.
	 */
	delta = (new_raw_count - prev_raw_count) &
		((1ULL << riscv_pmu->counter_width) - 1);
	local64_add(delta, &event->count);
	/*
	 * Something like local64_sub(delta, &hwc->period_left) here is
	 * needed if there is an interrupt for perf.
	 */
}

/*
 * State transition functions:
 *
 * stop()/start() & add()/del()
 */

/*
 * pmu->stop: stop the counter
 */
static void riscv_pmu_stop(struct perf_event *event, int flags)
{
	struct hw_perf_event *hwc = &event->hw;

	WARN_ON_ONCE(hwc->state & PERF_HES_STOPPED);
	hwc->state |= PERF_HES_STOPPED;

	if ((flags & PERF_EF_UPDATE) && !(hwc->state & PERF_HES_UPTODATE)) {
		riscv_pmu->pmu->read(event);
		hwc->state |= PERF_HES_UPTODATE;
	}
}

/*
 * pmu->start: start the event.
 */
static void riscv_pmu_start(struct perf_event *event, int flags)
{
	struct hw_perf_event *hwc = &event->hw;

	if (WARN_ON_ONCE(!(event->hw.state & PERF_HES_STOPPED)))
		return;

	if (flags & PERF_EF_RELOAD) {
		WARN_ON_ONCE(!(event->hw.state & PERF_HES_UPTODATE));

		/*
		 * Set the counter to the period to the next interrupt here,
		 * if you have any.
		 */
	}

	hwc->state = 0;
	perf_event_update_userpage(event);

	/*
	 * Since we cannot write to counters, this serves as an initialization
	 * to the delta-mechanism in pmu->read(); otherwise, the delta would be
	 * wrong when pmu->read is called for the first time.
	 */
	local64_set(&hwc->prev_count, read_counter(hwc->idx));
}

/*
 * pmu->add: add the event to PMU.
 */
static int riscv_pmu_add(struct perf_event *event, int flags)
{
	struct cpu_hw_events *cpuc = this_cpu_ptr(&cpu_hw_events);
	struct hw_perf_event *hwc = &event->hw;

	if (cpuc->n_events == riscv_pmu->num_counters)
		return -ENOSPC;

	/*
	 * We don't have general conunters, so no binding-event-to-counter
	 * process here.
	 *
	 * Indexing using hwc->config generally not works, since config may
	 * contain extra information, but here the only info we have in
	 * hwc->config is the event index.
	 */
	hwc->idx = hwc->config;
	cpuc->events[hwc->idx] = event;
	cpuc->n_events++;

	hwc->state = PERF_HES_UPTODATE | PERF_HES_STOPPED;

	if (flags & PERF_EF_START)
		riscv_pmu->pmu->start(event, PERF_EF_RELOAD);

	return 0;
}

/*
 * pmu->del: delete the event from PMU.
 */
static void riscv_pmu_del(struct perf_event *event, int flags)
{
	struct cpu_hw_events *cpuc = this_cpu_ptr(&cpu_hw_events);
	struct hw_perf_event *hwc = &event->hw;

	cpuc->events[hwc->idx] = NULL;
	cpuc->n_events--;
	riscv_pmu->pmu->stop(event, PERF_EF_UPDATE);
	perf_event_update_userpage(event);
}

/*
 * Interrupt: a skeletion for reference.
 */

static DEFINE_MUTEX(pmc_reserve_mutex);

irqreturn_t riscv_base_pmu_handle_irq(int irq_num, void *dev)
{
	return IRQ_NONE;
}

static int reserve_pmc_hardware(void)
{
	int err = 0;

	mutex_lock(&pmc_reserve_mutex);
	if (riscv_pmu->irq >= 0 && riscv_pmu->handle_irq) {
		err = request_irq(riscv_pmu->irq, riscv_pmu->handle_irq,
				  IRQF_PERCPU, "riscv-base-perf", NULL);
	}
	mutex_unlock(&pmc_reserve_mutex);

	return err;
}

void release_pmc_hardware(void)
{
	mutex_lock(&pmc_reserve_mutex);
	if (riscv_pmu->irq >= 0)
		free_irq(riscv_pmu->irq, NULL);
	mutex_unlock(&pmc_reserve_mutex);
}

/*
 * Event Initialization/Finalization
 */

static atomic_t riscv_active_events = ATOMIC_INIT(0);

static void riscv_event_destroy(struct perf_event *event)
{
	if (atomic_dec_return(&riscv_active_events) == 0)
		release_pmc_hardware();
}

static int riscv_event_init(struct perf_event *event)
{
	struct perf_event_attr *attr = &event->attr;
	struct hw_perf_event *hwc = &event->hw;
	int err;
	int code;

	if (atomic_inc_return(&riscv_active_events) == 1) {
		err = reserve_pmc_hardware();

		if (err) {
			pr_warn("PMC hardware not available\n");
			atomic_dec(&riscv_active_events);
			return -EBUSY;
		}
	}

	switch (event->attr.type) {
	case PERF_TYPE_HARDWARE:
		code = riscv_pmu->map_hw_event(attr->config);
		break;
	case PERF_TYPE_HW_CACHE:
		code = riscv_pmu->map_cache_event(attr->config);
		break;
	case PERF_TYPE_RAW:
		return -EOPNOTSUPP;
	default:
		return -ENOENT;
	}

	event->destroy = riscv_event_destroy;
	if (code < 0) {
		event->destroy(event);
		return code;
	}

	/*
	 * idx is set to -1 because the index of a general event should not be
	 * decided until binding to some counter in pmu->add().
	 *
	 * But since we don't have such support, later in pmu->add(), we just
	 * use hwc->config as the index instead.
	 */
	hwc->config = code;
	hwc->idx = -1;

	return 0;
}

/*
 * Initialization
 */

static struct pmu min_pmu = {
	.name		= "riscv-base",
	.event_init	= riscv_event_init,
	.add		= riscv_pmu_add,
	.del		= riscv_pmu_del,
	.start		= riscv_pmu_start,
	.stop		= riscv_pmu_stop,
	.read		= riscv_pmu_read,
};

static const struct riscv_pmu riscv_base_pmu = {
	.pmu = &min_pmu,
	.max_events = ARRAY_SIZE(riscv_hw_event_map),
	.map_hw_event = riscv_map_hw_event,
	.hw_events = riscv_hw_event_map,
	.map_cache_event = riscv_map_cache_event,
	.cache_events = &riscv_cache_event_map,
	.counter_width = 63,
#ifdef CONFIG_ARIANE_PMU
	.num_counters = RISCV_MAX_COUNTERS + 0,
#else
	.num_counters = RISCV_BASE_COUNTERS + 0,
#endif
	.handle_irq = &riscv_base_pmu_handle_irq,

	/* This means this PMU has no IRQ. */
	.irq = -1,
};

static const struct of_device_id riscv_pmu_of_ids[] = {
	{.compatible = "riscv,base-pmu",	.data = &riscv_base_pmu},
	{ /* sentinel value */ }
};

int __init init_hw_perf_events(void)
{
	struct device_node *node = of_find_node_by_type(NULL, "pmu");
	const struct of_device_id *of_id;

	riscv_pmu = &riscv_base_pmu;

	if (node) {
		of_id = of_match_node(riscv_pmu_of_ids, node);

		if (of_id)
			riscv_pmu = of_id->data;
		of_node_put(node);
	}

	perf_pmu_register(riscv_pmu->pmu, "cpu", PERF_TYPE_RAW);
	return 0;
}
arch_initcall(init_hw_perf_events);
