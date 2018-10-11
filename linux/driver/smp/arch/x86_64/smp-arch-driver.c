/* smp-arch-driver.c COPYRIGHT FUJITSU LIMITED 2015-2016 */
/**
 * \file smp-x86-driver.c
 * \brief
 *	IHK SMP-x86 Driver: IHK Host Driver
 *                        for partitioning an x86 SMP chip
 * \author Balazs Gerofi <bgerofi@is.s.u-tokyo.ac.jp> \par
 * Copyright (C) 2014 Balazs Gerofi <bgerofi@is.s.u-tokyo.ac.jp>
 *
 * Code partially based on IHK Builtin driver written by
 * Taku SHIMOSAWA <shimosawa@is.s.u-tokyo.ac.jp>
 */
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/mm.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/radix-tree.h>
#include <linux/irq.h>
#include <linux/vmalloc.h>
#include <asm/hw_irq.h>
#include <linux/version.h>
#include <linux/kallsyms.h>
#include <asm/mc146818rtc.h>
#include <asm/tlbflush.h>
#if defined(RHEL_RELEASE_CODE) || (LINUX_VERSION_CODE < KERNEL_VERSION(4,0,0))
#include <asm/smpboot_hooks.h>
#endif

#include <asm/realmode.h>
#include <asm/apic.h>
#include <asm/ipi.h>
#include <asm/uv/uv.h>
#include <asm/nmi.h>
#include <ihk/ihk_host_driver.h>
#include <ihk/ihk_host_user.h>
#include <ihk/misc/debug.h>
#include "config.h"
#include "smp-driver.h"
#include "smp-arch-driver.h"
#include "smp-defines-driver.h"

/* ----------------------------------------------- */

static unsigned int ihk_start_irq = 0;
module_param(ihk_start_irq, uint, 0644);
MODULE_PARM_DESC(ihk_start_irq, "IHK IKC IPI to be scanned from this IRQ vector");

static unsigned int ihk_ikc_irq_core = 0;
module_param(ihk_ikc_irq_core, uint, 0644);
MODULE_PARM_DESC(ihk_ikc_irq_core, "Target CPU of IHK IKC IRQ");

static unsigned long ihk_trampoline = 0;
module_param(ihk_trampoline, ulong, 0644);
MODULE_PARM_DESC(ihk_trampoline, "IHK trampoline page physical address");

#define IHK_SMP_MAP_ST_START		0xffff800000000000UL

#define PTL4_SHIFT	39
#define PTL3_SHIFT	30
#define PTL2_SHIFT	21

static struct page *trampoline_page;
static int using_linux_trampoline = 0;
static char linux_trampoline_backup[4096];
static void *trampoline_va;

static int ident_npages_order = 0;
static unsigned long *ident_page_table_virt;

static int ihk_smp_irq = 0;
static int ihk_smp_irq_apicid = 0;

extern const char ihk_smp_trampoline_end[], ihk_smp_trampoline_data[];
#define IHK_SMP_TRAMPOLINE_SIZE \
        roundup(ihk_smp_trampoline_end - ihk_smp_trampoline_data, PAGE_SIZE)

/* ----------------------------------------------- */

struct ihk_smp_trampoline_header {
	unsigned long reserved;		/* jmp ins. */
	unsigned long page_table;	/* ident page table */
	unsigned long next_ip;		/* the program address */
	unsigned long stack_ptr;	/* stack pointer */
	unsigned long notify_address;	/* notification address */
};

/* ----------------------------------------------- */


/*
 * IHK-SMP unexported kernel symbols
 */

static struct real_mode_header *_real_mode_header;
static void *_vector_irq;
static int (*_lapic_get_maxlvt)(void);
static struct radix_tree_root *_irq_desc_tree;
static enum uv_system_type (*_get_uv_system_type)(void);

#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 3, 0)
static atomic_t *_init_deasserted;
#endif

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 8, 0) || \
	(defined(RHEL_RELEASE_CODE) && RHEL_RELEASE_CODE >= RHEL_RELEASE_VERSION(7, 5))
static struct irq_desc *(*_alloc_desc)(int irq, int node, unsigned int flags,
				       const struct cpumask *affinity,
				       struct module *owner);
#else
static struct irq_desc *(*_alloc_desc)(int irq, int node,
				       struct module *owner);
#endif

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 6, 0)
static void (*___default_send_IPI_dest_field)(unsigned int mask, int vector,
					      unsigned int dest);
#endif

struct rb_root *_vmap_area_root;
spinlock_t *_vmap_area_lock;
static void (*___insert_vmap_area)(struct vmap_area *va);
static void (*___free_vmap_area)(struct vmap_area *va);


int ihk_smp_arch_symbols_init(void)
{
	_real_mode_header = (void *) kallsyms_lookup_name("real_mode_header");
	if (WARN_ON(!_real_mode_header))
		return -EFAULT;

	_vector_irq = (void *) kallsyms_lookup_name("vector_irq");
	if (WARN_ON(!_vector_irq))
		return -EFAULT;

	_lapic_get_maxlvt = (void *) kallsyms_lookup_name("lapic_get_maxlvt");
	if (WARN_ON(!_lapic_get_maxlvt))
		return -EFAULT;

	_irq_desc_tree = (void *) kallsyms_lookup_name("irq_desc_tree");
	if (WARN_ON(!_irq_desc_tree))
		return -EFAULT;

	_get_uv_system_type =
		(void *) kallsyms_lookup_name("get_uv_system_type");
	if (WARN_ON(!_get_uv_system_type))
		return -EFAULT;

#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 3, 0)
	_init_deasserted = (void *) kallsyms_lookup_name("init_deasserted");
	if (WARN_ON(!_init_deasserted))
		return -EFAULT;
#endif

	_alloc_desc = (void *) kallsyms_lookup_name("alloc_desc");
	if (WARN_ON(!_alloc_desc))
		return -EFAULT;

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 6, 0)
	___default_send_IPI_dest_field =
		(void *) kallsyms_lookup_name("__default_send_IPI_dest_field");
	if (WARN_ON(!___default_send_IPI_dest_field))
		return -EFAULT;
#endif
	_vmap_area_root = (void *)kallsyms_lookup_name("vmap_area_root");
	if (WARN_ON(!_vmap_area_root))
		return -EFAULT;

	_vmap_area_lock = (void *)kallsyms_lookup_name("vmap_area_lock");
	if (WARN_ON(!_vmap_area_lock))
		return -EFAULT;

	___insert_vmap_area = (void *)kallsyms_lookup_name("__insert_vmap_area");
	if (WARN_ON(!___insert_vmap_area))
		return -EFAULT;

	___free_vmap_area = (void *)kallsyms_lookup_name("__free_vmap_area");
	if (WARN_ON(!___free_vmap_area))
		return -EFAULT;

	return 0;
}

int ihk_smp_get_hw_id(int cpu)
{
	return per_cpu(x86_cpu_to_apicid, cpu);
}

#if defined(RHEL_RELEASE_CODE) || (LINUX_VERSION_CODE < KERNEL_VERSION(4,0,0))
#else
/* origin: arch/x86/kernel/smpboot.c */
static inline void smpboot_setup_warm_reset_vector(unsigned long start_eip)
{
	unsigned long flags;

	spin_lock_irqsave(&rtc_lock, flags);
	CMOS_WRITE(0xa, 0xf);
	spin_unlock_irqrestore(&rtc_lock, flags);
	local_flush_tlb();
	pr_debug("1.\n");
	*((volatile unsigned short *)phys_to_virt(TRAMPOLINE_PHYS_HIGH)) =
		start_eip >> 4;
	pr_debug("2.\n");
	*((volatile unsigned short *)phys_to_virt(TRAMPOLINE_PHYS_LOW)) =
		start_eip & 0xf;
	pr_debug("3.\n");
}
#if LINUX_VERSION_CODE > KERNEL_VERSION(4,3,5)
#warning smpboot_setup_warm_reset_vector() has been only tested up to 4.3.0 kernels
#endif
#endif

static int smp_wakeup_secondary_cpu_via_init(int phys_apicid,
                                             unsigned long start_eip)
{
	unsigned long send_status, accept_status = 0;
	int maxlvt, num_starts, j;

	maxlvt = _lapic_get_maxlvt();

	/*
	 * Be paranoid about clearing APIC errors.
	 */
	if (APIC_INTEGRATED(apic_version[phys_apicid])) {
		if (maxlvt > 3)	/* Due to the Pentium erratum 3AP. */
			apic_write(APIC_ESR, 0);
		apic_read(APIC_ESR);
	}

	pr_debug("Asserting INIT.\n");

	/*
	 * Turn INIT on target chip
	 */
	/*
	 * Send IPI
	 */
	apic_icr_write(APIC_INT_LEVELTRIG | APIC_INT_ASSERT | APIC_DM_INIT,
	               phys_apicid);

	pr_debug("Waiting for send to finish...\n");
	send_status = safe_apic_wait_icr_idle();

	mdelay(10);

	pr_debug("Deasserting INIT.\n");

	/* Target chip */
	/* Send IPI */
	apic_icr_write(APIC_INT_LEVELTRIG | APIC_DM_INIT, phys_apicid);

	pr_debug("Waiting for send to finish...\n");
	send_status = safe_apic_wait_icr_idle();

	mb();
#if LINUX_VERSION_CODE < KERNEL_VERSION(4,3,0)
	atomic_set(_init_deasserted, 1);
#endif

	/* Should we send STARTUP IPIs ?
	 * Determine this based on the APIC version.
	 * If we don't have an integrated APIC, don't send the STARTUP IPIs. */
	if (APIC_INTEGRATED(apic_version[phys_apicid]))
		num_starts = 2;
	else
		num_starts = 0;

	/*
	 * Run STARTUP IPI loop.
	 */
	pr_debug("#startup loops: %d.\n", num_starts);

	for (j = 1; j <= num_starts; j++) {
		pr_debug("Sending STARTUP #%d.\n", j);
		if (maxlvt > 3)	/* Due to the Pentium erratum 3AP. */
			apic_write(APIC_ESR, 0);
		apic_read(APIC_ESR);
		pr_debug("After apic_write.\n");

		/*
		 * STARTUP IPI
		 */

		/* Target chip */
		/* Boot on the stack */
		/* Kick the second */
		apic_icr_write(APIC_DM_STARTUP | (start_eip >> 12),
		               phys_apicid);

		/*
		 * Give the other CPU some time to accept the IPI.
		 */
		udelay(300);

		pr_debug("Startup point 1.\n");

		pr_debug("Waiting for send to finish...\n");
		send_status = safe_apic_wait_icr_idle();

		/*
		 * Give the other CPU some time to accept the IPI.
		 */
		udelay(200);
		if (maxlvt > 3)	/* Due to the Pentium erratum 3AP. */
			apic_write(APIC_ESR, 0);
		accept_status = (apic_read(APIC_ESR) & 0xEF);
		if (send_status || accept_status)
			break;
	}
	pr_debug("After Startup.\n");

	if (send_status)
		printk(KERN_ERR "APIC never delivered???\n");
	if (accept_status)
		printk(KERN_ERR "APIC delivery error (%lx).\n", accept_status);

	return (send_status | accept_status);
}

int smp_wakeup_secondary_cpu(int apicid, unsigned long start_eip)
{
#if LINUX_VERSION_CODE < KERNEL_VERSION(4,3,0)
	atomic_set(_init_deasserted, 0);
#endif

	if (_get_uv_system_type() != UV_NON_UNIQUE_APIC) {
		pr_debug("Setting warm reset code and vector.\n");

		smpboot_setup_warm_reset_vector(start_eip);

		/*
		 * Be paranoid about clearing APIC errors.
		 */
		if (APIC_INTEGRATED(apic_version[boot_cpu_physical_apicid])) {
			apic_write(APIC_ESR, 0);
			apic_read(APIC_ESR);
		}
	}

	if (apic->wakeup_secondary_cpu) {
		printk("%s: apic->wakeup_secondary_cpu()\n", __FUNCTION__);
		return apic->wakeup_secondary_cpu(apicid, start_eip);
	}
	else {
		int ret;
		printk("%s: smp_wakeup_secondary_cpu_via_init()\n", __FUNCTION__);

		preempt_disable();
		ret = smp_wakeup_secondary_cpu_via_init(apicid, start_eip);
		preempt_enable();

		return ret;
	}
}

#ifdef POSTK_DEBUG_ARCH_DEP_29
unsigned long calc_ns_per_tsc(void)
{
	return 1000000000L / tsc_khz;
}
#endif	/* POSTK_DEBUG_ARCH_DEP_29 */

unsigned long x2apic_is_enabled(void)
{
	unsigned long msr;

	rdmsrl(MSR_IA32_APICBASE, msr);

	return msr & (1 << 10); /* x2APIC enabled? */
}

/** \brief Boot a kernel. */
void smp_ihk_setup_trampoline(void *priv)
{
	int i;
	struct smp_os_data *os = priv;
	struct ihk_smp_trampoline_header *header;

	os->param->ihk_ikc_irq = ihk_smp_irq;
	for (i = 0; i < nr_cpu_ids; i++) {
		os->param->ihk_ikc_irq_apicids[i] = per_cpu(x86_bios_cpu_apicid, i);
	}

	os->param->linux_kernel_pgt_phys = __pa(&init_level4_pgt[0]);
	dprintf("%s: Linux kernel init PT: 0x%lx, phys: 0x%lx\n",
		__FUNCTION__, &init_level4_pgt[0], os->param->linux_kernel_pgt_phys);

	/* Make a temporary copy of the Linux trampoline */
	if (using_linux_trampoline) {
		memcpy(linux_trampoline_backup, trampoline_va, IHK_SMP_TRAMPOLINE_SIZE);
	}

	/* Prepare trampoline code */
	memcpy(trampoline_va, ihk_smp_trampoline_data, IHK_SMP_TRAMPOLINE_SIZE);

	header = trampoline_va;
	header->page_table = ident_page_table;
	header->next_ip = os->boot_rip;
	header->notify_address = __pa(os->param);
}

unsigned long smp_ihk_adjust_entry(unsigned long entry,
                                   unsigned long phys)
{
	return entry;
}

static struct vmap_area *lwk_va = NULL;

int smp_ihk_os_setup_startup(void *priv, unsigned long phys,
                            unsigned long entry)
{
	struct smp_os_data *os = priv;
	unsigned long flags;
	unsigned long _virt, _phys, _len;
	unsigned long stack_p;
	extern char startup_data[];
	extern char startup_data_end[];
	unsigned long startup_p;
	unsigned long *startup;
	int vmap_area_taken = 0;

	os->boot_pt = (pgd_t *)get_zeroed_page(GFP_KERNEL);
	if (!os->boot_pt) {
		printk("%s: error: allocating boot PT\n", __FUNCTION__);
		return -ENOMEM;
	}

	/* Map identity (256GB) */
	_len = 0x4000000000UL;
	for (_virt = 0, _phys = 0; _virt < _len;
			_virt += IHK_SMP_LARGE_PAGE, _phys += IHK_SMP_LARGE_PAGE) {
		if (ihk_smp_map_kernel(os->boot_pt, _virt, _phys) < 0) {
			printk("%s: error: mapping identity\n", __FUNCTION__);
			return -ENOMEM;
		}
	}

#if 0
	/* Map PAGE_OFFSET */
	for (_virt = PAGE_OFFSET, _phys = 0; _virt < (PAGE_OFFSET + _len);
			_virt += IHK_SMP_LARGE_PAGE, _phys += IHK_SMP_LARGE_PAGE) {
		if (ihk_smp_map_kernel(os->boot_pt, _virt, _phys) < 0) {
			printk("%s: error: mapping Linux area\n", __FUNCTION__);
			return -ENOMEM;
		}
	}
#endif

	/* Map ST */
	for (_virt = IHK_SMP_MAP_ST_START, _phys = 0; _virt < (IHK_SMP_MAP_ST_START + _len);
			_virt += IHK_SMP_LARGE_PAGE, _phys += IHK_SMP_LARGE_PAGE) {
		if (ihk_smp_map_kernel(os->boot_pt, _virt, _phys) < 0) {
			printk("%s: error: mapping straight area\n", __FUNCTION__);
			return -ENOMEM;
		}
	}

	/* Map kernel image */
	_len = (4 * IHK_SMP_LARGE_PAGE);
	for (_virt = IHK_SMP_MAP_KERNEL_START, _phys = phys; 
			_virt < (IHK_SMP_MAP_KERNEL_START + _len);
			_virt += IHK_SMP_LARGE_PAGE, _phys += IHK_SMP_LARGE_PAGE) {
		if (ihk_smp_map_kernel(os->boot_pt, _virt, _phys) < 0) {
			printk("%s: error: mapping kernel image\n", __FUNCTION__);
			return -ENOMEM;
		}
	}

	/* Stack grows down.. */
	stack_p = os->bootstrap_mem_end - PAGE_SIZE; 
	startup_p = (os->bootstrap_mem_end & IHK_SMP_LARGE_PAGE_MASK) - (2 << PTL2_SHIFT);

	/*
	 * Map in LWK image to Linux kernel space
	 */
	lwk_va = kmalloc(sizeof(*lwk_va), GFP_KERNEL);
	if (!lwk_va) {
		printk("%s: ERROR: allocating LWK va\n", __FUNCTION__);
		return -1;
	}

	spin_lock_irqsave(_vmap_area_lock, flags);
	{
		struct vmap_area *tmp_va;
		struct rb_node *p = rb_last(_vmap_area_root);

		if (p) {
			tmp_va = rb_entry(p, struct vmap_area, rb_node);
			if (tmp_va->va_start >= IHK_SMP_MAP_KERNEL_START)
				vmap_area_taken = 1;
		}
	}

	if (vmap_area_taken) {
		kfree(lwk_va);
		lwk_va = NULL;
		printk("%s: ERROR: reserving LWK kernel memory virtual range\n",
				__FUNCTION__);
	}
	else {
		lwk_va->va_start = IHK_SMP_MAP_KERNEL_START;
		lwk_va->va_end = MODULES_END;
		lwk_va->flags = 0;
		___insert_vmap_area(lwk_va);
	}
	spin_unlock_irqrestore(_vmap_area_lock, flags);

	if (vmap_area_taken)
		return -1;

	if (ioremap_page_range(IHK_SMP_MAP_KERNEL_START, MODULES_END,
				phys, PAGE_KERNEL_EXEC) < 0) {
		printk("%s: error: mapping LWK to Linux kernel space\n",
				__FUNCTION__);
	}

	startup = ihk_smp_map_virtual(startup_p, PAGE_SIZE);
	memcpy(startup, startup_data, startup_data_end - startup_data);
	startup[2] = __pa(os->boot_pt);
	startup[3] = stack_p;
	startup[4] = phys;
	startup[5] = trampoline_phys;
	startup[6] = entry;
	ihk_smp_unmap_virtual(startup);
	os->boot_rip = startup_p;

	return 0;
}

int smp_ihk_os_unmap_lwk() {
	if (lwk_va) {
		unsigned long flags;

		/* Unmap LWK from Linux kernel virtual */
		unmap_kernel_range_noflush(IHK_SMP_MAP_KERNEL_START,
				MODULES_END - IHK_SMP_MAP_KERNEL_START);

		spin_lock_irqsave(_vmap_area_lock, flags);
		___free_vmap_area(lwk_va);
		lwk_va = NULL;
		spin_unlock_irqrestore(_vmap_area_lock, flags);
	}
	return 0;
}

int smp_ihk_os_send_nmi(ihk_os_t ihk_os, void *priv, int mode)
{
	struct smp_os_data *os = priv;
	int i, ret;

	ret = ihk_smp_set_nmi_mode(ihk_os, priv, mode);
	if (ret) {
		return ret;
	}

	for (i = 0; i < os->cpu_info.n_cpus; i++) {

#ifdef CONFIG_X86_X2APIC
		if (x2apic_is_enabled()) {
			safe_apic_wait_icr_idle();
			apic_icr_write(APIC_DM_NMI, os->cpu_info.hw_ids[i]);
		}
		else 		
#endif
		{
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4,6,0)
			___default_send_IPI_dest_field(
				os->cpu_info.hw_ids[i],
				NMI_VECTOR, APIC_DEST_PHYSICAL);
#else
			__default_send_IPI_dest_field(
				os->cpu_info.hw_ids[i],
				NMI_VECTOR, APIC_DEST_PHYSICAL);
#endif
		}
	}
	return 0;
}

#ifdef POSTK_DEBUG_ARCH_DEP_46 /* user area direct access fix. */
static long get_dump_num_mem_areas(struct smp_os_data *os)
{
	struct ihk_dump_page *dump_page = NULL;
	int i, j, k, mem_num;
	unsigned long bit_count;

	while (1) {
		if (IHK_DUMP_PAGE_SET_COMPLETED == os->param->dump_page_set.completion_flag) {
			break;
		}
		msleep(10); /* 10ms sleep */
	}
	dump_page = phys_to_virt(os->param->dump_page_set.phy_page);

	for (i = 0, mem_num = 0; i < os->param->dump_page_set.count; i++) {
		if (i) {
			dump_page = (struct ihk_dump_page *)((char *)dump_page + ((dump_page->map_count * sizeof(unsigned long)) + sizeof(struct ihk_dump_page)));
		}

		for (j = 0, bit_count = 0; j < dump_page->map_count; j++) {
			for (k = 0; k < 64; k++) {
				if ((dump_page->map[j] >> k) & 0x1) {
					bit_count++;
				}
				else {
					if (bit_count) {
						mem_num++;
						bit_count = 0;
					}
				}
			}
		}

		if (bit_count) {
			mem_num++;
		}
	}
	return (sizeof(dump_mem_chunks_t) + (sizeof(struct dump_mem_chunk) * mem_num));
}
#endif /* POSTK_DEBUG_ARCH_DEP_46 */

int smp_ihk_os_dump(ihk_os_t ihk_os, void *priv, dumpargs_t *args)
{
	struct smp_os_data *os = priv;
	struct ihk_dump_page *dump_page = NULL;
#ifdef POSTK_DEBUG_ARCH_DEP_46 /* user area direct access fix. */
	int i,j,k,index;
	long mem_size;
#else
	int i,j,k,mem_num,index;
#endif /* POSTK_DEBUG_ARCH_DEP_46 */
	struct ihk_os_mem_chunk *os_mem_chunk;
	unsigned long map_start, bit_count;
	dump_mem_chunks_t *mem_chunks;
	void *va;
	extern struct list_head ihk_mem_used_chunks;

	if (0) printk("mcosdump: cmd %d start %lx size %lx buf %p\n",
			args->cmd, args->start, args->size, args->buf);

	switch (args->cmd) {
		
		case DUMP_SET_LEVEL:
			/* Set dump level information */
			switch (args->level) {
				case DUMP_LEVEL_ALL:
				case DUMP_LEVEL_USER_UNUSED_EXCLUDE:
					os->param->dump_level = args->level;
					break;
				default:
					printk("%s:invalid dump level:%d\n", __FUNCTION__, args->level);
					return -EINVAL;
			}
			break;

		case DUMP_NMI:
			if (os->param->dump_page_set.completion_flag !=  IHK_DUMP_PAGE_SET_COMPLETED) {
				smp_ihk_os_send_nmi(ihk_os, priv, 0);
			}
			break;

		case DUMP_QUERY_NUM_MEM_AREAS:
#ifdef POSTK_DEBUG_ARCH_DEP_46 /* user area direct access fix. */
			args->size = get_dump_num_mem_areas(os);
#else
			while (1) {
				if (IHK_DUMP_PAGE_SET_COMPLETED == os->param->dump_page_set.completion_flag)
					break;

				msleep(10); /* 10ms sleep */
			}

			dump_page = phys_to_virt(os->param->dump_page_set.phy_page);

			for (i = 0, mem_num = 0; i < os->param->dump_page_set.count; i++) {

				if (i) {
					dump_page = (struct ihk_dump_page *)((char *)dump_page + ((dump_page->map_count * sizeof(unsigned long)) + sizeof(struct ihk_dump_page)));
				}

				for (j = 0, bit_count = 0; j < dump_page->map_count; j++) {
					for ( k = 0; k < 64; k++) {
						if ((dump_page->map[j] >> k) & 0x1) {
							bit_count++;
						} else {
							if (bit_count) {
								mem_num++;
								bit_count = 0;
							}
						}
					}
				}

				if (bit_count) {
					mem_num++;
				}
			}

			args->size = (sizeof(dump_mem_chunks_t) + (sizeof(struct dump_mem_chunk) * mem_num));

#endif /* POSTK_DEBUG_ARCH_DEP_46 */
			break;

		case DUMP_QUERY:
			i = 0;
#ifdef POSTK_DEBUG_ARCH_DEP_46 /* user area direct access fix. */
			mem_size = get_dump_num_mem_areas(os);
			mem_chunks = kmalloc(mem_size, GFP_KERNEL);
			if (!mem_chunks) {
				printk("%s: memory allocation failed.\n", __FUNCTION__);
				return -ENOMEM;
			}
			memset(mem_chunks, 0, mem_size);
#else
			mem_chunks = args->buf;
#endif /* POSTK_DEBUG_ARCH_DEP_46 */

			/* Collect memory information */
			list_for_each_entry(os_mem_chunk, &ihk_mem_used_chunks, list) {
				if (os_mem_chunk->os != ihk_os)
					continue;

				mem_chunks->chunks[i].addr = os_mem_chunk->addr;
				mem_chunks->chunks[i].size = os_mem_chunk->size;
				++i;
			}

			mem_chunks->nr_chunks = i;
			/* See load_file() for the calculation below */
			mem_chunks->kernel_base =
				(os->bootstrap_mem_start + IHK_SMP_LARGE_PAGE * 2 - 1) & IHK_SMP_LARGE_PAGE_MASK;
#ifdef POSTK_DEBUG_ARCH_DEP_46 /* user area direct access fix. */

			if (copy_to_user(args->buf, mem_chunks, mem_size)) {
				printk("%s: copy_to_user failed.\n", __FUNCTION__);
				kfree(mem_chunks);
				return -EFAULT;
			}
			kfree(mem_chunks);
#endif /* POSTK_DEBUG_ARCH_DEP_46 */
			break;

		case DUMP_QUERY_MEM_AREAS:
#ifdef POSTK_DEBUG_ARCH_DEP_46 /* user area direct access fix. */
			mem_size = get_dump_num_mem_areas(os);
			mem_chunks = kmalloc(mem_size, GFP_KERNEL);
			if (!mem_chunks) {
				printk("%s: memory allocation failed.\n", __FUNCTION__);
				return -ENOMEM;
			}
			memset(mem_chunks, 0, mem_size);

#else

			mem_chunks = args->buf;
#endif /* POSTK_DEBUG_ARCH_DEP_46 */
			dump_page = phys_to_virt(os->param->dump_page_set.phy_page);

			for (i = 0, index = 0; i < os->param->dump_page_set.count; i++) {

				if (i) {
					dump_page = (struct ihk_dump_page *)((char *)dump_page + ((dump_page->map_count * sizeof(unsigned long)) + sizeof(struct ihk_dump_page)));
				}

				for (j = 0, bit_count = 0; j < dump_page->map_count; j++) {
					for (k = 0; k < 64; k++) {
						if ((dump_page->map[j] >> k) & 0x1) {
							if (!bit_count) {
								map_start = (unsigned long)(dump_page->start + ((unsigned long)j << (PAGE_SHIFT+6)));
								map_start = map_start + ((unsigned long)k << PAGE_SHIFT);
							}
							bit_count++;
						} else {
							if (bit_count) {
								mem_chunks->chunks[index].addr = map_start;
								mem_chunks->chunks[index].size = (bit_count << PAGE_SHIFT);
								index++;
								bit_count = 0;
							}
						}
					}
				}

				if (bit_count) {
					mem_chunks->chunks[index].addr = map_start;
					mem_chunks->chunks[index].size = (bit_count << PAGE_SHIFT);
					index++;
				}
			}

			mem_chunks->nr_chunks = index;

			/* See load_file() for the calculation below */
			mem_chunks->kernel_base =
				(os->bootstrap_mem_start + IHK_SMP_LARGE_PAGE * 2 - 1) & IHK_SMP_LARGE_PAGE_MASK;

#ifdef POSTK_DEBUG_ARCH_DEP_46 /* user area direct access fix. */
			if (copy_to_user(args->buf, mem_chunks, mem_size)) {
				printk("%s: copy_to_user failed.\n", __FUNCTION__);
				kfree(mem_chunks);
				return -EFAULT;
			}
			kfree(mem_chunks);
#endif /* POSTK_DEBUG_ARCH_DEP_46 */
			break;

		case DUMP_READ:
			va = phys_to_virt(args->start);
			if (copy_to_user(args->buf, va, args->size)) {
				return -EFAULT;
			}

			break;

		case DUMP_QUERY_ALL:
			args->start = os->mem_start;
			args->size = os->mem_end - os->mem_start;
			break;

		case DUMP_READ_ALL:
			va = phys_to_virt(args->start);
			if (copy_to_user(args->buf, va, args->size)) {
				return -EFAULT;
			}
			break;

		default:
			return -EINVAL;
	}

	return 0;
}

enum ihk_os_status smp_ihk_os_query_status(ihk_os_t ihk_os, void *priv)
{
	struct smp_os_data *os = priv;
	int status;

	status = os->status;

	switch (status) {
	case BUILTIN_OS_STATUS_BOOTING:
		if (os->param->status == 1) {
			return IHK_OS_STATUS_BOOTED;
		} else if(os->param->status == 2) {
			/* Restore Linux trampoline once ready */
			if (using_linux_trampoline) {
				memcpy(trampoline_va, linux_trampoline_backup, 
						IHK_SMP_TRAMPOLINE_SIZE);
			}
			return IHK_OS_STATUS_READY;
		} else if(os->param->status == 3) {
			return IHK_OS_STATUS_RUNNING;
		} else {
			return IHK_OS_STATUS_BOOTING;
		}
		break;
	case BUILTIN_OS_STATUS_HUNGUP:
		return IHK_OS_STATUS_HUNGUP;
#ifdef POSTK_DEBUG_TEMP_FIX_82 /* ihk_os_get_status() SHUTDOWN detect fix */
	case BUILTIN_OS_STATUS_SHUTDOWN:
		return IHK_OS_STATUS_SHUTDOWN;
#endif /* POSTK_DEBUG_TEMP_FIX_82 */
	default:
		return IHK_OS_STATUS_NOT_BOOTED;
	}
}

int smp_ihk_os_issue_interrupt(ihk_os_t ihk_os, void *priv,
                               int cpu, int v)
{
	struct smp_os_data *os = priv;
	unsigned long flags;

	/* better calcuation or make map */
	if (cpu < 0 || cpu >= os->cpu_info.n_cpus) {
		return -EINVAL;
	}
//	printk("smp_ihk_os_issue_interrupt(): %d\n", os->cpu_info.hw_ids[cpu]);
//	shimos_issue_ipi(os->cpu_info.hw_ids[cpu], v);
	
	local_irq_save(flags);
#ifdef CONFIG_X86_X2APIC
	if (x2apic_is_enabled()) {
		native_x2apic_icr_write(v, os->cpu_info.hw_ids[cpu]);
	}
	else
#endif
	{
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4,6,0)
		___default_send_IPI_dest_field(os->cpu_info.hw_ids[cpu], v, 
			APIC_DEST_PHYSICAL);
#else
		__default_send_IPI_dest_field(os->cpu_info.hw_ids[cpu], v, 
			APIC_DEST_PHYSICAL);
#endif
	}
	local_irq_restore(flags);

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
	ack_APIC_irq();
	smp_ihk_irq_call_handlers(ihk_smp_irq, NULL);
	return IRQ_HANDLED;
}

#ifdef CONFIG_SPARSE_IRQ
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4,3,0)
void (*orig_irq_flow_handler)(struct irq_desc *desc) = NULL;
void ihk_smp_irq_flow_handler(struct irq_desc *desc)
#else
static void (*orig_irq_flow_handler)(unsigned int irq, struct irq_desc *desc) = NULL;
static void ihk_smp_irq_flow_handler(unsigned int irq, struct irq_desc *desc)
#endif /* LINUX_VERSION_CODE >= KERNEL_VERSION(4,3,0) */
{
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4,3,0)
	unsigned int irq = desc->irq_data.irq;
#endif
	if (!desc->action || !desc->action->handler) {
		printk("IHK-SMP: no handler for IRQ %d??\n", irq);
		return;
	}

	raw_spin_lock(&desc->lock);

//	printk("IHK-SMP: calling handler for IRQ %d\n", irq);
	desc->action->handler(irq, NULL);
//	ack_APIC_irq();

	raw_spin_unlock(&desc->lock);
}
#endif

static int smp_ihk_init_ident_page_table(void)
{
	int ident_npages = 0;
	int i, j, k;
	unsigned long maxmem = 0, *p, physaddr;
	struct page *ident_pages;

	/* 256GB */
	maxmem = (unsigned long)256 * (1024 * 1024 * 1024);

	ident_npages = (maxmem + (1UL << PUD_SHIFT) - 1) >> PUD_SHIFT;
	ident_npages_order = fls(ident_npages + 2) - 1;
	if ((2 << ident_npages_order) != ident_npages + 2) {
		ident_npages_order++;
	}

	printk("IHK-SMP: page table pages = %d, ident_npages_order = %d\n",
	       ident_npages, ident_npages_order);

	ident_pages = alloc_pages(GFP_DMA32 | GFP_KERNEL, ident_npages_order);
	if (!ident_pages) {
		printk("IHK-SMP: error: allocating identity page tables\n");
		return ENOMEM;
	}

	ident_page_table = page_to_phys(ident_pages);
	ident_page_table_virt = pfn_to_kaddr(page_to_pfn(ident_pages));

	memset(ident_page_table_virt, 0, ident_npages);

	/* First level : We consider only < 512 GB of memory */
	ident_page_table_virt[0] = (ident_page_table + PAGE_SIZE) | 0x63;

	/* Second level */
	p = ident_page_table_virt + (PAGE_SIZE / sizeof(*p));

	for (i = 0; i < PTRS_PER_PUD; i++) {
		if (((unsigned long)i << PUD_SHIFT) < maxmem) {
		    *p = (ident_page_table + PAGE_SIZE * (2 + i)) | 0x63;
		}
		else {
			break;
		}
		p++;
	}

	if (i != ident_npages) {
		printk("Something wrong for memory map. : %d vs %d\n",
		       i, ident_npages);
	}

	/* Third level */
	p = ident_page_table_virt + (PAGE_SIZE * 2 / sizeof(*p));
	for (j = 0; j < ident_npages; j++) {
		for (k = 0; k < PTRS_PER_PMD; k++) {
			physaddr = ((unsigned long)j << PUD_SHIFT) |
			            ((unsigned long)k << PMD_SHIFT);
			if (physaddr < maxmem) {
				*p = physaddr | 0xe3;
				p++;
			}
			else {
				break;
			}
		}
	}

	printk("IHK-SMP: identity page tables allocated\n");
	return 0;
}

static struct irq_chip ihk_irq_chip = {
	.name = "ihk_irq",
};

static int
vector_is_used(int vector, int core) {
	int rtn = 0;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4,3,0)
	/* As of 4.3.0, vector_irq is an array of struct irq_desc pointers */
	struct irq_desc **vectors = (*SHIFT_PERCPU_PTR((vector_irq_t *)_vector_irq,
					per_cpu_offset(core)));
#else
/* TODO: find out where exactly between 2.6.32 and 3.0.0 vector_irq was changed */
	int *vectors = (*SHIFT_PERCPU_PTR((vector_irq_t *)_vector_irq,
				per_cpu_offset(core)));
#endif /* LINUX_VERSION_CODE < KERNEL_VERSION(4,3,0) */

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4,3,0)
	if (vectors[vector] != VECTOR_UNUSED) {
		printk(KERN_INFO "IHK-SMP: IRQ vector %d in core %d: used %d \n",
				vector, core, vectors[vector]);
		rtn = 1;
	}
#else
	if (vectors[vector] != -1) {
		printk(KERN_INFO "IHK-SMP: IRQ vector %d in core %d: used %d \n",
				vector, core, vectors[vector]);
		rtn = 1;
	}
#endif /* LINUX_VERSION_CODE < KERNEL_VERSION(4,3,0) */
	return rtn;
}

static void
set_vector(int vector, int core) {
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4,3,0)
	/* As of 4.3.0, vector_irq is an array of struct irq_desc pointers */
	struct irq_desc **vectors = (*SHIFT_PERCPU_PTR((vector_irq_t *)_vector_irq,
						per_cpu_offset(core)));
#else
	int *vectors = (*SHIFT_PERCPU_PTR((vector_irq_t *)_vector_irq,
				per_cpu_offset(core)));
#endif

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4,3,0)
	if (vectors[vector] == VECTOR_UNUSED) {
		dprintk(KERN_INFO "IHK-SMP: fixed vector_irq for %d in core %d\n", vector, core);
		vectors[vector] = desc;
	}
#else
	if (vectors[vector] == -1) {
		dprintk(KERN_INFO "IHK-SMP: fixed vector_irq for %d in core %d\n", vector, core);
		vectors[vector] = vector;
	}
#endif
}

static void
release_vector(int vector, int core) {
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4,3,0)
	/* As of 4.3.0, vector_irq is an array of struct irq_desc pointers */
	struct irq_desc **vectors = (*SHIFT_PERCPU_PTR((vector_irq_t *)_vector_irq,
				per_cpu_offset(core)));
#else
	int *vectors = (*SHIFT_PERCPU_PTR((vector_irq_t *)_vector_irq,
				per_cpu_offset(core)));
#endif

	/* Release IRQ vector */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4,3,0)
	vectors[vector] = VECTOR_UNUSED;
#else
	vectors[vector] = -1;
#endif
}

static int collect_topology(void);
int smp_ihk_arch_init(void)
{
	int error = 0;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4,2,0)
	int vector = ISA_IRQ_VECTOR(15) + 2;
#else
	int vector = IRQ15_VECTOR + 2;
#endif
	int i = 0;
	int is_used = 0;

	if (ihk_trampoline) {
		printk("IHK-SMP: preallocated trampoline phys: 0x%lx\n",
		        ihk_trampoline);

		trampoline_phys = ihk_trampoline;
		trampoline_va = ioremap_cache(trampoline_phys, PAGE_SIZE);
	}
	else {
#define TRAMP_ATTEMPTS  20
		int attempts = 0;
		struct page *bad_pages[TRAMP_ATTEMPTS];

		memset(bad_pages, 0, TRAMP_ATTEMPTS * sizeof(struct page *));

		/* Try to allocate trampoline page, it has to be under 1M so we can
		 * execute real-mode AP code. If allocation fails more than
		 * TRAMP_ATTEMPTS times, we will use Linux's one.
		 * NOTE: using Linux trampoline could potentially cause race
		 * conditions with concurrent CPU onlining requests */
retry_trampoline:
		trampoline_page = alloc_pages(GFP_DMA | GFP_KERNEL, 1);
		
		if (!trampoline_page || page_to_phys(trampoline_page) > 0xFF000) {
			bad_pages[attempts] = trampoline_page;

			if (++attempts < TRAMP_ATTEMPTS) {
				goto retry_trampoline;
			}
		}

		/* Free failed attempts.. */
		for (attempts = 0; attempts < TRAMP_ATTEMPTS; ++attempts) {
			if (!bad_pages[attempts]) {
				continue;
			}

			free_pages((unsigned long)pfn_to_kaddr(page_to_pfn(bad_pages[attempts])), 1);
		}

		/* Couldn't allocate trampoline page, use Linux' one from real_header */
		if (!trampoline_page || page_to_phys(trampoline_page) > 0xFF000) {
			using_linux_trampoline = 1;
			printk("IHK-SMP: warning: allocating trampoline_page failed, using Linux'\n");
			trampoline_phys = _real_mode_header->trampoline_start;
			trampoline_va = __va(trampoline_phys);
		}
		else {
			trampoline_phys = page_to_phys(trampoline_page);
			trampoline_va = pfn_to_kaddr(page_to_pfn(trampoline_page));
		}

		printk(KERN_INFO "IHK-SMP: trampoline_page phys: 0x%lx\n", trampoline_phys);
	}

	/* Find a suitable IRQ vector */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4,2,0)
	for (vector = ihk_start_irq ? ihk_start_irq : (ISA_IRQ_VECTOR(14) + 2);
			vector < 256; vector += 1) {
#else
	for (vector = ihk_start_irq ? ihk_start_irq : (IRQ14_VECTOR + 2);
			vector < 256; vector += 1) {
#endif
#ifdef CONFIG_SPARSE_IRQ
		struct irq_desc *desc;
#endif

		if (test_bit(vector, used_vectors)) {
			printk(KERN_INFO "IHK-SMP: IRQ vector %d: used\n", vector);
			continue;
		}

		for (i = 0; i < nr_cpu_ids; i++) {
			if (vector_is_used(vector, i)) {
				is_used = 1;
				break;
			}
		}

		if (is_used) {
			is_used = 0;
			continue;
		}

#ifdef CONFIG_SPARSE_IRQ
		/* If no descriptor, create one */
		desc = irq_to_desc(vector);
		if (!desc) {
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 8, 0) || \
	(defined(RHEL_RELEASE_CODE) && RHEL_RELEASE_CODE >= RHEL_RELEASE_VERSION(7, 5))
			desc = _alloc_desc(vector, first_online_node,
					   0 /* flags */, NULL /* affinity */,
					   THIS_MODULE);
#else
			desc = _alloc_desc(vector, first_online_node,
			                   THIS_MODULE);
#endif
			desc->irq_data.chip = &dummy_irq_chip;
			radix_tree_insert(_irq_desc_tree, vector, desc);
		}

		desc = irq_to_desc(vector);
		if (!desc) {
			printk(KERN_INFO "IHK-SMP: IRQ vector %d: no descriptor\n", vector);
			continue;
		}

		if (desc->action) {
			// action is already registered.
			continue;
		}

		if (desc->status_use_accessors & IRQ_NOREQUEST) {
			
			printk(KERN_INFO "IHK-SMP: IRQ vector %d: not allowed to request, fake it\n", vector);
			
			desc->status_use_accessors &= ~IRQ_NOREQUEST;
		}
		orig_irq_flow_handler = desc->handle_irq;
		desc->handle_irq = ihk_smp_irq_flow_handler;
#endif // CONFIG_SPARSE_IRQ

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4,1,0))
#define IRQF_DISABLED 0x0
#endif
		if (request_irq(vector, 
					smp_ihk_irq_handler, IRQF_DISABLED, "IHK-SMP", NULL) != 0) {
			printk(KERN_INFO "IHK-SMP: IRQ vector %d: request_irq failed\n", vector);
			continue;
		}

		/* Pretend a real external interrupt */
		for (i = 0; i < nr_cpu_ids; i++) {
			set_vector(vector, i);
		}
		break;
	}

	if (vector >= 256) {
		printk(KERN_ERR "IHK-SMP: error: allocating IKC irq vector\n");
		error = EFAULT;
		goto error_free_trampoline;
	}

	ihk_smp_irq = vector;
	ihk_smp_irq_apicid = (int)per_cpu(x86_bios_cpu_apicid,
	                                  ihk_ikc_irq_core);
	printk(KERN_INFO "IHK-SMP: IKC irq vector: %d, CPU logical id: %u, CPU APIC id: %d\n",
	    ihk_smp_irq, ihk_ikc_irq_core, ihk_smp_irq_apicid);

	irq_set_chip(vector, &ihk_irq_chip);
	irq_set_chip_data(vector, NULL);

	error = smp_ihk_init_ident_page_table();
	if (error) {
		printk(KERN_ERR "IHK-SMP: error: identity page table initialization failed\n");
		goto error_free_irq;
	}

	error = collect_topology();
	if (error) {
		printk(KERN_ERR "IHK-SMP: error: collecting topology information failed\n");
		free_irq(ihk_smp_irq, NULL);
		goto error_free_irq;
	}

	return error;

error_free_irq:
#if LINUX_VERSION_CODE <= KERNEL_VERSION(4, 3, 0)
	if (this_module_put) {
		try_module_get(THIS_MODULE);
	}
#endif

	free_irq(ihk_smp_irq, NULL);

error_free_trampoline:
	if (trampoline_page) {
		free_pages((unsigned long)pfn_to_kaddr(page_to_pfn(trampoline_page)), 1);
	}
	else {
		if (!using_linux_trampoline)
			iounmap(trampoline_va);
	}

	return error;
}

LIST_HEAD(cpu_topology_list);
LIST_HEAD(node_topology_list);

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
	if (error) {
		eprintk("ihk:collect_cache_topology:"
				"read_long(level) failed. %d\n", error);
		goto out;
	}

	error = read_string(&p->type, "%s/type", prefix);
	if (error) {
		eprintk("ihk:collect_cache_topology:"
				"read_string(type) failed. %d\n", error);
		goto out;
	}

	error = read_long(&p->size, "%s/size", prefix);
	if (error) {
		eprintk("ihk:collect_cache_topology:"
				"read_long(size) failed. %d\n", error);
		goto out;
	}
	p->size *= 1024;	/* XXX */

	error = read_string(&p->size_str, "%s/size", prefix);
	if (error) {
		eprintk("ihk:collect_cache_topology:"
				"read_string(size) failed. %d\n", error);
		goto out;
	}

	error = read_long(&p->coherency_line_size,
			"%s/coherency_line_size", prefix);
	if (error) {
		eprintk("ihk:collect_cache_topology:"
				"read_long(coherency_line_size) failed. %d\n",
				error);
		goto out;
	}

	error = read_long(&p->number_of_sets, "%s/number_of_sets", prefix);
	if (error) {
		eprintk("ihk:collect_cache_topology:"
				"read_long(number_of_sets) failed. %d\n",
				error);
		goto out;
	}

	error = read_long(&p->physical_line_partition,
			"%s/physical_line_partition", prefix);
	if (error) {
		eprintk("ihk:collect_cache_topology:"
				"read_long(physical_line_partition) failed."
				" %d\n", error);
		goto out;
	}

	error = read_long(&p->ways_of_associativity,
			"%s/ways_of_associativity", prefix);
	if (error) {
		eprintk("ihk:collect_cache_topology:"
				"read_long(ways_of_associativity) failed."
				" %d\n", error);
		goto out;
	}

	error = read_bitmap(&p->shared_cpu_map, nr_cpumask_bits,
			"%s/shared_cpu_map", prefix);
	if (error) {
		eprintk("ihk:collect_cache_topology:"
				"read_bitmap(shared_cpu_map) failed. %d\n",
				error);
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
	p->hw_id = per_cpu(x86_cpu_to_apicid, cpu);

	error = read_long(&p->core_id, "%s/topology/core_id", prefix);
	if (error) {
		eprintk("ihk:collect_cpu_info:"
				"read_long(core_id) failed. %d\n", error);
		goto out;
	}

	error = read_bitmap(&p->core_siblings, nr_cpumask_bits,
			"%s/topology/core_siblings", prefix);
	if (error) {
		eprintk("ihk:collect_cpu_info:"
				"read_bitmap(core_siblings) failed. %d\n",
				error);
		goto out;
	}

	error = read_long(&p->physical_package_id,
			"%s/topology/physical_package_id", prefix);
	if (error) {
		eprintk("ihk:collect_cpu_info:"
				"read_long(physical_package_id) failed. %d\n",
				error);
		goto out;
	}

	error = read_bitmap(&p->thread_siblings, nr_cpumask_bits,
			"%s/topology/thread_siblings", prefix);
	if (error) {
		eprintk("ihk:collect_cpu_info:"
				"read_bitmap(thread_siblings) failed. %d\n",
				error);
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
		goto out;
	}

	error = 0;
	list_add(&p->chain, &node_topology_list);
	p = NULL;

out:
	kfree(p);
	dprintk("collect_node_topology(%d): %d\n", node, error);
	return error;
} /* collect_node_topology() */

static int collect_topology(void)
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

#ifdef POSTK_DEBUG_ARCH_DEP_98 /* smp_ihk_os_set_ikc_map() move arch depend. */
int smp_ihk_os_set_ikc_map(ihk_os_t ihk_os, void *priv, unsigned long arg)
{
	int ret = 0;
	struct smp_os_data *os = priv;
	cpumask_t cpus_to_map;
	unsigned long flags;
#ifdef POSTK_DEBUG_ARCH_DEP_46 /* user area direct access fix. */
	char *string = NULL;
	long len = strnlen_user((const char __user *)arg, 32767);
#else /* POSTK_DEBUG_ARCH_DEP_46 */
	char *string = (char *)arg;
#endif /* POSTK_DEBUG_ARCH_DEP_46 */
	char *token;

	dprintk("%s,set_ikc_map,arg=%s\n", __FUNCTION__, string);

#ifdef POSTK_DEBUG_ARCH_DEP_46 /* user area direct access fix. */
	if (len == 0) {
		printk("%s: invalid request length\n", __FUNCTION__);
		return -EINVAL;
	}

	string = kmalloc(len + 1, GFP_KERNEL);
	if (!string) {
		printk("%s: error: allocating request string\n", __FUNCTION__);
		return -EINVAL;
	}

	if (copy_from_user(string, (char *)arg, len + 1)) {
		printk("%s: error: copying request string\n", __FUNCTION__);
		ret = -EFAULT;
		goto out;
	}
#endif /* POSTK_DEBUG_ARCH_DEP_46 */

	spin_lock_irqsave(&os->lock, flags);
	if (os->status != BUILTIN_OS_STATUS_INITIAL) {
		spin_unlock_irqrestore(&os->lock, flags);
		ret = -EBUSY;
		goto out;
	}
	spin_unlock_irqrestore(&os->lock, flags);

	token = strsep(&string, "+");
	while (token) {
		char *cpu_list;
		char *ikc_cpu;
		int cpu;

		cpu_list = strsep(&token, ":");
		if (!cpu_list) {
			ret = -EINVAL;
			goto out;
		}

		memset(&cpus_to_map, 0, sizeof(cpus_to_map));
		cpulist_parse(cpu_list, &cpus_to_map);

		ikc_cpu = strsep(&token, ":");
		if (!ikc_cpu) {
			ret = -EINVAL;
			goto out;
		}

		printk("%s: %s -> %s\n", __FUNCTION__, cpu_list, ikc_cpu);
		/* Store IKC target CPU */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4,0,0)
		for_each_cpu(cpu, &cpus_to_map) {
#else
		for_each_cpu_mask(cpu, cpus_to_map) {
#endif
#ifdef POSTK_DEBUG_TEMP_FIX_90 /* Add correspondence when illegal value is specified for ikc_map */
			unsigned int int_ikc_cpu = 0;
			int st = 0;

			if (kstrtoint(ikc_cpu, 10, &int_ikc_cpu)) {
				printk("kstrtoint() failed\n");
				ret = -EINVAL;
				goto out;
			}

			if (SMP_MAX_CPUS <= int_ikc_cpu) {
				printk("ikc_map included over SMP_MAX_CPUS(%d) number(%d).\n",
					SMP_MAX_CPUS, int_ikc_cpu);
				ret = -EINVAL;
				goto out;
			}
			st = ihk_smp_cpus[int_ikc_cpu].status;

			if (st == IHK_SMP_CPU_ASSIGNED) {
				printk("ikc_map included McKernel-core number(%d).\n",
					int_ikc_cpu);
				ret = -EINVAL;
				goto out;
			} else if (st != IHK_SMP_CPU_ONLINE) {
				printk("ikc_map included Blank-core number(%d).\n",
					int_ikc_cpu);
				ret = -EINVAL;
				goto out;
			} else {
				ihk_smp_cpus[cpu].ikc_map_cpu = int_ikc_cpu;
			}
#else /* POSTK_DEBUG_TEMP_FIX_90 */
			/* TODO: check if CPU belongs to OS */
			if (kstrtoint(ikc_cpu, 10, &ihk_smp_cpus[cpu].ikc_map_cpu)) {
				ret = -EINVAL;
				goto out;
			}
#endif /* POSTK_DEBUG_TEMP_FIX_90 */
		}

		token = strsep(&string, "+");
	}
	/* Mapping has been requested */
	os->cpu_ikc_mapped = 1;

out:
#ifdef POSTK_DEBUG_TEMP_FIX_90 /* Add correspondence when illegal value is specified for ikc_map */
	/* In case of no mapped, restore default setting */
	if (os->cpu_ikc_mapped != 1) {
		for (i = 0; i < SMP_MAX_CPUS; i++) {
			if ((ihk_smp_cpus[i].status != IHK_SMP_CPU_ASSIGNED) ||
			    (ihk_smp_cpus[i].os != ihk_os)) {
				continue;
			}
			ihk_smp_cpus[i].ikc_map_cpu = 0;
		}
	}
#endif /* POSTK_DEBUG_TEMP_FIX_90 */
#ifdef POSTK_DEBUG_ARCH_DEP_46 /* user area direct access fix. */
	if (string) kfree(string);
#endif /* POSTK_DEBUG_ARCH_DEP_46 */
	return ret;
}
#endif /* POSTK_DEBUG_ARCH_DEP_98 */

int ihk_smp_reset_cpu(int phys_apicid)
{
	unsigned long send_status;
	int maxlvt;

	preempt_disable();
	dprintk(KERN_INFO "IHK-SMP: resetting CPU %d.\n", phys_apicid);

	maxlvt = _lapic_get_maxlvt();

	/*
	 * Be paranoid about clearing APIC errors.
	 */
	if (APIC_INTEGRATED(apic_version[phys_apicid])) {
		if (maxlvt > 3) /* Due to the Pentium erratum 3AP. */
			apic_write(APIC_ESR, 0);
		apic_read(APIC_ESR);
	}

	pr_debug("Asserting INIT.\n");

	/*
	 * Turn INIT on target chip
	 */
	/*
	 * Send IPI
	 */
	apic_icr_write(APIC_INT_LEVELTRIG | APIC_INT_ASSERT | APIC_DM_INIT,
	               phys_apicid);

	pr_debug("Waiting for send to finish...\n");
	send_status = safe_apic_wait_icr_idle();

	mdelay(10);

	pr_debug("Deasserting INIT.\n");

	/* Target chip */
	/* Send IPI */
	apic_icr_write(APIC_INT_LEVELTRIG | APIC_DM_INIT, phys_apicid);

	pr_debug("Waiting for send to finish...\n");
	send_status = safe_apic_wait_icr_idle();

	preempt_enable();
	return 0;
}

void smp_ihk_arch_exit(void)
{
	int i = 0;
#ifdef CONFIG_SPARSE_IRQ
	struct irq_desc *desc;
#endif

	/* Release IRQ vector */
	for (i = 0; i < nr_cpu_ids; i++) {
		release_vector(ihk_smp_irq, i);
	}

	irq_set_chip(ihk_smp_irq, NULL);

#ifdef CONFIG_SPARSE_IRQ
	desc = irq_to_desc(ihk_smp_irq);
	if (desc) {
		desc->handle_irq = orig_irq_flow_handler;
	} else {
		printk("%s: Warning: irq_to_desc(ihk_smp_irq) returns NULL\n", __FUNCTION__);
	}
#endif

#if LINUX_VERSION_CODE <= KERNEL_VERSION(4, 3, 0)
	if (this_module_put) {
		try_module_get(THIS_MODULE);
	}
#endif
	free_irq(ihk_smp_irq, NULL);

#ifdef CONFIG_SPARSE_IRQ
	irq_free_descs(ihk_smp_irq, 1);
#endif
	if (trampoline_page) {
		free_pages((unsigned long)pfn_to_kaddr(page_to_pfn(trampoline_page)),
		           1);
	}
	else {
		if (!using_linux_trampoline)
			iounmap(trampoline_va);
	}

	if (ident_npages_order) {
		free_pages((unsigned long)ident_page_table_virt,
		           ident_npages_order);
	}
}

#ifdef POSTK_DEBUG_ARCH_DEP_108 /* move arch-depends code. */
#ifdef ENABLE_PERF
int smp_ihk_arch_get_perf_event(struct smp_boot_param *param)
{
	struct x86_pmu *__pmu;
	struct extra_reg *er;
	unsigned long *__hw_cache_event_ids;
	unsigned long *__hw_cache_extra_regs;
	unsigned long *__intel_perfmon_event_map;
	int i, er_cnt = 0;

	__pmu = (struct x86_pmu *)kallsyms_lookup_name("x86_pmu");
	__hw_cache_event_ids = (unsigned long *)kallsyms_lookup_name("hw_cache_event_ids");
	__hw_cache_extra_regs = (unsigned long *)kallsyms_lookup_name("hw_cache_extra_regs");
	__intel_perfmon_event_map = (unsigned long *)kallsyms_lookup_name("intel_perfmon_event_map");

	if (__pmu->extra_regs) {
		er = __pmu->extra_regs;
		for (i = 0; er->msr; er++) {
			param->ereg_event[i] = er->event;
			param->ereg_msr[i] = er->msr;
			param->ereg_valid_mask[i] = er->valid_mask;
			param->ereg_idx[i] = er->idx;
			er_cnt++;
			i++;
		}
	}

	if (er_cnt > PERF_EXTRA_REG_MAX) {
		printk("IHK: number os extra_reg is too many .\n");
		return -EINVAL;
	}
	param->nr_extra_regs = er_cnt;
	memcpy(param->hw_event_map, __intel_perfmon_event_map, sizeof(param->hw_event_map));
	memcpy(param->hw_cache_event_ids, __hw_cache_event_ids, sizeof(param->hw_cache_event_ids));
	memcpy(param->hw_cache_extra_regs, __hw_cache_extra_regs, sizeof(param->hw_cache_extra_regs));

	return 0;
}
#else /* ENABLE_PERF */
int smp_ihk_arch_get_perf_event(struct smp_boot_param *param)
{
	return 0;
}
#endif /* ENABLE_PERF */
#endif /* POSTK_DEBUG_ARCH_DEP_108 */

#ifdef POSTK_DEBUG_ARCH_DEP_113 /* Separation of architecture dependent code. */
void ihk_smp_free_page_tables(pgd_t *pt)
{
	pgd_t *pgd;
	pud_t *pud;
	pmd_t *pmd;
	int pgd_i, pud_i, pmd_i;

	if (!pt)
		return;

	for (pgd_i = 0; pgd_i < PTRS_PER_PGD; ++pgd_i) {
		pgd = ((pgd_t *)pt) + pgd_i;
		if (pgd_none(*pgd) || !pgd_present(*pgd))
			continue;

		for (pud_i = 0; pud_i < PTRS_PER_PUD; ++pud_i) {
			pud = ((pud_t *)pgd_page_vaddr(*pgd)) + pud_i;

			if (pud_none(*pud) || !pud_present(*pud))
				continue;

			for (pmd_i = 0; pmd_i < PTRS_PER_PMD; ++pmd_i) {
				pmd = ((pmd_t *)pud_page_vaddr(*pud)) + pmd_i;

				if (pmd_none(*pmd) || !pmd_present(*pmd))
					continue;

				if (pmd_large(*pmd))
					continue;

				dprintk("%s: freeing PGD %d: PUD %d: PMD %d\n",
					__FUNCTION__, pgd_i, pud_i, pmd_i);
				free_page(pmd_page_vaddr(*pmd));
			}

			dprintk("%s: freeing PGD %d: PUD %d: PMD @ 0x%lx\n",
					__FUNCTION__, pgd_i, pud_i, pud_page_vaddr(*pud));
			free_page(pud_page_vaddr(*pud));
		}

		dprintk("%s: freeing PGD %d\n",
				__FUNCTION__, pgd_i);
		free_page(pgd_page_vaddr(*pgd));
	}

	free_page((unsigned long)pt);
}

int ihk_smp_map_kernel(pgd_t *pt, unsigned long vaddr, phys_addr_t paddr)
{
	pgd_t *pgd;
	pud_t *pud;
	pmd_t *pmd;
	pte_t *pte;
	int result = -ENOMEM;

	pgd = pt + pgd_index(vaddr);
	if (!pgd_present(*pgd)) {
		pud = (pud_t *)get_zeroed_page(GFP_KERNEL | GFP_DMA32);
		if (!pud)
			goto err;
		dprintk("%s: PGD: %d: PUD allocated: 0x%lx\n",
				__FUNCTION__,
				(int)pgd_index(vaddr),
				(unsigned long)pgd);
		set_pgd(pgd, __pgd(__pa(pud) | _KERNPG_TABLE));
	}
	
	pud = pud_offset(pgd, vaddr);
	if (!pud_present(*pud)) {
		pmd = (pmd_t *)get_zeroed_page(GFP_KERNEL | GFP_DMA32);
		if (!pmd)
			goto err;
		set_pud(pud, __pud(__pa(pmd) | _KERNPG_TABLE));
		dprintk("%s: PGD: %d: PUD: %d: PMD allocated: 0x%lx\n",
				__FUNCTION__,
				(int)pgd_index(vaddr), (int)pud_index(vaddr),
				(unsigned long)pmd);
	}
	pmd = pmd_offset(pud, vaddr);

	if (pmd_present(*pmd) && pmd_large(*pmd)) {
		printk("%s: ERROR: mapping 0x%lx: PMD is busy\n",
			__FUNCTION__, vaddr);
		return -EBUSY;
	}

	/* Large page aligned and no mapping yet? Map it large then */
	if (!pmd_present(*pmd) &&
			!(vaddr & (IHK_SMP_LARGE_PAGE - 1)) &&
			!(paddr & (IHK_SMP_LARGE_PAGE - 1))) {
		set_pmd(pmd, pfn_pmd(paddr >> PAGE_SHIFT, PAGE_KERNEL_LARGE_EXEC));
	}
	else {
		if (!pmd_present(*pmd)) {
			pte = (pte_t *)get_zeroed_page(GFP_KERNEL | GFP_DMA32);
			if (!pte)
				goto err;
			set_pmd(pmd, __pmd(__pa(pte) | _KERNPG_TABLE));
		}
		pte = pte_offset_kernel(pmd, vaddr);
		set_pte(pte, pfn_pte(paddr >> PAGE_SHIFT, PAGE_KERNEL_EXEC));
	}
	return 0;
err:
	return result;
}

int ihk_smp_print_pte(struct mm_struct *mm, unsigned long address)
{
	pgd_t *pgd;
	pud_t *pud;
	pmd_t *pmd;
	pte_t *pte;
	pte_t entry;

	pgd = pgd_offset(mm, address);
	if (!pgd) {
		printk("%s: no PGD for 0x%lx\n", __FUNCTION__, address);
		return VM_FAULT_OOM;
	}
	printk("%s: PGD: 0x%lx\n", __FUNCTION__, (unsigned long)pgd);
	pud = pud_offset(pgd, address);
	if (!pud) {
		printk("%s: no PUD for 0x%lx\n", __FUNCTION__, address);
		return VM_FAULT_OOM;
	}
	printk("%s: PUD: 0x%lx\n", __FUNCTION__, (unsigned long)pud);
	pmd = pmd_offset(pud, address);
	if (!pmd) {
		printk("%s: no PMD for 0x%lx\n", __FUNCTION__, address);
		return VM_FAULT_OOM;
	}
	printk("%s: PMD: 0x%lx\n", __FUNCTION__, (unsigned long)pmd);
	pte = pte_offset_map(pmd, address);
	if (!pte) {
		printk("%s: no PTE for 0x%lx\n", __FUNCTION__, address);
		return VM_FAULT_OOM;
	}
	entry = *pte;

	if (!pte_present(entry)) {
		printk("%s: non-present PTE for 0x%lx\n", __FUNCTION__, address);
		return -1;
	}

	printk("%s: 0x%lx -> 0x%lx\n",
		__FUNCTION__,
		address,
		(unsigned long)(pte_val(entry) & PTE_PFN_MASK));
	return 0;
}
#endif /* !POSTK_DEBUG_ARCH_DEP_113 */

