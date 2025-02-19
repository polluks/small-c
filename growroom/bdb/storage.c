#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <err.h>

#include "bdb.h"
#include "storage.h"

size_t
snode_size (size_t data_size)
{
    // Add header (- 1 char of data declaration)
    // and record size.
    return sizeof (snode) + data_size - 1;
}

// Allocate ID (and space) for new record.
dbid_t
storage_alloc_id (bdb *db, size_t size)
{
    // An ID is basically an offset into the storage.
    dbid_t id = db->next_free;

    // Make space for size, snode and data.
    db->next_free += snode_size (size) + sizeof (size_t);
    return id;
}

// Write size before snode to storage.
bool
storage_write_size (bdb *db, dbid_t id, size_t size)
{
    size_t nsize = snode_size (size);
    size_t nwritten = db->write (db, id, &nsize, sizeof (size_t));
    return nwritten != sizeof (size_t);
}

// Write head of snode only to storage.
void
storage_write_snode (bdb *db, dbid_t id, snode *n)
{
    size_t nwritten = db->write (db, id + sizeof (size_t), n, snode_size (0));
    if (nwritten != snode_size (0))
        perror ("Error writing data.");
}

// Write data after snode to storage.
bool
storage_write_data (bdb *db, dbid_t id, void *data, size_t size)
{
    size_t nsize = sizeof (size_t) + snode_size (0);
    size_t nwritten = db->write (db, id, data, size);
    return nwritten != nsize;
}

void *
storage_map (size_t *size, bdb *db, dbid_t id)
{
    snode * n;
    size_t nsize;
    size_t nread;

    // Do nothing for the first record,
    // which is the root node of the
    // b-tree.
    if (id >= db->filled)
        return NULL;

    // Read size of node.
    if (!db->read (db, id, &nsize, sizeof (size_t)))
        perror ("storage_map(): Error reading record size.");

    // Save record size for caller.
    *size = nsize - snode_size (0);

    // Allocate memory for node & data.
    if (!(n = malloc (nsize)))
        perror ("storage_map(): Out of memory.");

    // Read node & data.
    nread = db->read (db, id + sizeof (size_t), n, nsize);
    if (nread != nsize)
        perror ("storage_map(): Reading data failed.");

    db->filled += sizeof (size_t) + nsize;
    return &n->data;
}

void
storage_insert_key (bdb *db, void *key, dbid_t recid)
{
    snode   *n;
    dbid_t  id = 0;
    dbid_t  oid = 0;
    size_t  unused_size;

    for (;;) {
        // Map node into memory.
        if (!(n = storage_map (&unused_size, db, id)))
            return; // Root node.

        if (oid == id)
            err (EXIT_FAILURE, "storage_insert_key(): endless loop");
        oid = id;  // Save for update.

        // Decide which to child to travel.
        if (db->compare (db, &n->data, key) < 0) {
            // Go left.
            if (!(id = n->left)) {
                // Set left child.
                n->left = recid;
                storage_write_snode (db, oid, n);
                return;
            }
        } else
            // Go right.
            if (!(id = n->right)) {
                // Set right child.
                n->right = recid;
                storage_write_snode (db, oid, n);
                return;
            }
    }
}

// Add record to storage.
void
storage_add (bdb *db, dbid_t id, void *data, size_t size)
{
    // Make clean snode.
    snode sn;
    bzero (&sn, snode_size (0));

    // Write size of snode + record.
    storage_write_size (db, id, size);

    // Write snode.
    storage_write_snode (db, id, &sn);

    // Write record.
    storage_write_data (db, id, data, size);

    // Link to parent node in b-tree.
    storage_insert_key (db, db->data2key (data), id);
}

dbid_t
storage_find (bdb *db, void *key)
{
    snode *n;
    dbid_t id = 0;
    dbid_t oid = -1;
    int c;
    size_t unused_size;

    for (;;) {
        // Map node into memory.
        if (!(n = storage_map (&unused_size, db, id)))
            return NOTFOUND;

        if (oid == id)
            err (EXIT_FAILURE, "storage_find(): endless loop");
        oid = id;

        // Decide to which child to travel.
        if (!(c = db->compare (db, &n->data, key)))
            return id;  // Match!
        if (c < 0) {
            // Go left.
            if (!(id = n->left))
                return NOTFOUND;
        } else
            // Go right.
            if (!(id = n->right))
                return NOTFOUND;
    }
}
