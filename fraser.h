/*
 * File:
 *   fraser.h
 * Author(s):
 *   Vincent Gramoli <vincent.gramoli@epfl.ch>
 * Description:
 *   Lock-based skip list implementation of the Fraser algorithm
 *   "Practical Lock Freedom", K. Fraser, 
 *   PhD dissertation, September 2003
 *   Cambridge University Technical Report UCAM-CL-TR-579 
 *
 * Copyright (c) 2009-2010.
 *
 * fraser.h is part of Synchrobench
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

#ifndef FRASER_H_
#define FRASER_H_
#include "skiplist.h"
#include "ssalloc.h"

int fraser_find(sl_intset_t *set, val_t val);
int fraser_remove(sl_intset_t *set, val_t val, int remove_succ);
int fraser_insert(sl_intset_t *set, val_t v);

inline int is_marked(uintptr_t i);
inline uintptr_t unset_mark(uintptr_t i);
inline uintptr_t set_mark(uintptr_t i);
inline void fraser_search(sl_intset_t *set, val_t val, sl_node_t **left_list, sl_node_t **right_list);
inline void mark_node_ptrs(sl_node_t *n);
#endif // FRASER_H_
