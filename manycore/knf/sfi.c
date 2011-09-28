#include <aal/debug.h>
#include <aal/types.h>
#include <aal/mm.h>
#include <errno.h>
#include <string.h>

struct sfi_table_header {
	char    sig[4];
	uint32_t len;
	uint8_t  rev;
	uint8_t  csum;
	char    oem_id[6];
	char    oem_table_id[8];
} __attribute__((packed));

struct sfi_table_simple {
	struct sfi_table_header     header;
	uint64_t             pentry[1];
} __attribute__((packed));
  
struct sfi_mem_entry {
	uint32_t type;
	uint64_t phys_start;
	uint64_t virt_start;
	uint64_t pages;
	uint64_t attrib;
} __attribute__((packed));

#define SFI_SYST_SEARCH_BEGIN       0x000E0000
#define SFI_SYST_SEARCH_END         0x000FFFFF

static struct sfi_table_simple *sfi_table;
static struct sfi_table_header *sfi_cpus, *sfi_mems;

static int __sfi_table_num_entry(struct sfi_table_header *hdr, size_t s)
{
	return (int)((hdr->len - sizeof(*hdr)) / s);
}

static void parse_sfi_cpus(struct sfi_table_header *hdr)
{
	int i, nentry;
	uint32_t *apicids;

	nentry = __sfi_table_num_entry(hdr, sizeof(uint32_t));
	kprintf("# of CPUs : %d\n", nentry);

	apicids = (uint32_t *)(hdr + 1);
	for (i = 0; i < nentry; i++) {
		kprintf("%d,", apicids[i]);
	}
	kprintf("\n");
}

static unsigned long sfi_mem_begin = (unsigned long)-1, sfi_mem_end = 0;

static void parse_sfi_mems(struct sfi_table_header *hdr)
{
	int i, nentry;
	struct sfi_mem_entry *me;
	unsigned long end;

	nentry = __sfi_table_num_entry(hdr, 36);
	kprintf("# of MEMs : %d\n", nentry);
	
	me = (struct sfi_mem_entry *)(hdr + 1);
	for (i = 0; i < nentry; i++) {
		if (me[i].type == 7) { /* CONVENTIONAL */
			if (sfi_mem_begin > me[i].phys_start) {
				sfi_mem_begin = me[i].phys_start;
			}
			end = me[i].phys_start + (me[i].pages << 12);
			if (sfi_mem_end < end) {
				sfi_mem_end = end;
			}
		}
	}
}

void init_sfi(void)
{
	unsigned long addr;
	int i, nentry;

	for (addr = SFI_SYST_SEARCH_BEGIN; addr < SFI_SYST_SEARCH_END;
	     addr += 16) {
		if (!strncmp((char *)addr, "SYST", 4)) {
			sfi_table = (struct sfi_table_simple *)addr;
			break;
		}
	}
	if (!addr) {
		return;
	}

	nentry = (sfi_table->header.len - sizeof(sfi_table->header))
		/ sizeof(unsigned long long);
	for (i = 0; i < nentry; i++) {
		struct sfi_table_header *hdr;
		hdr = (struct sfi_table_header *)(sfi_table->pentry[i]);
		kprintf("%c%c%c%c @ %lx\n",
		        hdr->sig[0], hdr->sig[1], hdr->sig[2], hdr->sig[3],
		        sfi_table->pentry[i]);
		if (!strncmp(hdr->sig, "CPUS", 4)) {
			sfi_cpus = early_alloc_page();
			memcpy(sfi_cpus, hdr, hdr->len);

			parse_sfi_cpus(sfi_cpus);
		} else if (!strncmp(hdr->sig, "MMAP", 4)) {
			sfi_mems = early_alloc_page();
			memcpy(sfi_mems, hdr, hdr->len);

			parse_sfi_mems(sfi_mems);
		}
	}
}

unsigned long sfi_get_memory_address(enum aal_mc_gma_type type, int opt)
{
	switch (type) {
	case AAL_MC_GMA_MAP_START:
	case AAL_MC_GMA_AVAIL_START:
		return sfi_mem_begin;
	case AAL_MC_GMA_MAP_END:
	case AAL_MC_GMA_AVAIL_END:
		return sfi_mem_end;
	default:
		break;
	}

	return -ENOENT;
}

void __reserve_arch_pages(unsigned long start, unsigned long end,
                          void (*cb)(unsigned long, unsigned long, int))
{

}
