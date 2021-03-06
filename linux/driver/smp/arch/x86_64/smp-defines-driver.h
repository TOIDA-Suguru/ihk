/* smp-defines-driver.h COPYRIGHT FUJITSU LIMITED 2015 */
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
#ifndef HEADER_SMP_SMP_DEFINES_DRIVER_H
#define HEADER_SMP_SMP_DEFINES_DRIVER_H

#define IHK_SMP_LARGE_PAGE_SHIFT	21

/*
 * MAP_KERNEL_START is 8MB below MODULES_END.
 * We map the LWK image at the end of the modules section
 * so that Linux can call into LWK text.
 */
#define IHK_SMP_MAP_KERNEL_START   0xFFFFFFFFFE800000UL

#define IHK_SMP_CHUNK_BASE_SIZE	4194304

#endif /* HEADER_SMP_SMP_DEFINES_DRIVER_H */
