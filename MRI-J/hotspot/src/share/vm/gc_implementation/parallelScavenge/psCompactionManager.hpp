/*
 * Copyright 2005-2007 Sun Microsystems, Inc.  All Rights Reserved.
 * DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER.
 *
 * This code is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 only, as
 * published by the Free Software Foundation.
 *
 * This code is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * version 2 for more details (a copy is included in the LICENSE file that
 * accompanied this code).
 *
 * You should have received a copy of the GNU General Public License version
 * 2 along with this work; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * Please contact Sun Microsystems, Inc., 4150 Network Circle, Santa Clara,
 * CA 95054 USA or visit www.sun.com if you need additional information or
 * have any questions.
 *  
 */
// This file is a derivative work resulting from (and including) modifications
// made by Azul Systems, Inc.  The date of such changes is 2010.
// Copyright 2010 Azul Systems, Inc.  All Rights Reserved.
//
// Please contact Azul Systems, Inc., 1600 Plymouth Street, Mountain View, 
// CA 94043 USA, or visit www.azulsystems.com if you need additional information 
// or have any questions.
#ifndef PSCOMPACTIONMANAGER_HPP
#define PSCOMPACTIONMANAGER_HPP


#include "taskqueue.hpp"

//
// psPromotionManager is used by a single thread to manage object survival
// during a scavenge. The promotion manager contains thread local data only.
//
// NOTE! Be carefull when allocating the stacks on cheap. If you are going
// to use a promotion manager in more than one thread, the stacks MUST be
// on cheap. This can lead to memory leaks, though, as they are not auto
// deallocated.
//
// FIX ME FIX ME Add a destructor, and don't rely on the user to drain/flush/deallocate!
//

// Move to some global location
#define HAS_BEEN_MOVED 0x1501d01d
// End move to some global location


class MutableSpace;
class PSOldGen;
class ParCompactionManager;
class ObjectStartArray;
class ParallelCompactData;
class ParMarkBitMap;

// Move to it's own file if this works out.

class ParCompactionManager : public CHeapObj {
  friend class ParallelTaskTerminator;
  friend class ParMarkBitMap;
  friend class PSParallelCompact;
  friend class StealChunkCompactionTask;
  friend class UpdateAndFillClosure;
  friend class RefProcTaskExecutor;

 public:

// ------------------------  Don't putback if not needed
  // Actions that the compaction manager should take.
  enum Action {
    Update,
    Copy,
    UpdateAndCopy,
    CopyAndUpdate,
    VerifyUpdate,
    ResetObjects,
    NotValid
  };
// ------------------------  End don't putback if not needed

 private:
  static ParCompactionManager**  _manager_array;
  static GenericTaskQueueSet<oop>*      _stack_array;
  static ObjectStartArray*     _start_array;
  static ChunkTaskQueueSet*    _chunk_array;
  static PSOldGen*             _old_gen;

  GenericTaskQueue<oop>		       _marking_stack;
  GrowableArray<oop>*          _overflow_stack;
  // Is there a way to reuse the _marking_stack for the
  // saving empty chunks?  For now just create a different
  // type of GenericTaskQueue.

#ifdef USE_ChunkTaskQueueWithOverflow
  ChunkTaskQueueWithOverflow   _chunk_stack;
#else
  ChunkTaskQueue	       _chunk_stack;
  GrowableArray<size_t>*       _chunk_overflow_stack;
#endif

#if 1  // does this happen enough to need a per thread stack?
  GrowableArray<Klass*>*       _revisit_klass_stack;
#endif
  static ParMarkBitMap* _mark_bitmap;

  Action _action;

  static PSOldGen* old_gen()             { return _old_gen; }
  static ObjectStartArray* start_array() { return _start_array; }
  static GenericTaskQueueSet<oop>* stack_array()   { return _stack_array; }

  static void initialize(ParMarkBitMap* mbm);

 protected:
  // Array of tasks.  Needed by the ParallelTaskTerminator.
  static ChunkTaskQueueSet* chunk_array()   { return _chunk_array; }

  GenericTaskQueue<oop>*  marking_stack()          { return &_marking_stack; }
  GrowableArray<oop>* overflow_stack()    { return _overflow_stack; }
#ifdef USE_ChunkTaskQueueWithOverflow
  ChunkTaskQueueWithOverflow* chunk_stack() { return &_chunk_stack; }
#else
  ChunkTaskQueue*  chunk_stack()          { return &_chunk_stack; }
  GrowableArray<size_t>* chunk_overflow_stack() { return _chunk_overflow_stack; }
#endif

  // Pushes onto the marking stack.  If the marking stack is full,
  // pushes onto the overflow stack.
  void stack_push(oop obj);
  // Do not implement an equivalent stack_pop.  Deal with the
  // marking stack and overflow stack directly.

  // Pushes onto the chunk stack.  If the chunk stack is full,
  // pushes onto the chunk overflow stack.
  void chunk_stack_push(size_t chunk_index);
 public:

  Action action() { return _action; }
  void set_action(Action v) { _action = v; }

  inline static ParCompactionManager* manager_array(int index);

  ParCompactionManager();
  ~ParCompactionManager();

  void allocate_stacks();
  void deallocate_stacks();
  ParMarkBitMap* mark_bitmap() { return _mark_bitmap; }

  // Take actions in preparation for a compaction.
  static void reset();

  // void drain_stacks();

  bool should_update();
  bool should_copy();
  bool should_verify_only();
  bool should_reset_only();

#if 1
  // Probably stays as a growable array
  GrowableArray<Klass*>* revisit_klass_stack() { return _revisit_klass_stack; }
#endif

  // Save oop for later processing.  Must not fail.
  void save_for_scanning(oop m);
  // Get a oop for scanning.  If returns null, no oop were found.
  oop retrieve_for_scanning();

  // Save chunk for later processing.  Must not fail.
  void save_for_processing(size_t chunk_index);
  // Get a chunk for processing.  If returns null, no chunk were found.
  bool retrieve_for_processing(size_t& chunk_index);

  // Access function for compaction managers
  static ParCompactionManager* gc_thread_compaction_manager(int index);

static bool steal(int queue_num,int*seed,oop&t){
    return stack_array()->steal(queue_num, seed, t);
  }

  static bool steal(int queue_num, int* seed, ChunkTask& t) {
    return chunk_array()->steal(queue_num, seed, t);
  }

  // Process tasks remaining on any stack
  void drain_marking_stacks(OopClosure *blk);

  // Process tasks remaining on any stack
  void drain_chunk_stacks();

  // Process tasks remaining on any stack
  void drain_chunk_overflow_stack();

  // Debugging support
#ifdef ASSERT
  bool stacks_have_been_allocated();
#endif
};

inline ParCompactionManager* ParCompactionManager::manager_array(int index) {
  assert(_manager_array != NULL, "access of NULL manager_array");
  assert(index >= 0 && index <= (int)ParallelGCThreads,
    "out of range manager_array access");
  return _manager_array[index];
}
#endif // PSCOMPACTIONMANAGER_HPP
