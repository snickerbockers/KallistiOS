/* KallistiOS ##version##

   init.c
   Copyright (C) 2003 Megan Potter
   Copyright (C) 2015 Lawrence Sebald
*/

#include <stdio.h>
#include <malloc.h>
#include <string.h>
#include <stdlib.h>
#include <kos/dbgio.h>
#include <arch/arch.h>
#include <arch/irq.h>
#include <arch/memory.h>
#include <arch/rtc.h>
#include <arch/timer.h>
#include <dc/ubc.h>
#include <dc/pvr.h>
#include <dc/vmufs.h>

#include "initall_hdrs.h"

extern int _bss_start, end;

/* ctor/dtor stuff from libgcc. */
#if __GNUC__ == 4
#define _init init
#define _fini fini
#endif

extern void _init(void);
extern void _fini(void);
extern void __verify_newlib_patch();

void (*__kos_init_early_fn)(void) __attribute__((weak,section(".data"))) = NULL;

int main(int argc, char **argv);
uint32 _fs_dclsocket_get_ip(void);

#ifdef _arch_sub_naomi
#define SAR2    ((vuint32 *)0xFFA00020)
#define CHCR2   ((vuint32 *)0xFFA0002C)
#define DMAOR   ((vuint32 *)0xFFA00040)
#endif

/* We have to put this here so we can include plat-specific devices */
dbgio_handler_t * dbgio_handlers[] = {
#ifndef _arch_sub_naomi
    &dbgio_dcload,
    &dbgio_dcls,
    &dbgio_scif,
    &dbgio_null,
    &dbgio_fb
#else
    &dbgio_null,
    &dbgio_fb
#endif
};
int dbgio_handler_cnt = sizeof(dbgio_handlers) / sizeof(dbgio_handler_t *);

/* Auto-init stuff: override with a non-weak symbol if you don't want all of
   this to be linked into your code (and do the same with the
   arch_auto_shutdown function too). */
int  __attribute__((weak)) arch_auto_init(void) {
#ifndef _arch_sub_naomi
    union {
        uint32 ipl;
        uint8 ipb[4];
    } ip;
#endif

    /* Initialize memory management */
    mm_init();

    /* Do this immediately so we can receive exceptions for init code
       and use ints for dbgio receive. */
    irq_init();         /* IRQs */
    irq_disable();          /* Turn on exceptions */

#ifndef _arch_sub_naomi
    if(!(__kos_init_flags & INIT_NO_DCLOAD))
        fs_dcload_init_console();   /* Init dc-load console, if applicable */

    /* Init SCIF for debug stuff (maybe) */
    scif_init();
#endif

    /* Init debug IO */
    dbgio_init();

    /* Print a banner */
    if(__kos_init_flags & INIT_QUIET)
        dbgio_disable();
    else {
        /* PTYs not initialized yet */
        dbgio_write_str("\n--\n");
        dbgio_write_str(kos_get_banner());
    }

    timer_init();           /* Timers */
    hardware_sys_init();        /* DC low-level hardware init */

    /* Initialize our timer */
    timer_ns_enable();
    timer_ms_enable();
    rtc_init();

    /* Threads */
    if(!(__kos_init_flags & INIT_THD_PREEMPT))
        dbglog(DBG_WARNING, "Cooperative threading mode is deprecated. KOS is \
        always in pre-emptive threading mode. \n");

    thd_init();

    nmmgr_init();

    fs_init();          /* VFS */
    fs_pty_init();          /* Pty */
    fs_ramdisk_init();      /* Ramdisk */
    fs_romdisk_init();      /* Romdisk */

/* The arc4random_buf() function used for random & urandom is only
   available in newlib starting with version 2.4.0 */
#if defined(__NEWLIB__) && !(__NEWLIB__ < 2 && __NEWLIB_MINOR__ < 4)
    fs_dev_init();          /* /dev/urandom etc. */
#else
#warning "/dev filesystem is not supported with Newlib < 2.4.0"
#endif

    hardware_periph_init();     /* DC peripheral init */

    if(__kos_romdisk != NULL) {
        fs_romdisk_mount("/rd", __kos_romdisk, 0);
    }

#ifndef _arch_sub_naomi
    if(!(__kos_init_flags & INIT_NO_DCLOAD) && *DCLOADMAGICADDR == DCLOADMAGICVALUE) {
        dbglog(DBG_INFO, "dc-load console support enabled\n");
        fs_dcload_init();
    }

    fs_iso9660_init();
#endif
    vmufs_init();
    fs_vmu_init();

    /* Initialize library handling */
    library_init();

    /* Now comes the optional stuff */
    if(__kos_init_flags & INIT_IRQ) {
        irq_enable();       /* Turn on IRQs */
        maple_wait_scan();  /* Wait for the maple scan to complete */
    }

#ifndef _arch_sub_naomi
    if(__kos_init_flags & INIT_NET) {
        ip.ipl = 0;

        /* Check if the dcload-ip console is up, and if so, disable it,
           otherwise we'll crash when we attempt to bring up the BBA */
        if(!(__kos_init_flags & INIT_NO_DCLOAD) && dcload_type == DCLOAD_TYPE_IP) {
            /* Grab the IP address from dcload before we disable dbgio... */
            ip.ipl = _fs_dclsocket_get_ip();
            dbglog(DBG_INFO, "dc-load says our IP is %d.%d.%d.%d\n", ip.ipb[3],
                   ip.ipb[2], ip.ipb[1], ip.ipb[0]);
            dbgio_disable();
        }

        net_init(ip.ipl);     /* Enable networking (and drivers) */

        if(!(__kos_init_flags & INIT_NO_DCLOAD) && dcload_type == DCLOAD_TYPE_IP) {
            fs_dclsocket_init_console();

            if(!fs_dclsocket_init()) {
                dbgio_dev_select("fs_dclsocket");
                dbgio_enable();
                dbglog(DBG_INFO, "fs_dclsocket console support enabled\n");
            }
        }
    }
#endif

    return 0;
}

void  __attribute__((weak)) arch_auto_shutdown(void) {
#ifndef _arch_sub_naomi
    fs_dclsocket_shutdown();
    net_shutdown();
#endif

    irq_disable();
    snd_shutdown();
    timer_shutdown();
    hardware_shutdown();
    pvr_shutdown();
    library_shutdown();
#ifndef _arch_sub_naomi
    fs_dcload_shutdown();
#endif
    fs_vmu_shutdown();
    vmufs_shutdown();
#ifndef _arch_sub_naomi
    fs_iso9660_shutdown();
#endif
#if defined(__NEWLIB__) && !(__NEWLIB__ < 2 && __NEWLIB_MINOR__ < 4)
    fs_dev_shutdown();
#endif
    fs_ramdisk_shutdown();
    fs_romdisk_shutdown();
    fs_pty_shutdown();
    fs_shutdown();
    thd_shutdown();
    rtc_shutdown();
}

/* This is the entry point inside the C program */
void arch_main(void) {
    uint8 *bss_start = (uint8 *)(&_bss_start);
    uint8 *bss_end = (uint8 *)(&end);
    int rv;

#ifdef _arch_sub_naomi
    /* Ugh. I'm really not sure why we have to set up these DMA registers this
       way on boot, but failing to do so breaks maple... */
    *SAR2 = 0;
    *CHCR2 = 0x1201;
    *DMAOR = 0x8201;
#endif /* _arch_sub_naomi */

    /* Ensure that UBC is not enabled from a previous session */
    ubc_disable_all();

    /* Handle optional callback provided by KOS_INIT_EARLY() */
    if(__kos_init_early_fn)
        __kos_init_early_fn();

    /* Clear out the BSS area */
    memset(bss_start, 0, bss_end - bss_start);

    /* Do auto-init stuff */
    arch_auto_init();

    __verify_newlib_patch();

    /* Run ctors */
    _init();

    /* Call the user's main function */
    rv = main(0, NULL);

    /* Call kernel exit */
    exit(rv);
}

/* Set the exit path (default is RETURN) */
int arch_exit_path = ARCH_EXIT_RETURN;
void arch_set_exit_path(int path) {
    assert(path >= ARCH_EXIT_RETURN && path <= ARCH_EXIT_REBOOT);
    arch_exit_path = path;
}

/* Does the actual shutdown stuff for a proper shutdown */
void arch_shutdown(void) {
    /* Run dtors */
    _fini();

    dbglog(DBG_CRITICAL, "arch: shutting down kernel\n");

    /* Turn off UBC breakpoints, if any */
    ubc_disable_all();

    /* Do auto-shutdown... or use the "light weight" version underneath */
#if 1
    arch_auto_shutdown();
#else
    /* Ensure that interrupts are disabled */
    irq_disable();

    /* Make sure that PVR and Maple are shut down */
    pvr_shutdown();
    maple_shutdown();

    /* Shut down any other hardware things */
    hardware_shutdown();
#endif

    if(__kos_init_flags & INIT_MALLOCSTATS) {
        malloc_stats();
    }

    /* Shut down IRQs */
    irq_shutdown();
}

/* Generic kernel exit point */
void arch_exit(void) {
    /* arch_exit always returns EXIT_SUCCESS (0)
       if return codes are desired then a call to
       newlib's exit() should be used in its place */
    exit(EXIT_SUCCESS);
}

/* Return point from newlib's _exit() (configurable) */
void arch_exit_handler(int ret_code) {
    dbglog(DBG_INFO, "arch: exit return code %d\n", ret_code);

    /* Shut down */
    arch_shutdown();

    switch(arch_exit_path) {
        default:
            dbglog(DBG_CRITICAL, "arch: arch_exit_path has invalid value!\n");
            __fallthrough;
        case ARCH_EXIT_RETURN:
            arch_return(ret_code);
            break;
        case ARCH_EXIT_MENU:
            arch_menu();
            break;
        case ARCH_EXIT_REBOOT:
            arch_reboot();
            break;
    }
}

/* Called to shut down the system and return to the debug handler (if any) */
void arch_return(int ret_code) {
    /* Jump back to the boot loader */
    arch_real_exit(ret_code);
}

/* Called to jump back to the BIOS menu; assumes a normal shutdown is possible */
void arch_menu(void) {
    typedef void (*menufunc)(int) __noreturn;
    menufunc menu;

    /* Jump to the menus */
    dbglog(DBG_CRITICAL, "arch: exiting the system to the BIOS menu\n");
    menu = (menufunc)(*((uint32 *) 0x8c0000e0));
    menu(1);
}

/* Called to shut down non-gracefully; assume the system is in peril
   and don't try to call the dtors */
void arch_abort(void) {
    /* Turn off UBC breakpoints, if any */
    ubc_disable_all();

    dbglog(DBG_CRITICAL, "arch: aborting the system\n");

    /* PVR disable-by-fire */
    PVR_SET(PVR_RESET, PVR_RESET_ALL);
    PVR_SET(PVR_RESET, PVR_RESET_NONE);

    /* Maple disable-by-fire */
    maple_dma_stop();

    /* Sound disable (nothing weird done in here) */
    spu_disable();

    /* Turn off any IRQs */
    irq_disable();

    arch_real_exit(EXIT_FAILURE);
}

/* Called to reboot the system; assume the system is in peril and don't
   try to call the dtors */
void arch_reboot(void) {
    typedef void (*reboot_func)() __noreturn;
    reboot_func rb;

    dbglog(DBG_CRITICAL, "arch: rebooting the system\n");

    /* Ensure that interrupts are disabled */
    irq_disable();

    /* Reboot */
    rb = (reboot_func)(MEM_AREA_P2_BASE | 0x00000000);
    rb();
}
