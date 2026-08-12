/* SPDX-License-Identifier: MIT */

#ifndef TPW_PW_CORE_INTERNAL_H
#define TPW_PW_CORE_INTERNAL_H

#include <stdbool.h>

#include <pipewire/pipewire.h>

/* Shared PipeWire thread-loop/context/core connection, reused by both
 * tpw_stream and tpw_filter so each owns a fully independent PipeWire
 * client without duplicating the setup/teardown logic. */
struct tpw_pw_core_conn {
    struct pw_thread_loop* loop;
    struct pw_context* context;
    struct pw_core* core;

    struct spa_hook core_listener;
    int pending_seq;
    int connect_result;
    bool sync_done;
};

/* One PipeWire node global seen through the registry. */
struct tpw_pw_node_entry {
    uint32_t id;
    char* name;
    uint64_t serial;
};

/* One PipeWire port global seen through the registry. */
struct tpw_pw_port_entry {
    uint32_t id;
    uint32_t node_id;
    char* name;
    enum spa_direction direction;
    /* The port's index within its node, numbered per direction. This is what
     * pairs a stream's channels to a device's: an unnegotiated stream's ports
     * carry no channel identity to match on. */
    uint32_t ordinal;
};

/* A live view of the graph's node and port globals, kept current by a
 * registry listener. Bound lazily so a client that never needs it pays
 * nothing; zero-initialized means "not bound yet". */
struct tpw_pw_registry {
    struct pw_registry* registry;
    struct spa_hook listener;

    struct tpw_pw_node_entry* nodes;
    size_t n_nodes;
    size_t nodes_capacity;

    struct tpw_pw_port_entry* ports;
    size_t n_ports;
    size_t ports_capacity;

    /* Set when a node this client cares about disappears; the owner reads
     * and clears it. Kept generic: the registry itself has no idea what a
     * link is. */
    void (*node_removed_cb)(void* data, uint32_t id);
    void* node_removed_data;

    /* Fired as each port global is cached, so an owner waiting for its own
     * ports can wake instead of polling. Same generic shape as above. */
    void (*port_added_cb)(void* data, uint32_t node_id);
    void* port_added_data;
};

/* Binds `reg` to `conn`'s core and waits (bounded) for the initial burst
 * of globals to arrive, so a lookup right after this call sees the graph
 * as it currently is. A no-op returning 0 if already bound. Returns 0 on
 * success, a negative error code otherwise. */
int tpw_pw_registry_bind(struct tpw_pw_registry* reg, struct tpw_pw_core_conn* conn);

/* Waits for one core round-trip so any globals created since the last
 * call have been delivered. Use when an object is expected to appear
 * shortly (objects show up asynchronously after the call that creates
 * them). Returns 0 on success, negative on timeout. */
int tpw_pw_registry_sync(struct tpw_pw_registry* reg, struct tpw_pw_core_conn* conn);

/* Removes the listener, destroys the registry proxy, and frees both
 * caches. Safe on an unbound or already-torn-down registry. Must be
 * called with the thread loop locked, or after it has been stopped. */
void tpw_pw_registry_teardown(struct tpw_pw_registry* reg);

/* Finds a node by exact name, or 0 if there is no such node. */
uint32_t tpw_pw_registry_find_node_by_name(const struct tpw_pw_registry* reg, const char* name);

/* Finds a node by object.serial, or 0 if there is no such node. */
uint32_t tpw_pw_registry_find_node_by_serial(const struct tpw_pw_registry* reg, uint64_t serial);

/* Finds a port by owning node id, exact name, and direction; 0 if none. */
uint32_t tpw_pw_registry_find_port(const struct tpw_pw_registry* reg, uint32_t node_id, const char* name,
                                    enum spa_direction direction);

/* Collects every port of `node_id` in `direction`, ordered by ordinal, into
 * `out` (up to `max`). Returns how many the node has, which may exceed `max`.
 * For callers that know a port exists but not what it is called. */
size_t tpw_pw_registry_list_ports(const struct tpw_pw_registry* reg, uint32_t node_id,
                                   enum spa_direction direction, const struct tpw_pw_port_entry** out,
                                   size_t max);

/* Increments the process-wide pw_init() refcount, calling pw_init() on
 * the first call. Must be paired with tpw_pw_global_deinit(). */
void tpw_pw_global_init(void);

/* Decrements the process-wide pw_init() refcount, calling pw_deinit()
 * when it reaches zero. */
void tpw_pw_global_deinit(void);

/* Starts a thread-loop, creates a context on it, and connects a core,
 * waiting (bounded) for PipeWire to confirm the connection before
 * returning. Returns 0 on success, a negative tpw_stream_error-style
 * code on failure (conn's fields are left safe to pass to
 * tpw_pw_core_teardown() either way). */
int tpw_pw_core_connect(struct tpw_pw_core_conn* conn, const char* loop_name);

/* Stops and destroys whatever conn holds (safe to call on a
 * partially-initialized or already-torn-down conn). */
void tpw_pw_core_teardown(struct tpw_pw_core_conn* conn);

#endif /* TPW_PW_CORE_INTERNAL_H */
