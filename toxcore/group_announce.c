#include <wchar.h>
#include "group_announce.h"
#include "util.h"

GC_Announces_List *new_gca_list()
{
    GC_Announces_List *announces_list = calloc(0, sizeof(GC_Announce));

    if (announces_list) {
        // TODO: process?
    }

    return announces_list;
}

void kill_gca(GC_Announces_List *announces_list)
{
    free(announces_list->announces);
    free(announces_list);
    announces_list = NULL;
}

/* Pack number of nodes into data of maxlength length.
 *
 * return length of packed nodes on success.
 * return -1 on failure.
 */
int pack_gca_nodes(uint8_t *data, uint16_t length, const GC_Announce_Node *nodes, uint32_t number)
{
    uint32_t i;
    int packed_length = 0;

    for (i = 0; i < number; ++i) {
        int ipp_size = pack_ip_port(data, length, packed_length, &nodes[i].ip_port);

        if (ipp_size == -1) {
            return -1;
        }

        packed_length += ipp_size;

        if (packed_length + ENC_PUBLIC_KEY > length) {
            return -1;
        }

        memcpy(data + packed_length, nodes[i].public_key, ENC_PUBLIC_KEY);
        packed_length += ENC_PUBLIC_KEY;
    }

    return packed_length;
}

/* Unpack data of length into nodes of size max_num_nodes.
 * Put the length of the data processed in processed_data_len.
 * tcp_enabled sets if TCP nodes are expected (true) or not (false).
 *
 * return number of unpacked nodes on success.
 * return -1 on failure.
 */
int unpack_gca_nodes(GC_Announce_Node *nodes, uint32_t max_num_nodes, uint16_t *processed_data_len,
                     const uint8_t *data, uint16_t length, uint8_t tcp_enabled)
{
    uint32_t num = 0, len_processed = 0;

    while (num < max_num_nodes && len_processed < length) {
        int ipp_size = unpack_ip_port(&nodes[num].ip_port, len_processed, data, length, tcp_enabled);

        if (ipp_size == -1) {
            return -1;
        }

        len_processed += ipp_size;

        if (len_processed + ENC_PUBLIC_KEY > length) {
            return -1;
        }

        memcpy(nodes[num].public_key, data + len_processed, ENC_PUBLIC_KEY);
        len_processed += ENC_PUBLIC_KEY;
        ++num;
    }

    if (processed_data_len) {
        *processed_data_len = len_processed;
    }

    return num;
}

/* Creates a GC_Announce_Node using public_key and your own IP_Port struct
 *
 * Return 0 on success.
 * Return -1 on failure.
 */
int make_self_gca_node(const DHT *dht, GC_Announce_Node *node, const uint8_t *public_key)
{
    if (ipport_self_copy(dht, &node->ip_port) == -1) {
        return -1;
    }

    memcpy(node->public_key, public_key, ENC_PUBLIC_KEY);
    return 0;
}

static GC_Announces* get_announces_by_chat_id(GC_Announces_List *gc_announces_list,  const uint8_t *chat_id)
{
    int i;
    for (i = 0; i < gc_announces_list->announces_count; i++) {
        if (!memcmp(gc_announces_list->announces[i].chat_id, chat_id, ENC_PUBLIC_KEY)) {
            return &gc_announces_list->announces[i];
        }
    }

    return NULL;
}

// TODO: filter own announces!
int get_gc_announces(GC_Announces_List *gc_announces_list, GC_Announce *gc_announces, uint8_t max_nodes,
                     const uint8_t *chat_id, const uint8_t *except_public_key)
{
    if (!gc_announces || !gc_announces_list || !chat_id || !max_nodes) {
        return -1;
    }

    GC_Announces *announces = get_announces_by_chat_id(gc_announces_list, chat_id);
    if (!announces) {
        return 0;
    }

    // TODO: add proper selection
    int gc_announces_count = 0, i;
    for (i = 0; i < announces->index && i < max_nodes; i++) {
        if (!memcmp(except_public_key, &announces->announces[i].peer_public_key, ENC_PUBLIC_KEY)) {
            continue;
        }

        memcpy(&gc_announces[gc_announces_count], &announces->announces[i], sizeof(GC_Announce));
        gc_announces_count++;
    }

    return gc_announces_count;
}

GC_Announce* add_gc_announce(GC_Announces_List *gc_announces_list, const uint8_t *node, const uint8_t *node_pk,
                             const uint8_t *chat_id, const uint8_t *peer_id)
{
    if (!gc_announces_list || !chat_id || !node_pk) {
        return NULL;
    }

    GC_Announces *announces = get_announces_by_chat_id(gc_announces_list, chat_id);
    if (!announces) {
        GC_Announces new_announce;
        new_announce.index = 0;
        gc_announces_list->announces_count++;
        gc_announces_list->announces = realloc(gc_announces_list->announces, gc_announces_list->announces_count * sizeof(GC_Announces));
        if (!gc_announces_list->announces) {
            gc_announces_list->announces_count = 0;

            return NULL;
        }
        announces = &gc_announces_list->announces[gc_announces_list->announces_count - 1];
        memcpy(announces, &new_announce, sizeof(GC_Announces));
    }
    uint64_t index = announces->index % MAX_GCA_SAVED_ANNOUNCES_PER_GC;
    GC_Announce *gc_announce = &announces->announces[index];
    memcpy(&gc_announce->gc_public_key, chat_id, ENC_PUBLIC_KEY);
    memcpy(&gc_announce->node, node, sizeof(Node_format));
    announces->announces[index].timestamp = unix_time();
    announces->index++;
    // TODO; lock
    return gc_announce;
}