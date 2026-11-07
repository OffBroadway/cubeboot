/* Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-2.0-only */
#ifndef CBVMC_RES_H
#define CBVMC_RES_H

/*
 * Result block of the Dolphin test image: big-endian 32-bit words that the harness watches (MemoryWatcher) and reads from a RAM
 * dump.  One struct, one X-macro list so that the build script and the report derive the word names from this header.
 */
#include <stdint.h>

#define CBVMC_RES_MAGIC 0x43425631u     /* 'CBV1' */

#define CBVMC_RES_FIELDS(X) \
    X(magic) X(version) \
    X(install_stage) X(install_status) X(install_ticks) X(words_status) X(words_bad) X(words_applied) \
    X(vec_before) X(vec_after) X(fmt_status) X(fmt_ticks) X(free_blocks_fmt) X(image_addr) X(blob_bytes) \
    X(drv_stage) X(drv_status) X(drv_last) X(exi_probe) X(exi_state) X(exi_id_rc) X(exi_id) X(raw_rc) X(raw_cmp) X(raw_w0) X(raw_w1) X(raw_w2) X(unc_w0) X(unc_w1) X(unc_w2) X(unc_w3) \
    X(probe_res) X(probe_mem) X(probe_sector) X(mount_res) X(free_bytes) X(free_files) \
    X(create1_res) X(open1_res) X(write1_res) X(close1_res) X(create2_res) X(open2_res) X(write2_res) X(close2_res) \
    X(free_bytes_after) X(free_files_after) X(nofile_res) X(unmount_res) X(remount_res) \
    X(reopen1_res) X(reopen2_res) X(read1_res) X(read2_res) X(cmp1) X(cmp2) X(stat_len1) X(stat_len2) \
    X(t_probe) X(t_mount) X(t_create) X(t_write) X(t_read) X(t_remount) X(t_total) \
    X(st_traps) X(st_exi_virtual) X(st_exi_forwarded) X(st_pi_reads) X(st_pi_irq_merged) X(st_other_forwarded) \
    X(st_bad_insn) X(st_last_bad_insn) X(st_last_bad_pc) X(st_dma_bad) X(ram_reads) X(ram_writes) \
    X(lm_34) X(lm_38) X(lm_3c) X(lm_e8) X(lm_ec) X(lm_f4) X(b_nonzero) X(r_text_ok) X(sim_loader_rc) X(sim_fst_old) X(sim_fst_new) X(sim_bi2_new) X(sim_r_size) X(font_encode) X(card_encoding) X(sim_slot_a) X(sim_slot_a_id) X(sim_slot_b) X(sim_slot_b_id) X(slots_live) X(free_blocks_b) \
    X(b_status) X(b_probe) X(b_mount) X(b_create) X(b_write) X(b_remount) X(b_read) X(b_cmp) X(b_cross) X(a_after_b) X(b_exi_id) X(b_traps) \
    X(sim_scan_ok) X(sim_scan_hook) X(sim_scan_words) \
    X(ipl_status) X(ipl_live) X(ipl_detail) X(ipl_vec_before) X(ipl_vec_after) X(ipl_exi_tries) X(ipl_hash_before) X(ipl_hash_after) X(ipl_mount_res) X(ipl_free_bytes) \
    X(ipl_open_res) X(ipl_read_res) X(ipl_read_cmp) X(ipl_create_res) X(ipl_write_res) X(ipl_refused_w) X(ipl_refused_o) X(ipl_reads) X(ipl_ticks) X(ipl_args_addr) X(ipl_close_res) X(ipl_golden_len) \
    X(t2_armed) X(t2_dabr) X(t2_rp_status) X(t2_rp_mode) X(t2_rp_words) X(t2_rp_why) X(t2_rp_ticks) X(t2_rp_srr0_ok) X(t2_rp_fixp) X(t2_rp_fixp_after) X(t2_rp_repaired) \
    X(t2_rp_mount) X(t2_rp_read) X(t2_rp_cmp) \
    X(done)

struct cbvmc_res_s {
#define F(n) uint32_t n;
    CBVMC_RES_FIELDS(F)
#undef F
};

extern struct cbvmc_res_s cbvmc_res;

/* install_status */
#define CBVMC_INST_OK        0u
#define CBVMC_INST_NOTRUN    0xFFFFFFFFu
#define CBVMC_INST_ERR_VEC   1u     /* the DSI stub is not the SDK's: nothing installed */
#define CBVMC_INST_ERR_WORDS 2u     /* the word table does not match this DOL: nothing installed */
#define CBVMC_INST_ERR_BIND  3u
#define CBVMC_INST_ERR_CARD  4u     /* the RAM card could not be formatted/opened */

/* drv_status */
#define CBVMC_DRV_NOTRUN  0u
#define CBVMC_DRV_RUNNING 1u
#define CBVMC_DRV_PASS    2u
#define CBVMC_DRV_FAIL    0x100u    /* + the stage that failed */

#endif
