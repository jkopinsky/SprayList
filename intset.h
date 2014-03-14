/*
 * File:
 *   intset.h
 * Author(s):
 *   Vincent Gramoli <vincent.gramoli@epfl.ch>
 * Description:
 *   Skip list integer set operations 
 *
 * Copyright (c) 2009-2010.
 *
 * intset.h is part of Synchrobench
 * 
 * Synchrobench is free software: you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation, version 2
 * of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include "fraser.h"
#include "linden.h"

int sl_contains(sl_intset_t *set, val_t val, int transactional);
int sl_add(sl_intset_t *set, val_t val, int transactional);
int sl_remove(sl_intset_t *set, val_t val, int transactional);
int sl_remove_succ(sl_intset_t *set, val_t val, int transactional);

// priority queue
int naive_delete_min(sl_intset_t *set, val_t *val, thread_data_t *d);
int spray_delete_min(sl_intset_t *set, val_t *val, thread_data_t *d);
