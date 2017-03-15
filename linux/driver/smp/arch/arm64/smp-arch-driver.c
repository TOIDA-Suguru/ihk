/* smp-arch-driver.c COPYRIGHT FUJITSU LIMITED 2015-2017 */
/**
 * \file smp-arch-driver.c
 * \brief
 *	IHK SMP-ARM64 Driver: IHK Host Driver
 *                          for partitioning an AARCH64 SMP chip
 */
#include <linux/delay.h>
#include <linux/errno.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/irqdomain.h>
#include <linux/list.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/of_address.h>
#include <linux/acpi.h>
#include <linux/version.h>
#include <linux/platform_device.h>
#include <linux/perf_event.h>
#include <linux/irqchip/arm-gic-v3.h>
#if LINUX_VERSION_CODE < KERNEL_VERSION(4,3,0)
# include <asm/pmu.h>
#else
# include <linux/perf/arm_pmu.h>
#endif
#include <asm/uaccess.h>
#include <uapi/linux/psci.h>
#include <ihk/misc/debug.h>
#include <ihk/ihk_host_user.h>
#include <dt-bindings/interrupt-controller/arm-gic.h>
#include "config-arm64.h"
#include "smp-driver.h"
#include "smp-defines-driver.h"

/*
 * IHK-SMP unexported kernel symbols
 */
#ifdef IHK_KSYM_gic_data_gicv2
#if IHK_KSYM_gic_data_gicv2
struct gic_chip_data_v2 *ihk_gic_data_v2 =
	(struct gic_chip_data_v2 *)
	IHK_KSYM_gic_data_gicv2;
#endif
#endif

#ifdef IHK_KSYM_gic_data_gicv3
#if IHK_KSYM_gic_data_gicv3
struct gic_chip_data_v3 *ihk_gic_data_v3 =
	(struct gic_chip_data_v3 *)
	IHK_KSYM_gic_data_gicv3;
#endif
#endif

#ifdef IHK_KSYM_gic_raise_softirq_gicv2
#if IHK_KSYM_gic_raise_softirq_gicv2
static void (*ihk___smp_cross_call_gicv2)(const struct cpumask *, unsigned int) =
	(void *)
	IHK_KSYM_gic_raise_softirq_gicv2;
#endif
#endif

#ifdef IHK_KSYM_gic_raise_softirq_gicv3
#if IHK_KSYM_gic_raise_softirq_gicv3
static void (*ihk___smp_cross_call_gicv3)(const struct cpumask *, unsigned int) =
	(void *)
	IHK_KSYM_gic_raise_softirq_gicv3;
#endif
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(4,4,0)
#ifdef IHK_KSYM___irq_domain_alloc_irqs
#if IHK_KSYM___irq_domain_alloc_irqs
static int (*ihk___irq_domain_alloc_irqs)(struct irq_domain *domain,
                                          int irq_base, unsigned int nr_irqs,
                                          int node, void *arg, bool realloc) =
	(void *)
	IHK_KSYM___irq_domain_alloc_irqs;
#endif
#endif

#ifdef IHK_KSYM_irq_domain_free_irqs
#if IHK_KSYM_irq_domain_free_irqs
static int (*ihk_irq_domain_free_irqs)(unsigned int virq, unsigned int nr_irqs) =
	(void *)
	IHK_KSYM_irq_domain_free_irqs;
#endif
#endif
#endif /* LINUX_VERSION_CODE < KERNEL_VERSION(4,4,0) */

#ifdef IHK_KSYM_irq_to_desc
#if IHK_KSYM_irq_to_desc
typedef struct irq_desc *(*irq_desc_star_fn_int_t)(unsigned int);
struct irq_desc *(*_irq_to_desc)(unsigned int irq) =
	(irq_desc_star_fn_int_t)
	IHK_KSYM_irq_to_desc;
#else /* exported */
#include <linux/irqnr.h>
struct irq_desc *(*_irq_to_desc)(unsigned int irq) = irq_to_desc;
#endif
#endif

#ifdef IHK_KSYM_psci_ops
#if IHK_KSYM_psci_ops
struct psci_operations *ihk_psci_ops =
	(struct psci_operations *)
	IHK_KSYM_psci_ops;
#endif
#endif

#ifdef IHK_KSYM___cpu_logical_map
#if IHK_KSYM___cpu_logical_map
static size_t ihk___cpu_logical_map_size = NR_CPUS;
u64 *ihk___cpu_logical_map =
	(u64 *)
	IHK_KSYM___cpu_logical_map;
#endif
#endif

#ifdef IHK_KSYM_invoke_psci_fn
unsigned long *ihk_invoke_psci_fn = (unsigned long *)IHK_KSYM_invoke_psci_fn;
#else
unsigned long *ihk_invoke_psci_fn = NULL;
#endif

#ifdef IHK_KSYM___invoke_psci_fn_hvc
unsigned long ihk___invoke_psci_fn_hvc = (unsigned long)IHK_KSYM___invoke_psci_fn_hvc;
#else
unsigned long ihk___invoke_psci_fn_hvc = 0;
#endif

#ifdef IHK_KSYM___invoke_psci_fn_smc
unsigned long ihk___invoke_psci_fn_smc = (unsigned long)IHK_KSYM___invoke_psci_fn_smc;
#else
unsigned long ihk___invoke_psci_fn_smc = 0;
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(4,4,0)
#
# ifdef IHK_KSYM_cpu_pmu
static struct arm_pmu **ihk_cpu_pmu = (struct arm_pmu**)IHK_KSYM_cpu_pmu;
# else
#  error "'struct arm_pmu cpu_pmu' address is unknown."
# endif
#
#else
#
# ifdef IHK_KSYM___oprofile_cpu_pmu
static struct arm_pmu **ihk_cpu_pmu = (struct arm_pmu**)IHK_KSYM___oprofile_cpu_pmu;
# else
#  error "'struct arm_pmu cpu_pmu' address is unknown."
# endif
#
#endif

#ifdef IHK_KSYM___irq_set_affinity
int (*ihk___irq_set_affinity)(unsigned int irq, const struct cpumask *mask, bool force) = (void*)IHK_KSYM___irq_set_affinity;
#else
# error "'__irq_set_affinity' address is unknown."
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(4,6,0)
#ifdef IHK_KSYM_arch_timer_use_virtual
static unsigned long is_arch_timer_use_virt(void)
{
	bool *arch_timer_use = (bool *)IHK_KSYM_arch_timer_use_virtual;
	return (unsigned long)*arch_timer_use;
}
#else
#error "'arch_timer_use_virtual' address is unknown."
#endif

#else /* LINUX_VERSION_CODE < KERNEL_VERSION(4,6,0) */
#ifdef IHK_KSYM_arch_timer_uses_ppi
static unsigned long is_arch_timer_use_virt(void)
{
	enum ppi_nr {
		PHYS_SECURE_PPI,
		PHYS_NONSECURE_PPI,
		VIRT_PPI,
		HYP_PPI,
		MAX_TIMER_PPI
	};
	enum ppi_nr *arch_timer_use = (enum ppi_nr *)IHK_KSYM_arch_timer_uses_ppi;

	if (*arch_timer_use == VIRT_PPI) {
		return 1UL;
	} else if (MAX_TIMER_PPI > *arch_timer_use) {
		return 0UL;
	} else {
		return ULONG_MAX;
	}
}
#else
#error "'arch_timer_uses_ppi' address is unknown."
#endif

#endif /* LINUX_VERSION_CODE < KERNEL_VERSION(4,6,0) */

#ifdef IHK_KSYM_arch_timer_rate
u32 *ihk_arch_timer_rate = (u32 *)IHK_KSYM_arch_timer_rate;
#else
#error "'arch_timer_rate' address is unknown."
#endif

/* ----------------------------------------------- */

static unsigned int ihk_start_irq = 0;
module_param(ihk_start_irq, uint, 0644);
MODULE_PARM_DESC(ihk_start_irq, "IHK IKC IPI to be scanned from this IRQ vector");

static unsigned long ihk_trampoline = 0;
module_param(ihk_trampoline, ulong, 0644);
MODULE_PARM_DESC(ihk_trampoline, "IHK trampoline page physical address");

#define D(fmt, ...) \
        printk( "%s(%d) " fmt, __func__, __LINE__, ## __VA_ARGS__ )

static struct page *trampoline_page;
static void *trampoline_va;

static int ident_npages_order = 0;
static unsigned long *ident_page_table_virt;

struct ihk_smp_irq_table ihk_smp_irq;

extern const char ihk_smp_trampoline_end[], ihk_smp_trampoline_data[];
#define IHK_SMP_TRAMPOLINE_SIZE ((unsigned long)(ihk_smp_trampoline_end - ihk_smp_trampoline_data))

static phys_addr_t ihk_smp_gic_dist_base_pa = 0;
static unsigned long ihk_smp_gic_dist_size = 0;
static phys_addr_t ihk_smp_gic_cpu_base_pa = 0;
static unsigned long ihk_smp_gic_cpu_size = 0;
static unsigned int ihk_gic_percpu_offset = 0;
static phys_addr_t ihk_smp_gic_rdist_pa[NR_CPUS];

static void (*ihk___smp_cross_call)(const struct cpumask *, unsigned int);

/*
 * If you edit IPI_XXX, you must edit following together.
 * mckernel/arch/arm64/kernel/include/irq.h::INTRID_xxx
 */
#define INTRID_CPU_STOP  3
#define INTRID_MEMDUMP   7

/* ----------------------------------------------- */

/**
 * @brief GICv2 chip data structs
 * @ref.impl drivers/irqchip/irq-gic.c
 */
union gic_base {
	void __iomem *common_base;
	void __percpu * __iomem *percpu_base;
};

#if (LINUX_VERSION_CODE < KERNEL_VERSION(4,4,0))
struct gic_chip_data_v2 {
	union gic_base dist_base;
	union gic_base cpu_base;
#ifdef CONFIG_CPU_PM
	u32 saved_spi_enable[DIV_ROUND_UP(1020, 32)];
	u32 saved_spi_conf[DIV_ROUND_UP(1020, 16)];
	u32 saved_spi_target[DIV_ROUND_UP(1020, 4)];
	u32 __percpu *saved_ppi_enable;
	u32 __percpu *saved_ppi_conf;
#endif
	struct irq_domain *domain;
	unsigned int gic_irqs;
#ifdef CONFIG_GIC_NON_BANKED
	void __iomem *(*get_base)(union gic_base *);
#endif
};
#elif ((LINUX_VERSION_CODE >= KERNEL_VERSION(4,4,0)) && \
	(LINUX_VERSION_CODE < KERNEL_VERSION(4,7,0)))
struct gic_chip_data_v2 {
	union gic_base dist_base;
	union gic_base cpu_base;
#ifdef CONFIG_CPU_PM
	u32 saved_spi_enable[DIV_ROUND_UP(1020, 32)];
	u32 saved_spi_active[DIV_ROUND_UP(1020, 32)];
	u32 saved_spi_conf[DIV_ROUND_UP(1020, 16)];
	u32 saved_spi_target[DIV_ROUND_UP(1020, 4)];
	u32 __percpu *saved_ppi_enable;
	u32 __percpu *saved_ppi_active;
	u32 __percpu *saved_ppi_conf;
#endif
	struct irq_domain *domain;
	unsigned int gic_irqs;
#ifdef CONFIG_GIC_NON_BANKED
	void __iomem *(*get_base)(union gic_base *);
#endif
};
#else /* LINUX_VERSION_CODE >= KERNEL_VERSION(4,7,0)) */
struct gic_chip_data_v2 {
	struct irq_chip chip;
	union gic_base dist_base;
	union gic_base cpu_base;
	void __iomem *raw_dist_base;
	void __iomem *raw_cpu_base;
	u32 percpu_offset;
#if defined(CONFIG_CPU_PM) || defined(CONFIG_ARM_GIC_PM)
	u32 saved_spi_enable[DIV_ROUND_UP(1020, 32)];
	u32 saved_spi_active[DIV_ROUND_UP(1020, 32)];
	u32 saved_spi_conf[DIV_ROUND_UP(1020, 16)];
	u32 saved_spi_target[DIV_ROUND_UP(1020, 4)];
	u32 __percpu *saved_ppi_enable;
	u32 __percpu *saved_ppi_active;
	u32 __percpu *saved_ppi_conf;
#endif
	struct irq_domain *domain;
	unsigned int gic_irqs;
#ifdef CONFIG_GIC_NON_BANKED
	void __iomem *(*get_base)(union gic_base *);
#endif
};
#endif

/*
 * @ref.impl drivers/irqchip/irq-gic-v3.c
 */
#if (LINUX_VERSION_CODE < KERNEL_VERSION(4,6,0))
struct redist_region {
	void __iomem *redist_base;
	phys_addr_t phys_base;
};
#else
struct redist_region {
	void __iomem		*redist_base;
	phys_addr_t		phys_base;
	bool			single_redist;
};
#endif

#if (LINUX_VERSION_CODE < KERNEL_VERSION(4,7,0))
struct gic_chip_data_v3 {
	void __iomem *dist_base;
	struct redist_region *redist_regions;
	struct rdists rdists;
	struct irq_domain *domain;
	u64 redist_stride;
	u32 nr_redist_regions;
	unsigned int irq_nr;
};
#else
struct gic_chip_data_v3 {
	struct fwnode_handle	*fwnode;
	void __iomem		*dist_base;
	struct redist_region	*redist_regions;
	struct rdists		rdists;
	struct irq_domain	*domain;
	u64			redist_stride;
	u32			nr_redist_regions;
	unsigned int		irq_nr;
	struct partition_desc	*ppi_descs[16];
};
#endif
static unsigned long ihk_gic_version = ACPI_MADT_GIC_VERSION_NONE;
static unsigned int ihk_gic_max_vector = 0;

/*
 * @ref.impl arch/arm64/kernel/psci.c
 */
struct psci_power_state {
	unsigned short id;
	unsigned char type;
	unsigned char affinity_level;
};

#if LINUX_VERSION_CODE < KERNEL_VERSION(4,3,0)
struct psci_operations {
	int (*cpu_suspend)(struct psci_power_state state,
	                   unsigned long entry_point);
	int (*cpu_off)(struct psci_power_state state);
	int (*cpu_on)(unsigned long cpuid, unsigned long entry_point);
	int (*migrate)(unsigned long cpuid);
	int (*affinity_info)(unsigned long target_affinity,
	                     unsigned long lowest_affinity_level);
	int (*migrate_info_type)(void);
};
#endif

struct ihk_smp_irq_table {
	int irq;
	int hwirq;
};

struct ihk_smp_trampoline_header {
	unsigned long reserved;	/* jmp ins. */
	unsigned long page_table;	/* ident page table */
	unsigned long next_ip;	/* the program address */
	unsigned long stack_ptr;	/* stack pointer */
	unsigned long notify_address;	/* notification address */
	unsigned long startup_data;	/* startup_data addr */
	unsigned long st_phys_base;	/* straight map phys base address */
	unsigned long st_phys_size;	/* straight map area size */
	unsigned long dist_base_pa;	/* GIC distributor register base addr */
	unsigned long dist_map_size;	/* GIC distributor register map size */
	unsigned long cpu_base_pa;	/* GIC cpu interface register base addr */
	unsigned long cpu_map_size;	/* GIC cpu interface register map size */
	unsigned int  percpu_offset;	/* GIC cpu interface register map offset value */
	unsigned int  gic_version;	/* GIC version value */
	unsigned long loops_per_jiffy;	/* udelay loops value */
	unsigned long hz;		/* HZ value */
#define PSCI_METHOD_INVALID	-1
#define PSCI_METHOD_HVC		0
#define PSCI_METHOD_SMC		1
	unsigned long psci_method;	/* psci_method value (smc or hvc ?) */
	unsigned long use_virt_timer;	/* use_virt_value */
	unsigned long evtstrm_timer_rate;	/* arch_timer_rate */
	unsigned long cpu_logical_map_size;	/* the cpu-core maximun number */
	unsigned long cpu_logical_map[NR_CPUS];	/* array of the MPIDR and the core number */
	unsigned long rdist_base_pa[NR_CPUS];	/* GIC re-distributor register base addresses */
	int nr_pmu_irq_affiniry;	/* number of pmu affinity list elements */
	int pmu_irq_affiniry[SMP_MAX_CPUS];	/* array of the pmu affinity list */
};

static unsigned long ihk_smp_psci_method = PSCI_METHOD_INVALID;	/* psci_method value */

/* ----------------------------------------------- */

/* @ref.impl arch/arm64/kernel/perf_event.c:armpmu_reserve_hardware */
static int
ihk_armpmu_get_irq_affinity(int irqs[], const struct arm_pmu *armpmu, const struct smp_os_data *os)
{
	const struct smp_boot_param* bp = os->param;
	struct platform_device* pmu_device;
	int hwid, virtid, irq;

	if (!armpmu) {
		return -EINVAL;
	}

	pmu_device = armpmu->plat_device;
	if (!pmu_device) {
		pr_err("no PMU device registered\n");
		return -ENODEV;
	}

	irq = platform_get_irq(pmu_device, 0);
	if (irq <= 0) {
		pr_err("failed to get valid irq for PMU device\n");
		return -ENODEV;
	}

	if (irq_is_percpu(irq)) {
		// TODO[PMU]: ここにくるときはPPIと予想。割込みコアは固定されているはず。
		pr_info("PMU irq is percpu.\n");
		return 0;
	}

	if (!pmu_device->num_resources) {
		pr_err("no irqs for PMUs defined\n");
		return -ENODEV;
	}

	// McKにおける論理CPU番号がインデックスになるように、
	// 物理CPU番号の若い順からirqs変数に格納しておく
	virtid = 0;
	for (hwid = 0; hwid < SMP_MAX_CPUS; hwid++) {
		int irq;

		if (!(CORE_ISSET(hwid, bp->coreset))) {
			continue;
		}

		if (pmu_device->num_resources <= hwid) {
			pr_err("failed to get core number.\n");
			return -ENOENT;
		}

		irq = platform_get_irq(pmu_device, hwid);
		if (irq <= 0) {
			pr_warn("failed to get irq number.\n");
		}
		irqs[virtid++] = irq;
	}
	return virtid;
}

/* @ref.impl arch/arm64/kernel/perf_event.c:armpmu_reserve_hardware */
static int
ihk_armpmu_set_irq_affinity(const int irqs[], const struct smp_os_data *os)
{
	const struct smp_boot_param* bp = os->param;
	int hwid, virtid;

	virtid = 0;
	for (hwid = 0; hwid < SMP_MAX_CPUS; hwid++) {
		int irq;
		if (!(CORE_ISSET(hwid, bp->coreset))) {
			continue;
		}
		irq  = irqs[virtid];

		/*
		 * If we have a single PMU interrupt that we can't shift,
		 * assume that we're running on a uniprocessor machine and
		 * continue. Otherwise, continue without this interrupt.
		 */
		if (ihk___irq_set_affinity(irq, cpumask_of(hwid), true)) {
			pr_warning("unable to set irq affinity (irq=%d, cpu=%u)\n",
				   irq, hwid);
		}
		virtid++;
	}
	return 0;
}

int ihk_smp_get_hw_id(int cpu)
{
	return cpu;
}

#ifdef CONFIG_ACPI
/*
 * @ref.impl drivers/acpi/tables.c:__init acpi_parse_entries
 */
static int ihk_smp_acpi_parse_entries(char *id, unsigned long table_size,
		acpi_tbl_entry_handler handler,
		struct acpi_table_header *table_header,
		int entry_id, unsigned int max_entries)
{
	struct acpi_subtable_header *entry;
	int count = 0;
	unsigned long table_end;

	if (acpi_disabled)
		return -ENODEV;

	if (!id || !handler)
		return -EINVAL;

	if (!table_size)
		return -EINVAL;

	if (!table_header) {
		printk(KERN_WARNING "%4.4s not present\n", id);
		return -ENODEV;
	}

	table_end = (unsigned long)table_header + table_header->length;

	/* Parse all entries looking for a match. */

	entry = (struct acpi_subtable_header *)
	    ((unsigned long)table_header + table_size);

	while (((unsigned long)entry) + sizeof(struct acpi_subtable_header) <
	       table_end) {
		if (entry->type == entry_id
		    && (!max_entries || count < max_entries)) {
			if (handler(entry, table_end))
				return -EINVAL;

			count++;
		}

		/*
		 * If entry->length is 0, break from this loop to avoid
		 * infinite loop.
		 */
		if (entry->length == 0) {
			printk(KERN_ERR "[%4.4s:0x%02x] Invalid zero length\n", id, entry_id);
			return -EINVAL;
		}

		entry = (struct acpi_subtable_header *)
		    ((unsigned long)entry + entry->length);
	}

	if (max_entries && count > max_entries) {
		printk(KERN_WARNING "[%4.4s:0x%02x] ignored %i entries of %i found\n",
			id, entry_id, count - max_entries, count);
	}

	return count;
}

#ifndef ACPI_GICV2_DIST_MEM_SIZE
#define ACPI_GICV2_DIST_MEM_SIZE	SZ_4K
#endif
#ifndef ACPI_GICV2_CPU_IF_MEM_SIZE
#define ACPI_GICV2_CPU_IF_MEM_SIZE	SZ_8K
#endif
#ifndef ACPI_GICV3_DIST_MEM_SIZE
#define ACPI_GICV3_DIST_MEM_SIZE	SZ_64K
#endif
#ifndef ACPI_GICV3_CPU_IF_MEM_SIZE
#define ACPI_GICV3_CPU_IF_MEM_SIZE	SZ_64K
#endif

/*
 * @ref.impl drivers/irqchip/irq-gic.c:__init gic_acpi_parse_madt_cpu
 */
static int ihk_smp_gicv2_acpi_parse_madt_cpu(struct acpi_subtable_header *header,
			const unsigned long end)
{
	struct acpi_madt_generic_interrupt *processor;
	phys_addr_t gic_cpu_base;
	static int cpu_base_assigned;

	processor = (struct acpi_madt_generic_interrupt *)header;
	if (BAD_MADT_GICC_ENTRY(processor, end))
		return -EINVAL;

	/*
	 * There is no support for non-banked GICv1/2 register in ACPI spec.
	 * All CPU interface addresses have to be the same.
	 */
	gic_cpu_base = processor->base_address;
	if (cpu_base_assigned && gic_cpu_base != ihk_smp_gic_cpu_base_pa)
		return -EINVAL;

	// set to global
	ihk_smp_gic_cpu_base_pa = gic_cpu_base;
	ihk_smp_gic_cpu_size = ACPI_GICV2_CPU_IF_MEM_SIZE;
	cpu_base_assigned = 1;
	return 0;
}

static void ihk_smp_gic_collect_rdist(void)
{
	int cpu;

	/* Collect redistributor base addresses for all possible cpus */
	for_each_cpu(cpu, cpu_possible_mask) {
		ihk_smp_gic_rdist_pa[cpu] = 
			(per_cpu_ptr(ihk_gic_data_v3->rdists.rdist, cpu))->phys_base;
	}

	if(ihk_gic_data_v3->redist_stride) {
		ihk_smp_gic_cpu_size = ihk_gic_data_v3->redist_stride;
	} else {
		unsigned long typer = 
			readq_relaxed(((this_cpu_ptr(ihk_gic_data_v3->rdists.rdist))->rd_base) + GICR_TYPER);
		if (typer & GICR_TYPER_VLPIS) {
			ihk_smp_gic_cpu_size = ACPI_GICV3_CPU_IF_MEM_SIZE * 4; /* RD + SGI + VLPI + reserved */
		} else {
			ihk_smp_gic_cpu_size = ACPI_GICV3_CPU_IF_MEM_SIZE * 2; /* RD + SGI */
		}
	}

}

/*
 * Get distributer base PA and gic version.
 *
 * @ref.impl drivers/irqchip/irq-gic.c:
 *		__init gic_acpi_parse_madt_distributor
 *		static bool __init gic_validate_dist
 * @ref.impl drivers/irqchip/irq-gic-v3.c:
 *		__init gic_acpi_parse_madt_distributor
 *		static bool __init acpi_validate_gic_table
 */
static int ihk_smp_acpi_parse_madt_distributor(struct acpi_subtable_header *header,
				const unsigned long end)
{

	struct ihk_acpi_madt_generic_distributor {
		struct acpi_subtable_header header;
		u16 reserved;		/* reserved - must be zero */
		u32 gic_id;
		u64 base_address;
		u32 global_irq_base;
		u8 version;
		u8 reserved2[3];	/* reserved - must be zero */
	};
	struct ihk_acpi_madt_generic_distributor *dist;

	dist = (struct ihk_acpi_madt_generic_distributor *)header;

	if (BAD_MADT_ENTRY(dist, end))
		return -EINVAL;

	// set to global
	ihk_gic_version = dist->version;
	ihk_smp_gic_dist_base_pa = dist->base_address;
	if(ihk_gic_version >= ACPI_MADT_GIC_VERSION_V3) {
		ihk_smp_gic_dist_size = ACPI_GICV3_DIST_MEM_SIZE;
	} else {
		ihk_smp_gic_dist_size = ACPI_GICV2_DIST_MEM_SIZE;
	}

	return 0;
}

/*
 * @ref.impl arch/arm64/kernel/acpi.c:__init acpi_gic_init
 * @ref.impl drivers/irqchip/irq-gic.c:__init gic_v2_acpi_init
 * @ref.impl drivers/irqchip/irq-gic-v3.c:__init gic_v3_acpi_init
 */
static int ihk_smp_acpi_get_gic_base(void)
{
	struct acpi_table_header *table;
	acpi_status status;
	acpi_size tbl_size;
	int count = 0;

	status = acpi_get_table_with_size(ACPI_SIG_MADT, 0, &table, &tbl_size);
	if (ACPI_FAILURE(status)) {
		printk("ERROR: Failed to get MADT table.\n");
		return -ENODATA;
	}

	/*
	 * Find distributor base address. We expect one distributor entry since
	 * ACPI 5.1 spec neither support multi-GIC instances nor GIC cascade.
	 */
	count = ihk_smp_acpi_parse_entries(ACPI_SIG_MADT,
				   sizeof(struct acpi_table_madt),
				   ihk_smp_acpi_parse_madt_distributor, table,
				   ACPI_MADT_TYPE_GENERIC_DISTRIBUTOR, 0);
	if (count < 0) {
		pr_err("Error during GICD entries parsing\n");
		return -EINVAL;
	} else if (!count) {
		pr_err("No valid GICD entries exist\n");
		return -ENXIO;
	} else if (count > 1) {
		pr_err("More than one GICD entry detected\n");
		return -EINVAL;
	}

	if(ihk_gic_version >= ACPI_MADT_GIC_VERSION_V3) {
		// for GICv3 or later
		/* 
		 * Collect re-distributor base PA infomation 
		 * that the host-linux was constructed.
		 */
		ihk_smp_gic_collect_rdist();
	} else {
		// for GICv2 or abobe
		/* Collect CPU base addresses */
		count = ihk_smp_acpi_parse_entries(ACPI_SIG_MADT,
					   sizeof(struct acpi_table_madt),
					   ihk_smp_gicv2_acpi_parse_madt_cpu, table,
					   ACPI_MADT_TYPE_GENERIC_INTERRUPT, 0);
		if (count <= 0) {
			printk("ERROR: No valid GICC entries exist\n");
			return -ENXIO;
		}
	}

	return 0;
}

#else /* CONFIG_ACPI */

static int ihk_smp_acpi_get_gic_base(void)
{
	printk("ERROR: System is really ACPI environment?\n");
	return -EINVAL;
}

#endif /* CONFIG_ACPI */

static int ihk_smp_dt_get_gic_base(void)
{
	int result;
	struct resource res;

	struct irq_domain *domain;
	struct device_node *node;

	if(ihk_gic_data_v3->domain != NULL) {
		ihk_gic_version = 3; /* GICv3 or later */
		domain = ihk_gic_data_v3->domain;
	} else {
		ihk_gic_version = 2; /* GICv2 or abobe */
		domain = ihk_gic_data_v2->domain;
	}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4,4,0)
	node = to_of_node(domain->fwnode);
#else /* LINUX_VERSION_CODE >= KERNEL_VERSION(4,4,0) */
	node = domain->of_node;
#endif /* LINUX_VERSION_CODE >= KERNEL_VERSION(4,4,0) */

	/* get dist base. */
	result = of_address_to_resource(node, 0, &res);
	if (result == 0) {
		ihk_smp_gic_dist_base_pa = res.start;
		ihk_smp_gic_dist_size = ALIGN(res.end - res.start + 1, PAGE_SIZE);
	} else {
		printk("ERROR: GIC failed to get distributor base PA\n");
		return result;
	}

	/* get cpu base. */
	if(ihk_gic_version >= 3) {
		/* 
		 * Collect re-distributor base PA infomation 
		 * that the host-linux was constructed.
		 */
		ihk_smp_gic_collect_rdist();
	} else {
		result = of_address_to_resource(node, 1, &res);
		if (result == 0) {
			ihk_smp_gic_cpu_base_pa = res.start;
			ihk_smp_gic_cpu_size = ALIGN(res.end - res.start + 1, PAGE_SIZE);
#if LINUX_VERSION_CODE < KERNEL_VERSION(4,7,0)
			if (of_property_read_u32(node, "cpu-offset", &ihk_gic_percpu_offset))
				ihk_gic_percpu_offset = 0;
#else /* LINUX_VERSION_CODE < KERNEL_VERSION(4,7,0) */
			ihk_gic_percpu_offset = ihk_gic_data_v2->percpu_offset;
#endif /* LINUX_VERSION_CODE < KERNEL_VERSION(4,7,0) */
		} else {
			printk("ERROR: GIC failed to get cpu I/F base PA\n");
			return result;
		}
	}

	return 0;
}

static int ihk_smp_collect_gic_info(void)
{
	int result = 0;

	if (!acpi_disabled){
		printk("INFO: This is the ACPI environment.\n");
		result = ihk_smp_acpi_get_gic_base();
	}
	else {
		printk("INFO: This is the Device Tree environment.\n");
		result = ihk_smp_dt_get_gic_base();
	}

	if(ihk_gic_version >= ACPI_MADT_GIC_VERSION_V3) {
		ihk___smp_cross_call = ihk___smp_cross_call_gicv3;

		ihk_gic_max_vector = ihk_gic_data_v3->irq_nr;
	} else {
		ihk___smp_cross_call = ihk___smp_cross_call_gicv2;

		ihk_gic_max_vector = ihk_gic_data_v2->gic_irqs;
	}

	return result;
}

static inline int ihk_smp_get_cpu_affinity(int hwid, u64* affi)
{
	if (hwid < 0 || ihk___cpu_logical_map_size <= hwid) {
		printk("IHK-SMP: invalid hwid = %d\n", hwid);
		return -EINVAL;
	}
	if (affi == NULL) {
		return -EINVAL;
	}
	*affi = ihk___cpu_logical_map[hwid];
	return 0;
}

int smp_wakeup_secondary_cpu(int hw_id, unsigned long start_eip)
{
	int ret;
	u64 affi;

	ret = ihk_smp_get_cpu_affinity(hw_id, &affi);
	if (ret) {
		return ret;
	}
	D("ihk_psci_ops->cpu_on[%p] (0x%llx, 0x%lx)\n",
	  ihk_psci_ops->cpu_on, affi, start_eip);
	return ihk_psci_ops->cpu_on(affi, start_eip);
}

#ifdef POSTK_DEBUG_ARCH_DEP_29
unsigned long calc_ns_per_tsc(void)
{
	unsigned int freq;

	asm volatile(
"	mrs	%0, cntfrq_el0\n"
	: "=r" (freq)
	:
	: "memory");

	return 1000000000000L / freq;
}
#endif	/* POSTK_DEBUG_ARCH_DEP_29 */

void smp_ihk_setup_trampoline(void *priv)
{
	struct smp_os_data *os = priv;
	struct ihk_smp_trampoline_header *header;
	int nr_irqs;

	os->param->ihk_ikc_irq = (unsigned int)ihk_smp_irq.hwirq & 0x00000000ffffffffUL;
	os->param->ihk_ikc_irq_apicid = 0;

	/* Prepare trampoline code */
	memcpy(trampoline_va, ihk_smp_trampoline_data,
	       IHK_SMP_TRAMPOLINE_SIZE);
	D("trampoline=0x%llx, trampoline_va=0x%lx\n", __pa(trampoline_va), (unsigned long)trampoline_va);

	header = trampoline_va;
	header->page_table = ident_page_table;
	header->next_ip = os->boot_rip;
	header->notify_address = __pa(os->param);
	header->st_phys_base = virt_to_phys((void*)PAGE_OFFSET);
	header->st_phys_size = (unsigned long)high_memory - PAGE_OFFSET;
	header->dist_base_pa = ihk_smp_gic_dist_base_pa;
	header->dist_map_size = ihk_smp_gic_dist_size;
	header->cpu_base_pa = ihk_smp_gic_cpu_base_pa;
	header->cpu_map_size = ihk_smp_gic_cpu_size;
	header->percpu_offset = ihk_gic_percpu_offset;
	header->gic_version = ihk_gic_version;
	header->loops_per_jiffy = loops_per_jiffy;
	header->hz = HZ;
	header->psci_method = ihk_smp_psci_method;
	header->use_virt_timer = is_arch_timer_use_virt();
	header->evtstrm_timer_rate = (unsigned long)*ihk_arch_timer_rate;
	header->cpu_logical_map_size = sizeof(header->cpu_logical_map) / sizeof(unsigned long);
	memcpy(header->cpu_logical_map, ihk___cpu_logical_map,
		header->cpu_logical_map_size * sizeof(unsigned long));
	memcpy(header->rdist_base_pa, ihk_smp_gic_rdist_pa,
		header->cpu_logical_map_size * sizeof(unsigned long));

	nr_irqs = ihk_armpmu_get_irq_affinity(header->pmu_irq_affiniry, *ihk_cpu_pmu, os);
	if (nr_irqs < 0) {
		header->nr_pmu_irq_affiniry = 0;
		return;
	}
	header->nr_pmu_irq_affiniry = nr_irqs;
	// TODO[PMU]: McKernel側でコアが起きた後にaffinity設定しないと駄目なら、ここでの設定は止める。
	ihk_armpmu_set_irq_affinity(header->pmu_irq_affiniry, os);
}

unsigned long smp_ihk_adjust_entry(unsigned long entry,
                                          unsigned long phys)
{
	entry = entry - (IHK_SMP_MAP_KERNEL_START - phys);
	D("IHK-SMP: phys = 0x%lx, entry_va=0x%lx, entry_pa=0x%lx\n",
	  phys, (unsigned long)phys_to_virt(entry), entry);

	return entry;
}

void smp_ihk_os_setup_startup(void *priv, unsigned long phys,
                            unsigned long entry)
{
	struct smp_os_data *os = priv;
	extern char startup_data[];
	extern char startup_data_end[];
	unsigned long startup_p;
	unsigned long *startup;

	startup_p = os->mem_end - (2 << IHK_SMP_LARGE_PAGE_SHIFT);
	D("startup_p=0x%lx\n", startup_p);
	startup = ihk_smp_map_virtual(startup_p, PAGE_SIZE);
	memcpy(startup, startup_data, startup_data_end - startup_data);
//	startup[2] = pml4_p;
	startup[3] = 0xffffffffc0000000;
	startup[4] = phys;
	startup[5] = trampoline_phys;
	startup[6] = entry;
	ihk_smp_unmap_virtual(startup);
	os->boot_rip = startup_p;
}

enum ihk_os_status smp_ihk_os_query_status(ihk_os_t ihk_os, void *priv)
{
	struct smp_os_data *os = priv;
	int status;

	status = os->status;

	if (status == BUILTIN_OS_STATUS_BOOTING) {
		if (os->param->status == 1) {
			return IHK_OS_STATUS_BOOTED;
		} else if(os->param->status == 2) {
			return IHK_OS_STATUS_READY;
		} else {
			return IHK_OS_STATUS_BOOTING;
		}
	} else {
		return IHK_OS_STATUS_NOT_BOOTED;
	}
}

void ihk_smp_memdump_cpu(int hw_id)
{
	int i;

	for (i = 0; i < SMP_MAX_CPUS; i++) {
		if ((ihk_smp_cpus[i].hw_id != hw_id) ||
		    (ihk_smp_cpus[i].status != IHK_SMP_CPU_ASSIGNED))
			continue;

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4,1,0)
		ihk___smp_cross_call(cpumask_of(hw_id), INTRID_MEMDUMP);
#else
		ihk___smp_cross_call(&cpumask_of_cpu(hw_id),
		                     INTRID_MEMDUMP);
#endif
		break;
	}
}

int smp_ihk_os_dump(ihk_os_t ihk_os, void *priv, dumpargs_t *args)
{
	if (0) printk("mcosdump: cmd %d start %lx size %lx buf %p\n",
			args->cmd, args->start, args->size, args->buf);

	if (args->cmd == DUMP_NMI) {
		int i;

		for (i = 0; i < SMP_MAX_CPUS; ++i) {
			if (ihk_smp_cpus[i].os != ihk_os)
				continue;

			ihk_smp_memdump_cpu(ihk_smp_cpus[i].hw_id);
		}
		return 0;
	}

	if (args->cmd == DUMP_QUERY) {
		int i = 0;
		struct ihk_os_mem_chunk *os_mem_chunk;
		dump_mem_chunks_t *mem_chunks = args->buf;
		extern struct list_head ihk_mem_used_chunks;

		/* Collect memory information */
		list_for_each_entry(os_mem_chunk, &ihk_mem_used_chunks, list) {
			if (os_mem_chunk->os != ihk_os)
				continue;

			mem_chunks->chunks[i].addr = os_mem_chunk->addr;
			mem_chunks->chunks[i].size = os_mem_chunk->size;
			++i;
		}

		mem_chunks->nr_chunks = i;

		return 0;
	}

	if (args->cmd == DUMP_READ) {
		void *va;

		va = phys_to_virt(args->start);
		if (copy_to_user(args->buf, va, args->size)) {
			return -EFAULT;
		}
		return 0;
	}

	return -EINVAL;
}

int smp_ihk_os_issue_interrupt(ihk_os_t ihk_os, void *priv,
                               int cpu, int v)
{
	struct smp_os_data *os = priv;

	/* better calcuation or make map */
	if (cpu < 0 || cpu >= os->cpu_info.n_cpus) {
		return -EINVAL;
	}
//	printk("smp_ihk_os_issue_interrupt(): %d\n", os->cpu_info.hw_ids[cpu]);

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4,1,0)
	ihk___smp_cross_call(cpumask_of(os->cpu_info.hw_ids[cpu]), v);
#else
	ihk___smp_cross_call(&cpumask_of_cpu(os->cpu_info.hw_ids[cpu]), v);
#endif

	return -EINVAL;
}

unsigned long smp_ihk_os_map_memory(ihk_os_t ihk_os, void *priv,
                                    unsigned long remote_phys,
                                    unsigned long size)
{
	/* We use the same physical memory. So no need to do something */
	return remote_phys;
}

int smp_ihk_os_unmap_memory(ihk_os_t ihk_os, void *priv,
                            unsigned long local_phys,
                            unsigned long size)
{
	return 0;
}

/** \brief Map a remote physical memory to the local physical memory.
 *
 * In BUILTIN, all the kernels including the host kernel are running in the
 * same physical memory map, thus there is nothing to do. */
unsigned long smp_ihk_map_memory(ihk_device_t ihk_dev, void *priv,
                                 unsigned long remote_phys,
                                 unsigned long size)
{
	/* We use the same physical memory. So no need to do something */
	return remote_phys;
}

int smp_ihk_unmap_memory(ihk_device_t ihk_dev, void *priv,
                         unsigned long local_phys,
                         unsigned long size)
{
	return 0;
}

static irqreturn_t smp_ihk_irq_handler(int irq, void *dev_id)
{
	/* temporary fix1 */
	if(ihk_smp_irq.irq != irq) {
		return IRQ_NONE;
	}
	/* temporary fix1 */

	smp_ihk_irq_call_handlers(ihk_smp_irq.hwirq, NULL);

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4,3,0))
	/* temporary fix2 */
	irq_set_irqchip_state(irq, IRQCHIP_STATE_PENDING, false);
	/* temporary fix2 */
#endif

	return IRQ_HANDLED;
}

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4,1,0))
#define IRQF_DISABLED 0x0
#endif

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4,4,0)
static int ihk_smp_reserve_irq(void)
{
	unsigned int virq, hwirq;
	struct irq_domain *domain;
	struct irq_fwspec fw_args;
	struct irq_desc *desc;

	if (ihk_start_irq < 32){
		hwirq = 32; // base of SPI.
	} else if (ihk_start_irq > ihk_gic_max_vector) {
		hwirq = ihk_gic_max_vector - 1;
	} else {
		hwirq = ihk_start_irq;
	}

	if(ihk_gic_version >= ACPI_MADT_GIC_VERSION_V3) {
		domain = ihk_gic_data_v3->domain;
	} else {
		domain = ihk_gic_data_v2->domain;
	}

	for( ; hwirq < ihk_gic_max_vector; hwirq += 1 ) {
		// check hwirq is in used?
		virq = irq_find_mapping(domain, hwirq);
		if (virq == 0) {
			break;
		}
	}

	if(hwirq == ihk_gic_max_vector) {
		printk("IRQ vector : There is no blank irq\n");
		return -ENOENT;
	}

	if (is_of_node(domain->fwnode)) {
		fw_args.fwnode = domain->fwnode;
		fw_args.param_count = 3;
		fw_args.param[0] = GIC_SPI;
		fw_args.param[1] = hwirq - 32;
		fw_args.param[2] = IRQ_TYPE_LEVEL_HIGH;
	} else if (is_fwnode_irqchip(domain->fwnode)) {
		fw_args.fwnode = domain->fwnode;
		fw_args.param_count = 2;
		fw_args.param[0] = hwirq;
		fw_args.param[1] = IRQ_TYPE_LEVEL_HIGH;
	}
	virq = irq_create_fwspec_mapping(&fw_args);

	desc = _irq_to_desc(virq);
	if (!desc) {
		printk("IRQ vector %d: still no descriptor??\n", virq);
		return -EINVAL;
	}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(3,2,0)
	if (desc->status_use_accessors & IRQ_NOREQUEST) {
		printk("IRQ vector %d: not allowed to request, fake it\n",
		       virq);

		desc->status_use_accessors &= ~IRQ_NOREQUEST;
	}
#else
	if (desc->status & IRQ_NOREQUEST) {
		printk("IRQ vector %d: not allowed to request, fake it\n",
		       virq);

		desc->status &= ~IRQ_NOREQUEST;
	}
#endif

	if (request_irq(virq, smp_ihk_irq_handler,
		IRQF_DISABLED, "IHK-SMP", NULL) != 0) {
		printk(KERN_INFO "IHK-SMP: IRQ vector %d: request_irq failed\n", virq);
		return -EFAULT;
	}

	ihk_smp_irq.irq = virq;
	ihk_smp_irq.hwirq = (u32)(irq_desc_get_irq_data(desc)->hwirq);
	printk("IHK-SMP: IKC irq vector: %d, hwirq#: %d\n", 
		ihk_smp_irq.irq, ihk_smp_irq.hwirq);

	return virq;
}
#endif /* LINUX_VERSION_CODE >= KERNEL_VERSION(4,4,0) */

static int collect_topology(void);
int smp_ihk_arch_init(void)
{
	int error;
#if LINUX_VERSION_CODE < KERNEL_VERSION(4,4,0)
	unsigned int vector;
#endif /* LINUX_VERSION_CODE < KERNEL_VERSION(4,4,0) */

	/* psci_method check */
	if (ihk_invoke_psci_fn) {
		if (*ihk_invoke_psci_fn == ihk___invoke_psci_fn_smc) {
			ihk_smp_psci_method = PSCI_METHOD_SMC;
		} else if (*ihk_invoke_psci_fn == ihk___invoke_psci_fn_hvc) {
			ihk_smp_psci_method = PSCI_METHOD_HVC;
		}
	}

	if (ihk_smp_psci_method == PSCI_METHOD_INVALID) {
		printk("IHK-SMP: error: psci_method neither SMC nor HVC\n");
		return EFAULT;
	}

	if (ihk_trampoline) {
		printk("IHK-SMP: preallocated trampoline phys: 0x%lx\n",
		       ihk_trampoline);

		trampoline_phys = ihk_trampoline;
		trampoline_va = ioremap_cache(trampoline_phys, PAGE_SIZE);

	}
	else {
#define TRAMP_ATTEMPTS  20
		int attempts = 0;
		int order;
		struct page *bad_pages[TRAMP_ATTEMPTS];

		memset(bad_pages, 0, TRAMP_ATTEMPTS * sizeof(struct page *));
		order = get_order(IHK_SMP_TRAMPOLINE_SIZE);
retry_trampoline:
		trampoline_page = alloc_pages(GFP_DMA | GFP_KERNEL, order);
		
		if (!trampoline_page) {
			bad_pages[attempts] = trampoline_page;
			
			if (++attempts < TRAMP_ATTEMPTS) {
				printk("IHK-SMP: warning: retrying trampoline_code allocation\n");
				goto retry_trampoline;
			}
			
			printk("IHK-SMP: error: allocating trampoline_code\n");
			return -EFAULT;
		}
		
		/* Free failed attempts.. */
		for (attempts = 0; attempts < TRAMP_ATTEMPTS; ++attempts) {
			if (!bad_pages[attempts]) {
				continue;
			}
			
			free_pages((unsigned long)pfn_to_kaddr(page_to_pfn(bad_pages[attempts])),
			           order);
		}
		
		trampoline_phys = page_to_phys(trampoline_page);
		trampoline_va =
			pfn_to_kaddr(page_to_pfn(trampoline_page));
		
		printk(KERN_INFO "IHK-SMP: trampoline_page phys: 0x%lx\n", trampoline_phys);
	}
	
	{
		/* Get GIC register base physical address */
		int result = ihk_smp_collect_gic_info();
		if(result != 0 || ihk_gic_max_vector <= 0) {
			printk("IHK-SMP: error: get GIC base pa\n");
			return -EINVAL;
		}
	}
	
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4,4,0)
	/* do request_irq for IKC */
	error = ihk_smp_reserve_irq();
	if (error <= 0) {
		printk("IHK-SMP: error: request IRQ faild.\n");
		return error;
	}
#else /* LINUX_VERSION_CODE >= KERNEL_VERSION(4,4,0) */
	/* Find a suitable IRQ vector */
	for (vector = ihk_start_irq ? ihk_start_irq : ihk_gic_max_vector/3;
		vector < ihk_gic_max_vector; vector += 1) {
		struct irq_desc *desc;

#ifdef CONFIG_SPARSE_IRQ
		desc = _irq_to_desc(vector);
		if (!desc) {
			int result;
			struct irq_domain *domain;

			struct of_phandle_args of_args;
			if(ihk_gic_version >= ACPI_MADT_GIC_VERSION_V3) {
				domain = ihk_gic_data_v3->domain;
			} else {
				domain = ihk_gic_data_v2->domain;
			}

			of_args.np = domain->of_node;
			of_args.args[0] = GIC_SPI;
			of_args.args[1] = vector;
			of_args.args[2] = IRQ_TYPE_LEVEL_HIGH;
			of_args.args_count = 3;

			result = ihk___irq_domain_alloc_irqs(
					domain,			/* struct irq_domain *domain */
					vector,			/* int irq_base */
					1,			/*  unsigned int nr_irqs */
					-1,			/* int node */
					(void *)&of_args,	/* void *arg */
					false);			/* bool realloc */

			if (result <= 0){
				printk("IRQ vector %d: irq_domain_alloc_irqs failed.(%d)\n", vector, result);
				continue;
			}
			vector = result;
		} else {
			printk("IRQ vector %d: has descriptor\n", vector);
			continue;
		}

		desc = _irq_to_desc(vector);
		if (!desc) {
			printk(KERN_INFO "IHK-SMP: IRQ vector %d: failed allocating descriptor\n", vector);
			continue;
		}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(3,2,0)
		if (desc->status_use_accessors & IRQ_NOREQUEST) {
			printk(KERN_INFO "IHK-SMP: IRQ vector %d: not allowed to request, fake it\n", vector);

			desc->status_use_accessors &= ~IRQ_NOREQUEST;
		}
#else
		if (desc->status & IRQ_NOREQUEST) {
			printk(KERN_INFO "IHK-SMP: IRQ vector %d: not allowed to request, fake it\n", vector);

			desc->status &= ~IRQ_NOREQUEST;
		}
#endif
#endif /* CONFIG_SPARSE_IRQ */

		if (request_irq(vector, smp_ihk_irq_handler, 
			IRQF_DISABLED, "IHK-SMP", NULL) != 0) {
			printk(KERN_INFO "IHK-SMP: IRQ vector %d: request_irq failed\n", vector);

			irq_free_descs(vector, 1);
			continue;
		}

		/* get HwIRQ# through desc->irq_data */
		ihk_smp_irq.hwirq = (u32)(irq_desc_get_irq_data(desc)->hwirq);
		break;
	}

	if (vector >= ihk_gic_max_vector) {
		printk("IHK-SMP: error: allocating IKC irq vector\n");
		error = EFAULT;
		goto error_free_trampoline;
	}

	printk("IHK-SMP: IKC irq vector: %d, hwirq#: %d\n", vector,
	       ihk_smp_irq.hwirq);
	ihk_smp_irq.irq = vector;
#endif /* LINUX_VERSION_CODE >= KERNEL_VERSION(4,4,0) */

	error = collect_topology();
	if (error) {
		goto error_free_irq;
	}

	return error;

error_free_irq:
	free_irq(ihk_smp_irq.irq, NULL);

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4,4,0)
#else /* LINUX_VERSION_CODE >= KERNEL_VERSION(4,4,0) */
error_free_trampoline:
	if (trampoline_page) {
		free_pages((unsigned long)pfn_to_kaddr(page_to_pfn(trampoline_page)), 1);
	}
#endif /* LINUX_VERSION_CODE >= KERNEL_VERSION(4,4,0) */
	return error;
}

/*
 * @ref.impl arch/arm64/kernel/psci.c
 */
static int ihk_smp_cpu_kill(unsigned int hwid)
{
	int ret, err, i;
	u64 affi;

	if (!ihk_psci_ops->affinity_info) {
		pr_warn("IHK-SMP: Undefined reference to 'affinity_info'\n");
	return -EFAULT;
	}

	/* cpu_kill could race with cpu_die and we can
	 * potentially end up declaring this cpu undead
	 * while it is dying. So, try again a few times. */
	ret = ihk_smp_get_cpu_affinity(hwid, &affi);
	if (ret) {
		return ret;
	}

	for (i = 0; i < 10; i++) {
		err = ihk_psci_ops->affinity_info(affi, 0);
		if (err == PSCI_0_2_AFFINITY_LEVEL_OFF) {
			pr_info("IHK-SMP: CPU HWID %d killed.\n", hwid);
			return 0;
		}

		msleep(10);
		pr_info("IHK-SMP: Retrying again to check for CPU kill\n");
	}

	pr_warn("IHK-SMP: CPU HWID %d may not have shut down cleanly (AFFINITY_INFO reports %d)\n",
	        hwid, err);

	/* Make op_cpu_kill() fail. */
	return -ETIMEDOUT;
}

LIST_HEAD(cpu_topology_list);
LIST_HEAD(node_topology_list);

static int ensure_continue(const char *errmsg, int result)
{
	int is_error = 0;

	switch (result) {
		case 0:
			// no problem.
			break;
		case -ENOENT:
			// When target file does not exist, 
			// processes may be continued.
			break;
		default:
			is_error = 1;
			printk(errmsg, result);
			break;
	}
	return is_error;
}

static int collect_cache_topology(struct ihk_cpu_topology *cpu_topo, int index)
{
	int error;
	char *prefix = NULL;
	int n;
	struct ihk_cache_topology *p = NULL;

	dprintk("collect_cache_topology(%p,%d)\n", cpu_topo, index);
	prefix = kmalloc(PATH_MAX, GFP_KERNEL);
	if (!prefix) {
		error = -ENOMEM;
		eprintk("ihk:collect_cache_topology:"
				"kmalloc failed. %d\n", error);
		goto out;
	}

	n = snprintf(prefix, PATH_MAX,
			"/sys/devices/system/cpu/cpu%d/cache/index%d",
			cpu_topo->cpu_number, index);
	if (n >= PATH_MAX) {
		error = -ENAMETOOLONG;
		eprintk("ihk:collect_cache_topology:"
				"snprintf failed. %d\n", error);
		goto out;
	}

	if (!file_readable("%s/level", prefix)) {
		/* File doesn't exist, it's not an error */
		error = 0;
		goto out;
	}

	p = kzalloc(sizeof(*p), GFP_KERNEL);
	if (!p) {
		error = -ENOMEM;
		eprintk("ihk:collect_cache_topology:"
				"kzalloc failed. %d\n", error);
		goto out;
	}

	p->index = index;

	error = read_long(&p->level, "%s/level", prefix);
	if(ensure_continue(KERN_ERR "ihk:collect_cache_topology:"
				"read_long(level) failed. %d\n", error)) {
		goto out;
	}

	error = read_string(&p->type, "%s/type", prefix);
	if(ensure_continue(KERN_ERR "ihk:collect_cache_topology:"
				"read_string(type) failed. %d\n", error)) {
		goto out;
	}

	error = read_long(&p->size, "%s/size", prefix);
	if(ensure_continue(KERN_ERR "ihk:collect_cache_topology:"
				"read_long(size) failed. %d\n", error)) {
		goto out;
	}
	p->size *= 1024;	/* XXX */

	error = read_string(&p->size_str, "%s/size", prefix);
	if(ensure_continue(KERN_ERR "ihk:collect_cache_topology:"
				"read_string(size) failed. %d\n", error)) {
		goto out;
	}

	error = read_long(&p->coherency_line_size,
			"%s/coherency_line_size", prefix);
	if(ensure_continue(KERN_ERR "ihk:collect_cache_topology:"
				"read_long(coherency_line_size) failed. %d\n",
				error)) {
		goto out;
	}

	error = read_long(&p->number_of_sets, "%s/number_of_sets", prefix);
	if(ensure_continue(KERN_ERR "ihk:collect_cache_topology:"
				"read_long(number_of_sets) failed. %d\n",
				error)) {
		goto out;
	}

	error = read_long(&p->ways_of_associativity,
			"%s/ways_of_associativity", prefix);
	if(ensure_continue(KERN_ERR "ihk:collect_cache_topology:"
				"read_long(ways_of_associativity) failed."
				" %d\n", error)) {
		goto out;
	}

	error = read_bitmap(&p->shared_cpu_map, nr_cpumask_bits,
			"%s/shared_cpu_map", prefix);
	if(ensure_continue(KERN_ERR "ihk:collect_cache_topology:"
				"read_bitmap(shared_cpu_map) failed. %d\n",
				error)) {
		goto out;
	}

	error = 0;
	list_add(&p->chain, &cpu_topo->cache_topology_list);
	p = NULL;

out:
	if (p) {
		kfree(p->type);
		kfree(p->size_str);
		kfree(p);
	}
	kfree(prefix);
	dprintk("collect_cache_topology(%p,%d): %d\n", cpu_topo, index, error);
	return error;
} /* collect_cache_topology() */

static int collect_cpu_topology(int cpu)
{
	int error;
	char *prefix = NULL;
	int n;
	struct ihk_cpu_topology *p = NULL;
	int index;

	dprintk("collect_cpu_topology(%d)\n", cpu);
	prefix = kmalloc(PATH_MAX, GFP_KERNEL);
	if (!prefix) {
		error = -ENOMEM;
		eprintk("ihk:collect_cpu_topology:"
				"kmalloc failed. %d\n", error);
		goto out;
	}

	n = snprintf(prefix, PATH_MAX, "/sys/devices/system/cpu/cpu%d", cpu);
	if (n >= PATH_MAX) {
		error = -ENAMETOOLONG;
		eprintk("ihk:collect_cpu_topology:"
				"snprintf failed. %d\n", error);
		goto out;
	}

	p = kzalloc(sizeof(*p), GFP_KERNEL);
	if (!p) {
		error = -ENOMEM;
		eprintk("ihk:collect_cpu_topology:"
				"kzalloc failed. %d\n", error);
		goto out;
	}

	INIT_LIST_HEAD(&p->cache_topology_list);
	p->cpu_number = cpu;
	p->hw_id = cpu;

	error = read_long(&p->core_id, "%s/topology/core_id", prefix);
	if(ensure_continue(KERN_ERR "ihk:collect_cpu_info:"
				"read_long(core_id) failed. %d\n", error)) {
		goto out;
	}

	error = read_bitmap(&p->core_siblings, nr_cpumask_bits,
			"%s/topology/core_siblings", prefix);
	if(ensure_continue(KERN_ERR "ihk:collect_cpu_info:"
				"read_bitmap(core_siblings) failed. %d\n",
				error)) {
		goto out;
	}

	error = read_long(&p->physical_package_id,
			"%s/topology/physical_package_id", prefix);
	if(ensure_continue(KERN_ERR "ihk:collect_cpu_info:"
				"read_long(physical_package_id) failed. %d\n",
				error)) {
		goto out;
	}

	error = read_bitmap(&p->thread_siblings, nr_cpumask_bits,
			"%s/topology/thread_siblings", prefix);
	if(ensure_continue(KERN_ERR "ihk:collect_cpu_info:"
				"read_bitmap(thread_siblings) failed. %d\n",
				error)) {
		goto out;
	}

	for (index = 0; index < 10; ++index) {
		error = collect_cache_topology(p, index);
		if (error) {
			dprintk("collect_cpu_info:"
					"collect_cache_topology(%d) failed."
					" %d\n", index, error);
			break;
		}
	}

	error = 0;
	list_add(&p->chain, &cpu_topology_list);
	p = NULL;

out:
	kfree(p);
	kfree(prefix);
	dprintk("collect_cpu_topology(%d): %d\n", cpu, error);
	return error;
} /* collect_cpu_topology() */

static int collect_node_topology(int node)
{
	int error;
	struct ihk_node_topology *p = NULL;

	dprintk("collect_node_topology(%d)\n", node);
	p = kzalloc(sizeof(*p), GFP_KERNEL);
	if (!p) {
		error = -ENOMEM;
		eprintk("ihk:collect_node_topology:"
				"kzalloc failed. %d\n", error);
		goto out;
	}

	p->node_number = node;

	error = read_bitmap(&p->cpumap, nr_cpumask_bits,
			"/sys/devices/system/node/node%d/cpumap", node);
	if (error) {
		eprintk("ihk:collect_node_topology:"
				"read_bitmap failed. %d\n", error);
		// goto out;
	}

	error = 0;
	list_add(&p->chain, &node_topology_list);
	p = NULL;

out:
	kfree(p);
	dprintk("collect_node_topology(%d): %d\n", node, error);
	return error;
} /* collect_node_topology() */

int collect_topology(void)
{
	int error;
	int cpu;
	int node;

	dprintk("collect_topology()\n");
	for_each_cpu(cpu, cpu_online_mask) {
		error = collect_cpu_topology(cpu);
		if (error) {
			eprintk("ihk:collect_topology:"
					"collect_cpu_topology failed. %d\n",
					error);
			goto out;
		}
	}

	for_each_online_node(node) {
		error = collect_node_topology(node);
		if (error) {
			eprintk("ihk:collect_topology:"
					"collect_node_topology failed. %d\n",
					error);
			goto out;
		}
	}

	error = 0;
out:
	dprintk("collect_topology(): %d\n", error);
	return error;
} /* collect_topology() */

int ihk_smp_reset_cpu(int hw_id)
{
	int ret = 0;
	int i;

	printk(KERN_INFO "IHK-SMP: resetting CPU %d.\n", hw_id);

	for (i = 0; i < SMP_MAX_CPUS; i++) {
		if ((ihk_smp_cpus[i].hw_id != hw_id) ||
		    (ihk_smp_cpus[i].status != IHK_SMP_CPU_ASSIGNED))
			continue;

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4,1,0)
		ihk___smp_cross_call(cpumask_of(hw_id), INTRID_CPU_STOP);
#else
		ihk___smp_cross_call(&cpumask_of_cpu(hw_id), INTRID_CPU_STOP);
#endif
		ret = ihk_smp_cpu_kill(hw_id);
		break;
	}

	return ret;
}

void smp_ihk_arch_exit(void)
{
#if ((LINUX_VERSION_CODE >= KERNEL_VERSION(3,0,0)) && \
	(LINUX_VERSION_CODE <= KERNEL_VERSION(4,3,0)))
	if (this_module_put) {
		try_module_get(THIS_MODULE);
	}
#endif
	free_irq(ihk_smp_irq.irq, NULL);

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4,4,0)
	irq_dispose_mapping(ihk_smp_irq.irq);
#else
#ifdef CONFIG_SPARSE_IRQ
	ihk_irq_domain_free_irqs(ihk_smp_irq.irq, 1);
#endif
#endif

	if (trampoline_page) {
		int order = get_order(IHK_SMP_TRAMPOLINE_SIZE);
		free_pages((unsigned long)pfn_to_kaddr(page_to_pfn(trampoline_page)),
		           order);
	}
	else {
		iounmap(trampoline_va);
	}
	
	if (ident_npages_order) {
		free_pages((unsigned long)ident_page_table_virt,
		           ident_npages_order);
	}
}
