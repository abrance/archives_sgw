#include "public.h"
#include "mt_log.h"

void header_dump_packed(msg_t *h)
{
    log_debug("---- header dump packed ----");
    log_debug("length: %u, major: %d, minor: %d", be32toh(h->length), h->major, h->minor);
    log_debug("src_type: %d, dst_type: %d", h->src_type, h->dst_type);
    log_debug("src_id: %u, dst_id: %u", be32toh(h->src_id), be32toh(h->dst_id));
    log_debug("trans_id: %llu, sequence: %llu",
           (unsigned long long int)be64toh(h->trans_id),
           (unsigned long long int)be64toh(h->sequence));
    log_debug("command: 0x%x, ack_code: %u", be32toh(h->command), be32toh(h->ack_code));
    log_debug("total: %llu, offset: %llu, count: %u",
           (unsigned long long int)be64toh(h->total),
           (unsigned long long int)be64toh(h->offset),
           be32toh(h->count));
}

void header_dump_unpack(msg_t *h)
{
    log_debug("---- header dump unpack ----");
    log_debug("length: %u, major: %d, minor: %d", h->length, h->major, h->minor);
    log_debug("src_type: %d, dst_type: %d", h->src_type, h->dst_type);
    log_debug("src_id: %u, dst_id: %u", h->src_id, h->dst_id);
    log_debug("trans_id: %llu, sequence: %llu",
           (unsigned long long int)h->trans_id,
           (unsigned long long int)h->sequence);
    log_debug("command: 0x%x, ack_code: %u", h->command, h->ack_code);
    log_debug("total: %llu, offset: %llu, count: %u",
           (unsigned long long int)h->total,
           (unsigned long long int)h->offset,
           h->count);
}

void pr_task_info(task_info_t *t)
{
    char output[4096];
    snprintf(output, sizeof(output),
             "operation: %u, region_id: %u, site_id: %u, app_id: %u, timestamp: %u, "
             "sgw_port: %u, proxy_port: %u, sgw_ip: %d.%d.%d.%d, proxy_ip: %d.%d.%d.%d, sgw_id: %u, proxy_id: %u, "
             "file_len: %lu, file_md5: %s, file_name: %s, metadata_len: %u",
             t->operation, t->region_id, t->site_id, t->app_id, t->timestamp,
             t->sgw_port, t->proxy_port,
             t->sgw_ip & 0xFF, t->sgw_ip & 0xFF00, t->sgw_ip & 0xFF0000, t->sgw_ip & 0xFF000000,
             t->proxy_ip & 0xFF, t->proxy_ip & 0xFF00, t->proxy_ip & 0xFF0000, t->proxy_ip & 0xFF000000,
             t->sgw_id, t->proxy_id,
             t->file_len, t->file_md5, t->file_name, t->metadata_len);
    log_info("%s", output);
}
