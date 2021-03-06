/*
 * Copyright 2000-2006 Sun Microsystems, Inc.  All Rights Reserved.
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


#include "callGenerator.hpp"
#include "callnode.hpp"
#include "deoptimization.hpp"
#include "graphKit.hpp"
#include "mutexLocker.hpp"
#include "parse.hpp"
#include "sharedRuntime.hpp"
#include "stubRoutines.hpp"
#include "type.hpp"

CallGenerator::CallGenerator(ciMethod* method) {
  _method = method;
}

// Utility function.
const TypeFunc* CallGenerator::tf() const {
  return TypeFunc::make(method());
}

//-----------------------------ParseGenerator---------------------------------
// Internal class which handles all direct bytecode traversal.
class ParseGenerator : public InlineCallGenerator {
private:
  bool  _is_osr;
  float _expected_uses;
  CodeProfile *_c1_cp;          // C1 CodeProfile data for the inline tree which includes this method
  int _c1_cp_inloff;            // Offset to the profile data for this method

public:
  ParseGenerator(ciMethod* method, CodeProfile *c1_cp, int c1_cp_inloff, float expected_uses, bool is_osr = false)
    : InlineCallGenerator(method), _c1_cp(c1_cp), _c1_cp_inloff(c1_cp_inloff)
  {
    _is_osr        = is_osr;
    _expected_uses = expected_uses;
    assert(can_parse(method, is_osr), "parse must be possible");
  }

  // Can we build either an OSR or a regular parser for this method?
  static bool can_parse(ciMethod* method, int is_osr = false);

  CodeProfile *c1_cp() const { return _c1_cp; }
  virtual bool      is_parse() const           { return true; }
  virtual JVMState* generate(JVMState* jvms, CPData_Invoke *cpdi, bool cloned_in_citypeflow);
  int is_osr() { return _is_osr; }

};

JVMState* ParseGenerator::generate(JVMState* jvms, CPData_Invoke *cpdi, bool cloned_in_citypeflow) {
  Compile* C = Compile::current();

  if (is_osr()) {
    // The JVMS for a OSR has a single argument (see its TypeFunc).
    assert(jvms->depth() == 1, "no inline OSR");
  }

  if (C->failing()) {
    return NULL;  // bailing out of the compile; do not try to parse
  }

  bool matches_c1_inlining = method()->objectId()==cpdi->inlined_method_oid() && cpdi->inlined_method_oid()!=0;
  Parse parser(jvms, method(), _c1_cp, _c1_cp_inloff, matches_c1_inlining, _expected_uses, cpdi);
  // Grab signature for matching/allocation
#ifdef ASSERT
const Type*mytf=tf();
if(parser.tf()!=(parser.depth()==1?C->tf():mytf)){
MutexLocker ml(Compile_lock);
    if( !C->env()->system_dictionary_modification_counter_changed() ) {
C2OUT->print_cr("==== Must invalidate if TypeFuncs differ ====");
      C2OUT->print("==== parser.tf()=%p= ",parser.tf()); parser.tf()->dump(); C2OUT->cr();
      C2OUT->print("==== this  .tf()=%p= ",     mytf  );      mytf  ->dump(); C2OUT->cr();
    }
    assert(C->env()->system_dictionary_modification_counter_changed(), 
           "Must invalidate if TypeFuncs differ");
  }
#endif

  GraphKit& exits = parser.exits();

  if (C->failing()) {
    while (exits.pop_exception_state() != NULL) ;
    return NULL;
  }

  assert(exits.jvms()->same_calls_as(jvms), "sanity");

  // Simply return the exit state of the parser,
  // augmented by any exceptional states.
  return exits.transfer_exceptions_into_jvms();
}

//---------------------------DirectCallGenerator------------------------------
// Internal class which handles all out-of-line calls w/o receiver type checks.
class DirectCallGenerator : public CallGenerator {
public:
  DirectCallGenerator(ciMethod* method)
    : CallGenerator(method)
  {
  }
  virtual JVMState* generate(JVMState* jvms, CPData_Invoke *cpdi, bool cloned_in_citypeflow);
};

JVMState* DirectCallGenerator::generate(JVMState* jvms, CPData_Invoke *cpdi, bool cloned_in_citypeflow) {
  GraphKit kit(jvms);
  bool is_static = method()->is_static();
address target=StubRoutines::resolve_and_patch_call_entry();

  CallStaticJavaNode *call = new (kit.C, tf()->domain()->cnt()) CallStaticJavaNode(tf(), target, method(), kit.bci(), cpdi, cloned_in_citypeflow);
  // Check for optimized-virtual calls, which require a null receiver check.
  if (!is_static) {
    // Make an explicit receiver null_check as part of this call.
    // Since we share a map with the caller, his JVMS gets adjusted.
kit.null_check_receiver(method(),cpdi);
    if (kit.stopped()) {
      // And dump it back to the caller, decorated with any exceptions:
      return kit.transfer_exceptions_into_jvms();
    }
  }
  kit.set_arguments_for_java_call(call);
kit.set_edges_for_java_call(call,cpdi,/*must_throw=*/false);
  Node* ret = kit.set_results_for_java_call(call);
  kit.push_node(method()->return_type()->basic_type(), ret);
  return kit.transfer_exceptions_into_jvms();
}

class VirtualCallGenerator : public CallGenerator {
private:
  int _vtable_index;
public:
  VirtualCallGenerator(ciMethod* method, int vtable_index)
    : CallGenerator(method), _vtable_index(vtable_index)
  {
    assert(vtable_index == methodOopDesc::invalid_vtable_index ||
           vtable_index >= 0, "either invalid or usable");
  }
  virtual bool      is_virtual() const          { return true; }
  virtual JVMState* generate(JVMState* jvms, CPData_Invoke *cpdi, bool cloned_in_citypeflow);
};

//--------------------------VirtualCallGenerator------------------------------
// Internal class which handles all out-of-line calls checking receiver type.
JVMState* VirtualCallGenerator::generate(JVMState* jvms, CPData_Invoke *cpdi, bool cloned_in_citypeflow) {
  GraphKit kit(jvms);
  Node* receiver = kit.argument(0);

  // If the receiver is a constant null, do not torture the system
  // by attempting to call through it.  The compile will proceed
  // correctly, but may bail out in final_graph_reshaping, because
  // the call instruction will have a seemingly deficient out-count.
  // (The bailout says something misleading about an "infinite loop".)
  if (kit.gvn().type(receiver)->higher_equal(TypePtr::NULL_PTR)) {
    kit.inc_sp(method()->arg_size());  // restore arguments
    kit.builtin_throw( Deoptimization::Reason_null_check, "null receiver", cpdi, cpdi->saw_null(), /*must throw*/true );
    return kit.transfer_exceptions_into_jvms();
  }

  // Ideally we would unconditionally do a null check here and let it
  // be converted to an implicit check based on profile information.
  // However currently the conversion to implicit null checks in
  // Block::implicit_null_check() only looks for loads and stores, not calls.
  if( cpdi->saw_null() ) {
    // Make an explicit receiver null_check as part of this call.
    // Since we share a map with the caller, his JVMS gets adjusted.
receiver=kit.null_check_receiver(method(),cpdi);
    if (kit.stopped()) {
      // And dump it back to the caller, decorated with any exceptions:
      return kit.transfer_exceptions_into_jvms();
    }
  }

  assert(!method()->is_static(), "virtual call must not be to static");
  assert(!method()->is_final(), "virtual call should not be to final");
  assert(!method()->is_private(), "virtual call should not be to private");
assert(_vtable_index==methodOopDesc::invalid_vtable_index,
         "no vtable calls if +UseInlineCaches ");
address target=StubRoutines::resolve_and_patch_call_entry();
  // Normal inline cache used for call
CallDynamicJavaNode*call=new(kit.C,tf()->domain()->cnt())CallDynamicJavaNode(tf(),target,method(),_vtable_index,kit.bci(),cpdi,cloned_in_citypeflow);
  kit.set_arguments_for_java_call(call);
kit.set_edges_for_java_call(call,cpdi,/*must_throw=*/false);
  Node* ret = kit.set_results_for_java_call(call);
  kit.push_node(method()->return_type()->basic_type(), ret);

  // Represent the effect of an implicit receiver null_check
  // as part of this call.  Since we share a map with the caller,
  // his JVMS gets adjusted.
kit.cast_not_null(receiver,true);
  return kit.transfer_exceptions_into_jvms();
}

bool ParseGenerator::can_parse(ciMethod* m, int entry_bci) {
  // Certain methods cannot be parsed at all:
if(!m->is_c2_compilable())return false;
  if (!m->has_balanced_monitors())        return false;
  if (m->get_flow_analysis()->failing())  return false;

  // (Methods may bail out for other reasons, after the parser is run.
  // We try to avoid this, but if forced, we must return (Node*)NULL.
  // The user of the CallGenerator must check for this condition.)
  return true;
}

CallGenerator* CallGenerator::for_inline(ciMethod* m, CodeProfile *c1_cp, int c1_cp_inloff, float expected_uses) {
  if (!ParseGenerator::can_parse(m))  return NULL;
  return new ParseGenerator(m, c1_cp, c1_cp_inloff, expected_uses);
}

// As a special case, the JVMS passed to this CallGenerator is
// for the method execution already in progress, not just the JVMS
// of the caller.  Thus, this CallGenerator cannot be mixed with others!
CallGenerator*CallGenerator::for_osr(ciMethod*m,CodeProfile*c1_cp,int c1_cp_inloff,int osr_bci){
  if (!ParseGenerator::can_parse(m, true))  return NULL;
  int invoke_count = UseC1 ? m->get_codeprofile_count(CodeProfile::_invoke) : m->invocation_count();
  float past_uses = (float)invoke_count;
  float expected_uses = past_uses;
  return new ParseGenerator(m, c1_cp, c1_cp_inloff, expected_uses, true);
}

CallGenerator* CallGenerator::for_direct_call(ciMethod* m) {
  assert(!m->is_abstract(), "for_direct_call mismatch");
  return new DirectCallGenerator(m);
}

CallGenerator* CallGenerator::for_virtual_call(ciMethod* m, int vtable_index) {
  assert(!m->is_static(), "for_virtual_call mismatch");
  return new VirtualCallGenerator(m, vtable_index);
}


//---------------------------WarmCallGenerator--------------------------------
// Internal class which handles initial deferral of inlining decisions.
class WarmCallGenerator : public CallGenerator {
  WarmCallInfo*   _call_info;
  CallGenerator*  _if_cold;
  CallGenerator*  _if_hot;
  bool            _is_virtual;   // caches virtuality of if_cold
  bool            _is_inline;    // caches inline-ness of if_hot

public:
  WarmCallGenerator(WarmCallInfo* ci,
                    CallGenerator* if_cold,
                    CallGenerator* if_hot)
    : CallGenerator(if_cold->method())
  {
    assert(method() == if_hot->method(), "consistent choices");
    _call_info  = ci;
    _if_cold    = if_cold;
    _if_hot     = if_hot;
    _is_virtual = if_cold->is_virtual();
    _is_inline  = if_hot->is_inline();
  }

  virtual bool      is_inline() const           { return _is_inline; }
  virtual bool      is_virtual() const          { return _is_virtual; }
  virtual bool      is_deferred() const         { return true; }

  virtual JVMState* generate(JVMState* jvms, CPData_Invoke *cpdi, bool cloned_in_citypeflow);
};


CallGenerator* CallGenerator::for_warm_call(WarmCallInfo* ci,
                                            CallGenerator* if_cold,
                                            CallGenerator* if_hot) {
  return new WarmCallGenerator(ci, if_cold, if_hot);
}

JVMState* WarmCallGenerator::generate(JVMState* jvms, CPData_Invoke *cpdi, bool cloned_in_citypeflow) {
  Compile* C = Compile::current();
jvms=_if_cold->generate(jvms,cpdi,cloned_in_citypeflow);
  if (jvms != NULL) {
    Node* m = jvms->map()->control();
    if (m->is_CatchProj()) m = m->in(0);  else m = C->top();
    if (m->is_Catch())     m = m->in(0);  else m = C->top();
    if (m->is_Proj())      m = m->in(0);  else m = C->top();
    if (m->is_CallJava()) {
      _call_info->set_call(m->as_Call());
      _call_info->set_hot_cg(_if_hot);
      if (PrintOpto) {
C2OUT->print_cr("Queueing for warm inlining at bci %d:",jvms->bci());
C2OUT->print("WCI: ");
#ifndef PRODUCT
        _call_info->print();
#endif
      }
      _call_info->set_heat(_call_info->compute_heat());
      C->set_warm_calls(_call_info->insert_into(C->warm_calls()));
    }
  }
  return jvms;
}

void WarmCallInfo::make_hot() {
  Compile* C = Compile::current();
  // Replace the callnode with something better.
  CallJavaNode* call = this->call()->as_CallJava();
  ciMethod* method   = call->method();
  int       nargs    = method->arg_size();
  JVMState* jvms     = call->jvms()->clone_shallow(C);
  uint size = TypeFunc::Parms + MAX2(2, nargs);
SafePointNode*map=new(C,size)SafePointNode(size,jvms,NULL,NULL);
  for (uint i1 = 0; i1 < (uint)(TypeFunc::Parms + nargs); i1++) {
    map->init_req(i1, call->in(i1));
  }
  jvms->set_map(map);
  jvms->set_offsets(map->req());
  jvms->set_locoff(TypeFunc::Parms);
  jvms->set_stkoff(TypeFunc::Parms);
  GraphKit kit(jvms);

  JVMState* new_jvms = _hot_cg->generate(kit.jvms(), &_cpdi, _cloned_in_citypeflow);
  if (new_jvms == NULL)  return;  // no change
  if (C->failing())      return;

  kit.set_jvms(new_jvms);
  Node* res = C->top();
  int   res_size = method->return_type()->size();
  if (res_size != 0) {
    kit.inc_sp(-res_size);
    res = kit.argument(0);
  }
  GraphKit ekit(kit.combine_and_pop_all_exception_states()->jvms());

  // Replace the call:
  for (DUIterator i = call->outs(); call->has_out(i); i++) {
    Node* n = call->out(i);
    Node* nn = NULL;  // replacement
    if (n->is_Proj()) {
      ProjNode* nproj = n->as_Proj();
      assert(nproj->_con < (uint)(TypeFunc::Parms + (res_size ? 1 : 0)), "sane proj");
      if (nproj->_con == TypeFunc::Parms) {
        nn = res;
      } else {
        nn = kit.map()->in(nproj->_con);
      }
      if (nproj->_con == TypeFunc::I_O) {
        for (DUIterator j = nproj->outs(); nproj->has_out(j); j++) {
          Node* e = nproj->out(j);
if(e->Opcode()==Op_Catch){
            for (DUIterator k = e->outs(); e->has_out(k); k++) {
              CatchProjNode* p = e->out(j)->as_CatchProj();
              if (p->is_handler_proj()) {
                p->replace_by(ekit.control());
              } else {
                p->replace_by(kit.control());
              }
            }
          }
        }
      }
    }
    NOT_PRODUCT(if (!nn)  n->dump(2));
    assert(nn != NULL, "don't know what to do with this user");
    n->replace_by(nn);
  }
}

void WarmCallInfo::make_cold() {
  // No action:  Just dequeue.
}


//------------------------PredictedCallGenerator------------------------------
// Internal class which handles all out-of-line calls checking receiver type.
class PredictedCallGenerator : public CallGenerator {
  ciKlass*       _predicted_receiver;
ciKlass*_secondary_predicted_receiver;
  CallGenerator* _if_missed;
  CallGenerator* _if_hit;
  float          _hit_prob;

public:
  PredictedCallGenerator(ciKlass* predicted_receiver,
                         CallGenerator* if_missed,
                         CallGenerator* if_hit, float hit_prob)
    : CallGenerator(if_missed->method())
  {
    // The call profile data may predict the hit_prob as extreme as 0 or 1.
    // Remove the extremes values from the range.
    if (hit_prob > PROB_MAX)   hit_prob = PROB_MAX;
    if (hit_prob < PROB_MIN)   hit_prob = PROB_MIN;

    _predicted_receiver = predicted_receiver;
_secondary_predicted_receiver=NULL;
    _if_missed          = if_missed;
    _if_hit             = if_hit;
    _hit_prob           = hit_prob;
  }

  PredictedCallGenerator(ciKlass* predicted_receiver,
ciKlass*secondary_predicted_receiver,
                         CallGenerator* if_missed,
                         CallGenerator* if_hit, float hit_prob)
    : CallGenerator(if_missed->method())
  {
    // The call profile data may predict the hit_prob as extreme as 0 or 1.
    // Remove the extremes values from the range.
    if (hit_prob > PROB_MAX)   hit_prob = PROB_MAX;
    if (hit_prob < PROB_MIN)   hit_prob = PROB_MIN;

    _predicted_receiver = predicted_receiver;
    _secondary_predicted_receiver = secondary_predicted_receiver;
    _if_missed          = if_missed;
    _if_hit             = if_hit;
    _hit_prob           = hit_prob;
  }

  virtual bool      is_virtual()   const    { return true; }
  virtual bool      is_inline()    const    { return _if_hit->is_inline(); }
  virtual bool      is_deferred()  const    { return _if_hit->is_deferred(); }

  virtual JVMState* generate(JVMState* jvms, CPData_Invoke *cpdi, bool cloned_in_citypeflow);
};


CallGenerator* CallGenerator::for_predicted_call(ciKlass* predicted_receiver,
                                                 CallGenerator* if_missed,
                                                 CallGenerator* if_hit,
                                                 float hit_prob) {
  return new PredictedCallGenerator(predicted_receiver, if_missed, if_hit, hit_prob);
}

CallGenerator*CallGenerator::for_bimorphic_predicted_call(ciKlass*predicted_receiver,
ciKlass*secondary_predicted_receiver,
                                                           CallGenerator* if_missed,
                                                           CallGenerator* if_hit,
                                                           float hit_prob) {
  return new PredictedCallGenerator(predicted_receiver, secondary_predicted_receiver, if_missed, if_hit, hit_prob);
}


JVMState* PredictedCallGenerator::generate(JVMState* jvms, CPData_Invoke *cpdi, bool cloned_in_citypeflow) {
  GraphKit kit(jvms);
  PhaseGVN& gvn = kit.gvn();
  // We need an explicit receiver null_check before checking its type.
  // We share a map with the caller, so his JVMS gets adjusted.
  Node* receiver = kit.argument(0);

  receiver = kit.null_check_receiver(method(), cpdi);
  if (kit.stopped()) {
    return kit.transfer_exceptions_into_jvms();
  }

  const TypeOopPtr *tp = TypeOopPtr::make_from_klass_unique(_predicted_receiver)->is_oopptr();
  const Type* recv_exact_type = tp->cast_to_exactness(true);
  const Type* result_type = receiver->bottom_type()->join(recv_exact_type);
  if( result_type->empty() )    // Impossible result?
    // Happens sometimes in CTWs if the prediction is unrelated to the current
    // call context, but the current call context has bogus inflated counts
    // and so is attempting to force inlining.
    return NULL;
  
  // Note the EXACT type check - subclasses not allowed, because we are trying
  // to inline a specific method.
  // Note: We *could* use a normal subtype check if CHA reports a single implementor.
  Node* recv_kid   = gvn.transform(new (kit.C, 2) GetKIDNode(NULL/*receiver known not-null here*/, receiver,TypeKlassPtr::KID));
  Node* recv_klass = gvn.transform(new (kit.C, 2) KID2KlassNode(recv_kid));
  Node* exact_receiver = receiver;  // will get updated in place...
  Node* slow_ctl = kit.type_check_receiver(receiver,
                                           _predicted_receiver, _hit_prob,
                                           &exact_receiver);

  SafePointNode* slow_map = NULL;
JVMState*slow_jvms=NULL;
  { PreserveJVMState pjvms(&kit);
    kit.set_control(slow_ctl);
if(_secondary_predicted_receiver!=NULL){
      const TypeOopPtr *tp2 = TypeOopPtr::make_from_klass_unique(_secondary_predicted_receiver)->is_oopptr();
      const Type* recv_exact_type2 = tp2->cast_to_exactness(true);
      const Type* result_type2 = receiver->bottom_type()->join(recv_exact_type2);

      // Slow-path for failed 2ndary test: just uncommon-trap it
      Node* prof_k2 = kit.makecon(TypeKlassPtr::make(_secondary_predicted_receiver));
      Node* cmp2 = gvn.transform(new (kit.C, 3) CmpPNode( recv_klass, prof_k2));
      Node* bol2 = gvn.transform(new (kit.C, 2) BoolNode( cmp2, BoolTest::eq));
      { BuildCutout unless(&kit, bol2, 0.0);
        CallGenerator* utcall = CallGenerator::for_uncommon_trap(method(),Deoptimization::Reason_unexpected_klass,"TypeProfile failure",false);
        utcall->generate(kit.sync_jvms(), cpdi, cloned_in_citypeflow);
kit.stop();
      }

      if( kit.stopped() ) { // Impossible result?
        // The profiling had a bad answer for the 2ndary receiver
        _if_missed = CallGenerator::for_uncommon_trap(method(),Deoptimization::Reason_unexpected_klass,"BiMorphic 2ndary receiver not legal",false);
      } else {
        Node* exact_recv2 = gvn.transform( new (kit.C, 2) CheckCastPPNode(kit.control(), receiver, recv_exact_type2) );
kit.replace_in_map(receiver,exact_recv2);
      }
    }
   
    if (!kit.stopped()) {
slow_jvms=_if_missed->generate(kit.sync_jvms(),cpdi,cloned_in_citypeflow);
      assert(slow_jvms != NULL, "miss path must not fail to generate");
      kit.add_exception_states_from(slow_jvms);
      kit.set_map(slow_jvms->map());
      if (!kit.stopped())
        slow_map = kit.stop();
    }
  }

  // fall through if the instance exactly matches the desired type
  kit.replace_in_map(receiver, exact_receiver);

  // Make the hot call:
JVMState*new_jvms=_if_hit->generate(kit.sync_jvms(),cpdi,cloned_in_citypeflow);
  if (new_jvms == NULL) {
    // Inline failed, so make a direct call.
    assert(_if_hit->is_inline(), "must have been a failed inline");
    CallGenerator* cg = CallGenerator::for_direct_call(_if_hit->method());
new_jvms=cg->generate(kit.sync_jvms(),cpdi,cloned_in_citypeflow);
  }
  kit.add_exception_states_from(new_jvms);
  kit.set_jvms(new_jvms);

  // Need to merge slow and fast?
  if (slow_map == NULL) {
    // The fast path is the only path remaining.
    return kit.transfer_exceptions_into_jvms();
  }

  if (kit.stopped()) {
    // Inlined method threw an exception, so it's just the slow path after all.
    kit.set_jvms(slow_jvms);
    return kit.transfer_exceptions_into_jvms();
  }

  // Finish the diamond.
  kit.C->set_has_split_ifs(true); // Has chance for split-if optimization
  RegionNode* region = new (kit.C, 3) RegionNode(3);
  region->init_req(1, kit.control());
  region->init_req(2, slow_map->control());
  kit.set_control(gvn.transform(region));
  Node* iophi = PhiNode::make(region, kit.i_o(), Type::ABIO);
  iophi->set_req(2, slow_map->i_o());
  kit.set_i_o(gvn.transform(iophi));
  kit.merge_memory(slow_map->merged_memory(), region, 2);
  uint tos = kit.jvms()->stkoff() + kit.sp();
  uint limit = slow_map->req();
  for (uint i = TypeFunc::Parms; i < limit; i++) {
    // Skip unused stack slots; fast forward to monoff();
    if (i == tos) {
      i = kit.jvms()->monoff();
      if( i >= limit ) break;
    }
    Node* m = kit.map()->in(i);
    Node* n = slow_map->in(i);
    if (m != n) {
      const Type* t = gvn.type(m)->meet(gvn.type(n));
      Node* phi = PhiNode::make(region, m, t);
      phi->set_req(2, n);
      kit.map()->set_req(i, gvn.transform(phi));
    }
  }
  return kit.transfer_exceptions_into_jvms();
}


//-------------------------UncommonTrapCallGenerator-----------------------------
// Internal class which handles all out-of-line calls checking receiver type.
class UncommonTrapCallGenerator : public CallGenerator {
Deoptimization::DeoptReason _trap_index;
  const char *_comment;
  bool _must_throw;

public:
  UncommonTrapCallGenerator(ciMethod* m,
                            Deoptimization::DeoptReason trap_index, const char *comment, bool must_throw)
    : CallGenerator(m), _trap_index(trap_index), _comment(comment), _must_throw(must_throw) { }

  virtual bool      is_virtual() const          { ShouldNotReachHere(); return false; }
  virtual bool      is_trap() const             { return true; }

  virtual JVMState* generate(JVMState* jvms, CPData_Invoke *cpdi, bool cloned_in_citypeflow);
};


CallGenerator*
CallGenerator::for_uncommon_trap(ciMethod* m,
                                 Deoptimization::DeoptReason trap_index, const char *comment, bool must_throw) {
  return new UncommonTrapCallGenerator(m, trap_index, comment, must_throw);
}


JVMState* UncommonTrapCallGenerator::generate(JVMState* jvms, CPData_Invoke *cpdi, bool cloned_in_citypeflow) {
  GraphKit kit(jvms);
  // Take the trap with arguments pushed on the stack.  (Cf. null_check_receiver).
  int nargs = method()->arg_size();
  kit.inc_sp(nargs);
  assert(nargs <= kit.sp() && kit.sp() <= jvms->stk_size(), "sane sp w/ args pushed");
  kit.uncommon_trap(_trap_index, NULL, _comment, _must_throw);
  return kit.transfer_exceptions_into_jvms();
}

// (Note:  Moved hook_up_call to GraphKit::set_edges_for_java_call.)

// (Node:  Merged hook_up_exits into ParseGenerator::generate.)

#define NODES_OVERHEAD_PER_METHOD (30.0)
#define NODES_PER_BYTECODE (9.5)

void WarmCallInfo::init(JVMState* call_site, ciMethod* call_method, int call_count, float prof_factor, CPData_Invoke *cpdi) {
  int code_size = call_method->code_size();

  // Expected execution count is based on the historical count:
_count=call_count<0?1:prof_factor*call_count;//our scale_count (OJDK equivalent) is the simple product

  // Expected profit from inlining, in units of simple call-overheads.
  _profit = 1.0;

  // Expected work performed by the call in units of call-overheads.
  // %%% need an empirical curve fit for "work" (time in call)
  float bytecodes_per_call = 3;
  _work = 1.0 + code_size / bytecodes_per_call;

  // Expected size of compilation graph:
  // -XX:+PrintParseStatistics once reported:
  //  Methods seen: 9184  Methods parsed: 9184  Nodes created: 1582391
  //  Histogram of 144298 parsed bytecodes:
  // %%% Need an better predictor for graph size.
  _size = NODES_OVERHEAD_PER_METHOD + (NODES_PER_BYTECODE * code_size);

  // Copy call-site profiling info local
  Untested("");
  _cpdi = *cpdi;
}

// is_cold:  Return true if the node should never be inlined.
// This is true if any of the key metrics are extreme.
bool WarmCallInfo::is_cold() const {
  if (count()  <  WarmCallMinCount)        return true;
  if (profit() <  WarmCallMinProfit)       return true;
  if (work()   >  WarmCallMaxWork)         return true;
  if (size()   >  WarmCallMaxSize)         return true;
  return false;
}

// is_hot:  Return true if the node should be inlined immediately.
// This is true if any of the key metrics are extreme.
bool WarmCallInfo::is_hot() const {
  assert(!is_cold(), "eliminate is_cold cases before testing is_hot");
  if (count()  >= HotCallCountThreshold)   return true;
  if (profit() >= HotCallProfitThreshold)  return true;
  if (work()   <= HotCallTrivialWork)      return true;
  if (size()   <= HotCallTrivialSize)      return true;
  return false;
}

// compute_heat:  
float WarmCallInfo::compute_heat() const {
  assert(!is_cold(), "compute heat only on warm nodes");
  assert(!is_hot(),  "compute heat only on warm nodes");
  int min_size = MAX2(0,   (int)HotCallTrivialSize);
  int max_size = MIN2(500, (int)WarmCallMaxSize);
  float method_size = (size() - min_size) / MAX2(1, max_size - min_size);
  float size_factor;
  if      (method_size < 0.05)  size_factor = 4;   // 2 sigmas better than avg.
  else if (method_size < 0.15)  size_factor = 2;   // 1 sigma better than avg.
  else if (method_size < 0.5)   size_factor = 1;   // better than avg.
  else                          size_factor = 0.5; // worse than avg.
  return (count() * profit() * size_factor);
}

bool WarmCallInfo::warmer_than(WarmCallInfo* that) {
  assert(this != that, "compare only different WCIs");
  assert(this->heat() != 0 && that->heat() != 0, "call compute_heat 1st");
  if (this->heat() > that->heat())   return true;
  if (this->heat() < that->heat())   return false;
  assert(this->heat() == that->heat(), "no NaN heat allowed");
  // Equal heat.  Break the tie some other way.
  if (!this->call() || !that->call())  return (address)this > (address)that;
  return this->call()->_idx > that->call()->_idx;
}

//#define UNINIT_NEXT ((WarmCallInfo*)badAddress)
#define UNINIT_NEXT ((WarmCallInfo*)NULL)

WarmCallInfo* WarmCallInfo::insert_into(WarmCallInfo* head) {
  assert(next() == UNINIT_NEXT, "not yet on any list");
  WarmCallInfo* prev_p = NULL;
  WarmCallInfo* next_p = head;
  while (next_p != NULL && next_p->warmer_than(this)) {
    prev_p = next_p;
    next_p = prev_p->next();
  }
  // Install this between prev_p and next_p.
  this->set_next(next_p);
  if (prev_p == NULL)
    head = this;
  else
    prev_p->set_next(this);
  return head;
}

WarmCallInfo* WarmCallInfo::remove_from(WarmCallInfo* head) {
  WarmCallInfo* prev_p = NULL;
  WarmCallInfo* next_p = head;
  while (next_p != this) {
    assert(next_p != NULL, "this must be in the list somewhere");
    prev_p = next_p;
    next_p = prev_p->next();
  }
  next_p = this->next();
  debug_only(this->set_next(UNINIT_NEXT));
  // Remove this from between prev_p and next_p.
  if (prev_p == NULL)
    head = next_p;
  else
    prev_p->set_next(next_p);
  return head;
}

WarmCallInfo* WarmCallInfo::_always_hot  = NULL;
WarmCallInfo* WarmCallInfo::_always_cold = NULL;

WarmCallInfo* WarmCallInfo::always_hot() {
  if (_always_hot == NULL) {
    static double bits[sizeof(WarmCallInfo) / sizeof(double) + 1] = {0};
    WarmCallInfo* ci = (WarmCallInfo*) bits;
    ci->_profit = ci->_count = MAX_VALUE();
    ci->_work   = ci->_size  = MIN_VALUE();
    _always_hot = ci;
  }
  assert(_always_hot->is_hot(), "must always be hot");
  return _always_hot;
}

WarmCallInfo* WarmCallInfo::always_cold() {
  if (_always_cold == NULL) {
    static double bits[sizeof(WarmCallInfo) / sizeof(double) + 1] = {0};
    WarmCallInfo* ci = (WarmCallInfo*) bits;
    ci->_profit = ci->_count = MIN_VALUE();
    ci->_work   = ci->_size  = MAX_VALUE();
    _always_cold = ci;
  }
  assert(_always_cold->is_cold(), "must always be cold");
  return _always_cold;
}


#ifndef PRODUCT

void WarmCallInfo::print() const {
C2OUT->print("%s : C=%6.1f P=%6.1f W=%6.1f S=%6.1f H=%6.1f -> %p",
             is_cold() ? "cold" : is_hot() ? "hot " : "warm",
             count(), profit(), work(), size(), compute_heat(), next());
C2OUT->cr();
  if (call() != NULL)  call()->dump();
}

void print_wci(WarmCallInfo* ci) {
  ci->print();
}

void WarmCallInfo::print_all() const {
  for (const WarmCallInfo* p = this; p != NULL; p = p->next())
    p->print();
}

int WarmCallInfo::count_all() const {
  int cnt = 0;
  for (const WarmCallInfo* p = this; p != NULL; p = p->next())
    cnt++;
  return cnt;
}

#endif //PRODUCT
