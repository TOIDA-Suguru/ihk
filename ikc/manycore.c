/**
 * \file ikc/manycore.c
 * \brief IHK-IKC: Wrapper functions in IHK-Manycore for IHK-IKC
 *
 * Copyright (C) 2011-2012 Taku Shimosawa <shimosawa@is.s.u-tokyo.ac.jp>
 */
#include <ikc/ihk.h>
#include <ikc/queue.h>
#include <ikc/master.h>

struct ihk_ikc_channel_desc *ihk_mc_get_master_channel(void);

static ihk_spinlock_t *ihk_ikc_channels_lock;
static struct list_head *ihk_ikc_channels;

/* Comment: McKernel側の割り込み処理channel_list */
static ihk_spinlock_t *intr_list_lock;
static struct list_head *intr_list;

struct list_head *ihk_ikc_get_channel_list(ihk_os_t os)
{
	return &ihk_ikc_channels[ihk_mc_get_processor_id()];
}
ihk_spinlock_t *ihk_ikc_get_channel_list_lock(ihk_os_t os)
{
	return &ihk_ikc_channels_lock[ihk_mc_get_processor_id()];
}

/* Comment: 割り込み処理channel_list と lockの取得関数 (McKernel側) */
struct list_head *ihk_ikc_get_intr_list(ihk_os_t os, int cpu)
{
	return &intr_list[cpu];
}
ihk_spinlock_t *ihk_ikc_get_intr_list_lock(ihk_os_t os, int cpu)
{
	return &intr_list_lock[cpu];
}

static void ihk_ikc_interrupt_handler(void *priv)
{
/* Comment: 自CPUで割り込み処理が必要なchannelのリストを取得し、
   それぞれのchannelにパケットが届いていれば処理する */
	/* This should be done in the software irq... */
	struct ihk_ikc_channel_desc *c;
	struct list_head *intr_list = ihk_ikc_get_intr_list(NULL, ihk_mc_get_processor_id());
	ihk_spinlock_t *intr_list_lock = ihk_ikc_get_intr_list_lock(NULL, ihk_mc_get_processor_id());
	unsigned long flags;

	flags = ihk_ikc_spinlock_lock(intr_list_lock);
	list_for_each_entry(c, intr_list, list_intr) {
		while (ihk_ikc_channel_enabled(c) &&
		    !ihk_ikc_queue_is_empty(c->recv.queue) &&
		    c->recv.queue->read_cpu == ihk_mc_get_processor_id()) {
			ihk_ikc_spinlock_unlock(intr_list_lock, flags);
			ihk_ikc_recv_handler(c, c->handler, NULL, 0);
			flags = ihk_ikc_spinlock_lock(intr_list_lock);
		}
	}
	ihk_ikc_spinlock_unlock(intr_list_lock, flags);
}

int ihk_ikc_send(struct ihk_ikc_channel_desc *channel, void *p, int opt)
{
/* Comment: master_channel での送信通知を廃止
   通信を行うchannelの宛先CPUに直接割り込みを行う */
	int r;
	unsigned long flags;

	if(!channel || !p)
		return -EINVAL;

	flags = cpu_disable_interrupt_save();

retry:
	/* Add main packet to target channel */
	if (ihk_ikc_channel_enabled(channel)) {
		r = ihk_ikc_write_queue(channel->send.queue, p, opt);

		if (r != 0) {
			kprintf("%s: couldn't append packet -> retrying\n", __FUNCTION__);
			goto retry;
		}

		if (!(opt & IKC_NO_NOTIFY)) {
			ihk_ikc_notify_remote_write(channel);
		}
	} else {
		r = -EINVAL;
	}

	cpu_restore_interrupt(flags);

	return r;
}

struct ihk_ikc_channel_desc *ihk_ikc_get_master_channel(ihk_os_t os)
{
	return ihk_mc_get_master_channel();
}

static struct ihk_mc_interrupt_handler ihk_ikc_handler = {
	.func = ihk_ikc_interrupt_handler,
	.priv = NULL,
};

extern int num_processors;

void ihk_ikc_system_init(ihk_os_t os)
{
	int i;
	INIT_LIST_HEAD(&ihk_ikc_handler.list);
	ihk_mc_register_interrupt_handler(ihk_mc_get_vector(IHK_GV_IKC),
	                                  &ihk_ikc_handler);

	ihk_ikc_channels = ihk_ikc_malloc(sizeof(*ihk_ikc_channels) * num_processors);
	ihk_ikc_channels_lock = ihk_ikc_malloc(sizeof(*ihk_ikc_channels_lock) * num_processors);

/* Comment: 割り込み処理channel_list のinit */
	intr_list = ihk_ikc_malloc(sizeof(*intr_list) * num_processors);
	intr_list_lock = ihk_ikc_malloc(sizeof(*intr_list_lock) * num_processors);

	if (!ihk_ikc_channels || !ihk_ikc_channels_lock || !intr_list || !intr_list_lock) {
		kprintf("%s: error allocating channels list\n", __FUNCTION__);
		panic("");
	}

	for (i = 0; i < num_processors; ++i) {
		INIT_LIST_HEAD(&ihk_ikc_channels[i]);
		ihk_ikc_spinlock_init(&ihk_ikc_channels_lock[i]);
		INIT_LIST_HEAD(&intr_list[i]);
		ihk_ikc_spinlock_init(&intr_list_lock[i]);
	}
}

void ihk_ikc_system_exit(ihk_os_t os)
{
	ihk_mc_unregister_interrupt_handler(ihk_mc_get_vector(IHK_GV_IKC),
	                                    &ihk_ikc_handler);
}

struct ihk_ikc_queue_head *ihk_ikc_alloc_queue(int qpages)
{
	return ihk_mc_alloc_pages(qpages, 0);
}

void ihk_ikc_free_queue(struct ihk_ikc_queue_head *q)
{
	ihk_mc_free_pages(q, (sizeof(struct ihk_ikc_queue_head) + 
	                      q->queue_size + PAGE_SIZE - 1) >> PAGE_SHIFT);
}

void *ihk_ikc_malloc(int size)
{
	return ihk_mc_allocate(size, 0);
}
void ihk_ikc_free(void *p)
{
	return ihk_mc_free(p);
}

extern ihk_ikc_ph_t arch_master_channel_packet_handler;

int call_arch_master_packet_handler(void *os, struct ihk_ikc_channel_desc *c,
                                    void *__packet)
{
	return arch_master_channel_packet_handler(c, __packet, os);
}

static LIST_HEAD(wait_list);
static ihk_spinlock_t wait_lock;

struct list_head *ihk_ikc_get_master_wait_list(ihk_os_t ihk_os)
{
	return &wait_list;
}

ihk_spinlock_t *ihk_ikc_get_master_wait_lock(ihk_os_t ihk_os)
{
	return &wait_lock;
}

void ihk_ikc_wait_init(ihk_wait_t *wait)
{
}

int ihk_ikc_wait_master(struct ihk_ikc_master_wait_struct *ws)
{
	/* XXX: SPINNING! */
	while (!ws->status) {
		cpu_pause();
		barrier();
	}
	return 0;
}

void ihk_ikc_wake_master(struct ihk_ikc_master_wait_struct *ws)
{
	ws->status = 1;
}

static ihk_atomic_t channel_id;

int ihk_ikc_get_unique_channel_id(ihk_os_t ihk_os)
{
	return ihk_atomic_inc_return(&channel_id);
}
