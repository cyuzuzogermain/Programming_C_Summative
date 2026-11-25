/**
 * @file storage.h
 * @brief Text-file persistence for transactions.
 *
 * Format: one transaction per line, '|' separated; '#' starts a comment.
 *   id|product|quantity|unitPrice|date
 * The total is not stored - it is recomputed on load so it can never be
 * inconsistent with quantity and price.
 */
#ifndef STORAGE_H
#define STORAGE_H

#include "sales.h"

SalesStatus save_transactions(const SalesDB *db, const char *path);
/** Loads into @db (which must be initialised and is normally empty). */
SalesStatus load_transactions(SalesDB *db, const char *path);

#endif /* STORAGE_H */
