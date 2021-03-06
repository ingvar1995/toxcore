#include <wchar.h>
#include "group_announce.h"
#include "util.h"


static void remove_announces(GC_Announces_List *gc_announces_list, GC_Announces *announces) {
    if (announces->prev_announce) {
        announces->prev_announce->next_announce = announces->next_announce;
    } else {
        gc_announces_list->announces = announces->next_announce;
    }

    if (announces->next_announce) {
        announces->next_announce->prev_announce = announces->prev_announce;
    }

    free(announces);
    gc_announces_list->announces_count--;
}

GC_Announces_List *new_gca_list()
{
    GC_Announces_List *announces_list = malloc(sizeof(GC_Announces_List));

    if (announces_list) {
        announces_list->announces_count = 0;
        announces_list->announces = NULL;
    }

    return announces_list;
}

void kill_gca(GC_Announces_List *announces_list)
{
    while (announces_list->announces) {
        remove_announces(announces_list, announces_list->announces);
    }
    free(announces_list);
    announces_list = NULL;
}

void do_gca(GC_Announces_List *gc_announces_list) {
    if (!gc_announces_list) {
        return;
    }

    GC_Announces *announces = gc_announces_list->announces;
    while (announces) {
        if (announces->last_announce_received_timestamp <= unix_time() - GC_ANNOUNCE_SAVING_TIMEOUT) {
            GC_Announces *announces_to_delete = announces;
            announces = announces->next_announce;
            remove_announces(gc_announces_list, announces_to_delete);

            continue;
        }
        announces = announces->next_announce;
    }
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

static GC_Announces* get_announces_by_chat_id(const GC_Announces_List *gc_announces_list,  const uint8_t *chat_id)
{
    GC_Announces *announces = gc_announces_list->announces;
    while (announces) {
        if (!memcmp(announces->chat_id, chat_id, CHAT_ID_SIZE)) {
            return announces;
        }
        announces = announces->next_announce;
    }

    return NULL;
}

bool cleanup_gca(GC_Announces_List *gc_announces_list, const uint8_t *chat_id) {
    if (!gc_announces_list || !chat_id) {
        return false;
    }

    GC_Announces *announces = get_announces_by_chat_id(gc_announces_list, chat_id);
    if (announces) {
        remove_announces(gc_announces_list, announces);

        return true;
    }

    return false;
}

int get_gc_announces(GC_Announces_List *gc_announces_list, GC_Peer_Announce *gc_announces, uint8_t max_nodes,
                     const uint8_t *chat_id, const uint8_t *except_public_key)
{
    if (!gc_announces || !gc_announces_list || !chat_id || !max_nodes || !except_public_key) {
        return -1;
    }

    GC_Announces *announces = get_announces_by_chat_id(gc_announces_list, chat_id);
    if (!announces) {
        return 0;
    }

    // TODO: add proper selection
    int gc_announces_count = 0, i, j;
    for (i = 0; i < announces->index && i < MAX_GCA_SAVED_ANNOUNCES_PER_GC && gc_announces_count < max_nodes; i++) {
        int index = i % MAX_GCA_SAVED_ANNOUNCES_PER_GC;
        if (!memcmp(except_public_key, &announces->announces[index].peer_public_key, ENC_PUBLIC_KEY)) {
            continue;
        }

        bool already_added = false;
        for (j = 0; j < gc_announces_count; j++) {
            if (!memcmp(&gc_announces[j].peer_public_key,
                        &announces->announces[index].peer_public_key,
                        ENC_PUBLIC_KEY)) {
                already_added = true;
                break;
            }
        }

        if (!already_added) {
            memcpy(&gc_announces[gc_announces_count], &announces->announces[index], sizeof(GC_Peer_Announce));
            gc_announces_count++;
        }
    }

    return gc_announces_count;
}

GC_Peer_Announce* add_gc_announce(GC_Announces_List *gc_announces_list, const Node_format *node,
                                  const uint8_t *chat_id, const uint8_t *peer_id)
{
    if (!gc_announces_list || !node || !chat_id || !peer_id) {
        return NULL;
    }

    GC_Announces *announces = get_announces_by_chat_id(gc_announces_list, chat_id);
    if (!announces) {
        gc_announces_list->announces_count++;
        announces = malloc(sizeof(GC_Announces));
        announces->index = 0;
        announces->prev_announce = NULL;
        if (gc_announces_list->announces) {
            gc_announces_list->announces->prev_announce = announces;
        }
        announces->next_announce = gc_announces_list->announces;
        gc_announces_list->announces = announces;
        memcpy(announces->chat_id, chat_id, CHAT_ID_SIZE);
    }
    uint64_t index = announces->index % MAX_GCA_SAVED_ANNOUNCES_PER_GC;
    announces->last_announce_received_timestamp = unix_time();
    GC_Peer_Announce *gc_peer_announce = &announces->announces[index];
    memcpy(&gc_peer_announce->peer_public_key, peer_id, ENC_PUBLIC_KEY);
    memcpy(&gc_peer_announce->node, node, sizeof(Node_format));
    gc_peer_announce->timestamp = unix_time();
    announces->index++;
    // TODO; lock
    return gc_peer_announce;
}

GC_Peer_Announce* add_self_announce(GC_Announces_List *gc_announces_list, const uint8_t *chat_id, Node_format *node)
{
    return add_gc_announce(gc_announces_list, node, chat_id, 0);
}
