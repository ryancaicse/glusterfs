/*
  Copyright (c) 2017 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

#include "glusterfs/monitoring.h"
#include "glusterfs/xlator.h"
#include "glusterfs/syscall.h"

#include <stdlib.h>

static void
dump_mem_acct_details(xlator_t *xl, int fd)
{
    struct mem_acct_rec *mem_rec;
    int i = 0;

    if (!xl || !xl->mem_acct || (xl->ctx->active != xl->graph))
        return;

    dprintf(fd, "# %s.%s.total.num_types %d\n", xl->type, xl->name,
            xl->mem_acct->num_types);

    dprintf(fd,
            "# type, in-use-size, in-use-units, max-size, "
            "max-units, total-allocs\n");

    for (i = 0; i < xl->mem_acct->num_types; i++) {
        mem_rec = &xl->mem_acct->rec[i];
        if (!GF_ATOMIC_GET(mem_rec->num_allocs))
            continue;
#ifdef DEBUG
        dprintf(fd, "# %s, %" PRIu64 ", %" PRIu64 ", %u, %" PRIu64 "\n",
                mem_rec->typestr, mem_rec->size, mem_rec->max_size,
                mem_rec->max_num_allocs, GF_ATOMIC_GET(mem_rec->num_allocs));
#else
        dprintf(fd, "# %s, %" PRIu64 ", %" PRIu64 "\n", mem_rec->typestr,
                mem_rec->size, GF_ATOMIC_GET(mem_rec->num_allocs));
#endif
    }
}

static void
dump_latency_and_count(xlator_t *xl, int fd)
{
    int32_t index = 0;
    uint64_t fop = 0;
    uint64_t cbk = 0;
    uint64_t total_fop_count = 0;
    uint64_t interval_fop_count = 0;

    if (xl->winds) {
        dprintf(fd, "%s.total.pending-winds.count %" PRIu64 "\n", xl->name,
                xl->winds);
    }

    /* Need 'fuse' data, and don't need all the old graph info */
    if ((xl != xl->ctx->root) && (xl->ctx->active != xl->graph))
        return;

    for (index = 0; index < GF_FOP_MAXVALUE; index++) {
        fop = GF_ATOMIC_GET(xl->stats[index].total_fop);
        if (fop) {
            dprintf(fd, "%s.total.%s.count %" PRIu64 "\n", xl->name,
                    gf_fop_list[index], fop);
            total_fop_count += fop;
        }
        fop = GF_ATOMIC_SWAP(xl->stats[index].interval_fop, 0);
        if (fop) {
            dprintf(fd, "%s.interval.%s.count %" PRIu64 "\n", xl->name,
                    gf_fop_list[index], fop);
            interval_fop_count += fop;
        }
        cbk = GF_ATOMIC_SWAP(xl->stats[index].interval_fop_cbk, 0);
        if (cbk) {
            dprintf(fd, "%s.interval.%s.fail_count %" PRIu64 "\n", xl->name,
                    gf_fop_list[index], cbk);
        }
        if (xl->stats[index].latencies.count != 0) {
            dprintf(fd, "%s.interval.%s.latency %lf\n", xl->name,
                    gf_fop_list[index],
                    (((double)xl->stats[index].latencies.total) /
                     xl->stats[index].latencies.count));
            dprintf(fd, "%s.interval.%s.max %" PRIu64 "\n", xl->name,
                    gf_fop_list[index], xl->stats[index].latencies.max);
            dprintf(fd, "%s.interval.%s.min %" PRIu64 "\n", xl->name,
                    gf_fop_list[index], xl->stats[index].latencies.min);
        }
        memset(&xl->stats[index].latencies, 0,
               sizeof(xl->stats[index].latencies));
    }

    dprintf(fd, "%s.total.fop-count %" PRIu64 "\n", xl->name, total_fop_count);
    dprintf(fd, "%s.interval.fop-count %" PRIu64 "\n", xl->name,
            interval_fop_count);
}

static inline void
dump_call_stack_details(glusterfs_ctx_t *ctx, int fd)
{
    dprintf(fd, "total.stack.count %" PRIu64 "\n",
            GF_ATOMIC_GET(ctx->pool->total_count));
    dprintf(fd, "total.stack.in-flight %" PRIu64 "\n", ctx->pool->cnt);
}

static inline void
dump_dict_details(glusterfs_ctx_t *ctx, int fd)
{
    uint64_t total_dicts = 0;
    uint64_t total_pairs = 0;

    total_dicts = GF_ATOMIC_GET(ctx->stats.total_dicts_used);
    total_pairs = GF_ATOMIC_GET(ctx->stats.total_pairs_used);

    dprintf(fd, "total.dict.max-pairs-per %" PRIu64 "\n",
            GF_ATOMIC_GET(ctx->stats.max_dict_pairs));
    dprintf(fd, "total.dict.pairs-used %" PRIu64 "\n", total_pairs);
    dprintf(fd, "total.dict.used %" PRIu64 "\n", total_dicts);
    dprintf(fd, "total.dict.average-pairs %" PRIu64 "\n",
            (total_pairs / total_dicts));
}

static void
dump_inode_stats(glusterfs_ctx_t *ctx, int fd)
{
}

static void
dump_global_metrics(glusterfs_ctx_t *ctx, int fd)
{
    struct timeval tv;
    time_t nowtime;
    struct tm *nowtm;
    char tmbuf[64] = {
        0,
    };

    gettimeofday(&tv, NULL);
    nowtime = tv.tv_sec;
    nowtm = localtime(&nowtime);
    strftime(tmbuf, sizeof tmbuf, "%Y-%m-%d %H:%M:%S", nowtm);

    /* Let every file have information on which process dumped info */
    dprintf(fd, "## %s\n", ctx->cmdlinestr);
    dprintf(fd, "### %s\n", tmbuf);
    dprintf(fd, "### BrickName: %s\n", ctx->cmd_args.brick_name);
    dprintf(fd, "### MountName: %s\n", ctx->cmd_args.mount_point);
    dprintf(fd, "### VolumeName: %s\n", ctx->cmd_args.volume_name);

    dump_call_stack_details(ctx, fd);
    dump_dict_details(ctx, fd);
    dprintf(fd, "# -----\n");

    dump_inode_stats(ctx, fd);
    dprintf(fd, "# -----\n");
}

static void
dump_xl_metrics(glusterfs_ctx_t *ctx, int fd)
{
    xlator_t *xl;

    xl = ctx->active->top;

    while (xl) {
        dump_latency_and_count(xl, fd);
        dump_mem_acct_details(xl, fd);
        if (xl->dump_metrics)
            xl->dump_metrics(xl, fd);
        xl = xl->next;
    }

    if (ctx->root) {
        xl = ctx->root;

        dump_latency_and_count(xl, fd);
        dump_mem_acct_details(xl, fd);
        if (xl->dump_metrics)
            xl->dump_metrics(xl, fd);
    }

    return;
}

char *
gf_monitor_metrics(glusterfs_ctx_t *ctx)
{
    int ret = -1;
    int fd = 0;
    char *filepath = NULL, *dumppath = NULL;

    gf_msg_trace("monitoring", 0, "received monitoring request (sig:USR2)");

    dumppath = ctx->config.metrics_dumppath;
    if (dumppath == NULL) {
        dumppath = GLUSTER_METRICS_DIR;
    }
    ret = mkdir_p(dumppath, 0755, true);
    if (ret) {
        /* EEXIST is handled in mkdir_p() itself */
        gf_msg("monitoring", GF_LOG_ERROR, 0, LG_MSG_STRDUP_ERROR,
               "failed to create metrics dir %s (%s)", dumppath,
               strerror(errno));
        return NULL;
    }

    ret = gf_asprintf(&filepath, "%s/gmetrics.XXXXXX", dumppath);
    if (ret < 0) {
        return NULL;
    }

    /* coverity[secure_temp] mkstemp uses 0600 as the mode and is safe */
    fd = mkstemp(filepath);
    if (fd < 0) {
        gf_msg("monitoring", GF_LOG_ERROR, 0, LG_MSG_STRDUP_ERROR,
               "failed to open tmp file %s (%s)", filepath, strerror(errno));
        GF_FREE(filepath);
        return NULL;
    }

    dump_global_metrics(ctx, fd);

    dump_xl_metrics(ctx, fd);

    /* This below line is used just to capture any errors with dprintf() */
    ret = dprintf(fd, "\n# End of metrics\n");
    if (ret < 0) {
        gf_msg("monitoring", GF_LOG_WARNING, 0, LG_MSG_STRDUP_ERROR,
               "dprintf() failed: %s", strerror(errno));
    }

    ret = sys_fsync(fd);
    if (ret < 0) {
        gf_msg("monitoring", GF_LOG_WARNING, 0, LG_MSG_STRDUP_ERROR,
               "fsync() failed: %s", strerror(errno));
    }
    sys_close(fd);

    /* Figure this out, not happy with returning this string */
    return filepath;
}
