#include "iencoding.h"
#include "../vcompiler.h"
#include "../codegen.h"
#include "../dispatcher.h"
#include "../codemanager.h"
#include "../Python/frames.h"


DEFINEVAR reg_t RegistersLoop[REG_TOTAL] = {
    /* RAX > */ REG_NONE,
    /* RCX > */ REG_NONE,
    /* RDX > */ REG_NONE,
    /* RBX > */ RBP_IS_RESERVED ? REG_LOOP_START : REG_X64_RBP,
    /* RSP > */ REG_NONE,
    /* RBP > */ RBP_IS_RESERVED ? REG_NONE : REG_LOOP_START,
    /* RSI > */ REG_NONE,
    /* RDI > */ REG_NONE,
    /* R8  > */ REG_NONE,
    /* R9  > */ REG_NONE,
    /* R10 > */ REG_NONE,
    /* R11 > */ REG_NONE,
    /* R12 > */ REG_NONE,
    /* R13 > */ REG_X64_R14,
    /* R14 > */ REG_X64_R15,
    /* R15 > */ REG_X64_RBX
};

/* XXX Get function info from function pointer in glibc only */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <dlfcn.h>

DEFINEFN void* psyco_call_code_builder(PsycoObject* po, void* fn, int restore, RunTimeSource extraarg, size_t block_size)
{
  char* block_start;
  Dl_info dl_info;

  BEGIN_CODE
  if(dladdr(fn, &dl_info)) {
    fprintf(codegen_log, "CALL_CODE_BUILDER %p > %p (%s:%s)\n", code, fn, dl_info.dli_fname, dl_info.dli_sname);
  } else {
    fprintf(codegen_log, "CALL_CODE_BUILDER %p > %p ()\n", code, fn);
  }

  BEGIN_SHORT_JUMP(0);
  block_start = (char*)(((long)code + 7) & (~7)); /* alignment */
  code = block_start;
  code += block_size;
  END_SHORT_JUMP(0);
  BEGIN_CALL(extraarg != SOURCE_DUMMY ? 2 : 1);
  if (extraarg != SOURCE_DUMMY) {
    CALL_SET_ARG_FROM_RT(extraarg, 1);
  }
  CALL_SET_ARG_IMMED(block_start, 0);
  END_CALL_I(fn);
  JMP_R(REG_FUNCTIONS_RETURN);
  END_CODE
  return (void*)block_start;
}

DEFINEFN
vinfo_t* psyco_call_psyco(PsycoObject* po, CodeBufferObject* codebuf,
			  Source argsources[], int argcount,
			  struct stack_frame_info_s* finfo)
{
	/* this is a simplified version of psyco_generic_call() which
	   assumes Psyco's calling convention instead of the C's. */
	int i;
	bool ccflags;
    int initial_stack_depth;
    int stack_correction;
	BEGIN_CODE
	/* cannot use NEED_CC(): it might clobber one of the registers
	   mentioned in argsources */
	ccflags = HAS_CCREG(po);
	if (ccflags)
		PUSH_CC();
    
    /* PUSH RTs with reg set */
    for(i = 0; i < REG_TOTAL; i++) {
        vinfo_t* content = REG_NUMBER(po, i);
        if (content != NULL) {
            PUSH_R(i);
            psyco_inc_stackdepth(po);
        }
    }

    /* add 16 byte aligned stack */
    do {
        stack_correction = (16 - (((argcount * sizeof(long)) + STACK_DEPTH_SINCE_CALL() + INITIAL_STACK_DEPTH) % 16)) % 16;
        STACK_CORRECTION(stack_correction);
        po->stack_depth += stack_correction;
    } while (0);

#if CHECK_STACK_DEPTH
    MOV_R_R(REG_TRANSIENT_1, REG_X64_RSP);
    SUB_R_I8(REG_TRANSIENT_1, 8);
    PUSH_R(REG_TRANSIENT_1);
    psyco_inc_stackdepth(po);
#endif
    PUSH_I(-1); /* finfo */
    psyco_inc_stackdepth(po);

    ABOUT_TO_CALL_SUBFUNCTION(finfo);
    /* offset in stack to this this finfo */
    finfo_last(finfo)->link_stack_depth = po->stack_depth - INITIAL_STACK_DEPTH; 
    initial_stack_depth = po->stack_depth;
    /* We assume that compiler register state is not changed by the call macros */
    /* note that we are not using C calling convention here */
    for(i = 0; i < argcount; i++) {
        Source source = argsources[i];
        if(RSOURCE_REG_IS_NONE(source)) {
            assert(getstack(source) != RUNTIME_STACK_NONE);
            PUSH_O(REG_X64_RSP, STACK_POS_OFFSET(RSOURCE_STACK(source)));
        } else {
            PUSH_R(getreg(source));
        }
        psyco_inc_stackdepth(po);
    }
    po->stack_depth += stack_correction;

    fprintf(codegen_log, "PSYCO_CALL_PSYCO %p > %p\n", code, codebuf->codestart);
    fflush(codegen_log);

    CALL_I(codebuf->codestart);
    /* psyco callees remove args :| */
    po->stack_depth = initial_stack_depth;
    RETURNED_FROM_SUBFUNCTION();
    #if CHECK_STACK_DEPTH
        ADD_R_I8(REG_X64_RSP, sizeof(long) * 2);
        psyco_dec_stackdepth(po);
        psyco_dec_stackdepth(po);
    #else
        ADD_R_I8(REG_X64_RSP, sizeof(long)); 
        psyco_dec_stackdepth(po);
    #endif
    /* remove stack correction */
    STACK_CORRECTION(-stack_correction);
    po->stack_depth -= stack_correction;
    /* POP RTs with reg set */
    for(i = REG_TOTAL-1; i >= 0; i--) {
        vinfo_t* content = REG_NUMBER(po, i);
        if (content != NULL) {
            POP_R(i);
            psyco_dec_stackdepth(po);
        }
    }

	if (ccflags)
		POP_CC();


	END_CODE
	return generic_call_check(po, CfReturnRef|CfPyErrIfNull,
				  bfunction_result(po, true));
}


/* run-time vinfo_t creation */
PSY_INLINE vinfo_t* new_rtvinfo(PsycoObject* po, reg_t reg, bool ref, bool nonneg) {
	vinfo_t* vi = vinfo_new(RunTime_New(reg, ref, nonneg));
	REG_NUMBER(po, reg) = vi;
	return vi;
}

/* internal, see NEED_CC() */
EXTERNFN condition_code_t cc_from_vsource(Source source);  /* in codegen.c */

DEFINEFN
code_t* psyco_compute_cc(PsycoObject* po, code_t* code, reg_t reserved)
{
	int i;
	vinfo_t* v;
	condition_code_t cc;
	reg_t rg;
	for (i=0; i<2; i++) {
		v = po->ccregs[i];
		if (v == NULL)
			continue;
		cc = cc_from_vsource(v->source);

		NEED_FREE_BYTE_REG(rg, reserved, REG_NONE);
		LOAD_REG_FROM_CONDITION(rg, cc);

		v->source = RunTime_New(rg, false, true);
		REG_NUMBER(po, rg) = v;
		po->ccregs[i] = NULL;
	}
        return code;
}


/*****************************************************************/
 /***   Emit common instructions                                ***/


DEFINEFN
vinfo_t* bininstrgrp(PsycoObject* po, int group, bool ovf, bool nonneg,
                     vinfo_t* v1, vinfo_t* v2)
{
  reg_t rg;
  BEGIN_CODE
  NEED_CC();
  COPY_IN_REG(v1, rg);                      /* MOV rg, (v1) */
  COMMON_INSTR_FROM(group, rg, v2->source); /* XXX rg, (v2) */
  END_CODE
  if (ovf && runtime_condition_f(po, CC_O))
    return NULL;  /* if overflow */
  return new_rtvinfo(po, rg, false, nonneg);
}

DEFINEFN
vinfo_t* bint_add_i(PsycoObject* po, vinfo_t* rt1, long value2, bool unsafe)
{
  reg_t rg, dst;
  extra_assert(is_runtime(rt1->source));
  BEGIN_CODE
  NEED_FREE_REG(dst);
  rg = getreg(rt1->source);
  if (rg == REG_NONE)
    {
      rg = dst;
      LOAD_REG_FROM(rt1->source, rg);
    }
  LOAD_REG_FROM_REG_PLUS_IMMED(dst, rg, value2);
  END_CODE
  return new_rtvinfo(po, dst, false,
		unsafe && value2>=0 && is_rtnonneg(rt1->source));
}

#define GENERIC_SHIFT_BY(rtmacro, nonneg)               \
  {                                                     \
    reg_t rg;                                           \
    extra_assert(0 < counter && counter < LONG_BIT);    \
    BEGIN_CODE                                          \
    NEED_CC();                                          \
    COPY_IN_REG(v1, rg);                                \
    rtmacro(rg, counter);                               \
    END_CODE                                            \
    return new_rtvinfo(po, rg, false, nonneg);          \
  }

DEFINEFN
vinfo_t* bininstrshift(PsycoObject* po, int group,
                       bool nonneg, vinfo_t* v1, vinfo_t* v2)
{
  reg_t rg;
  BEGIN_CODE
  if (RSOURCE_REG(v2->source) != SHIFT_COUNTER) {
    NEED_REGISTER(SHIFT_COUNTER);
    LOAD_REG_FROM(v2->source, SHIFT_COUNTER);
  }
  NEED_CC_REG(SHIFT_COUNTER);
  DELAY_USE_OF(SHIFT_COUNTER);
  COPY_IN_REG(v1, rg);
  SHIFT_GENERICCL(rg, group);      /* SHx rg, CL */
  END_CODE
  return new_rtvinfo(po, rg, false, nonneg);
}


DEFINEFN
vinfo_t* bint_lshift_i(PsycoObject* po, vinfo_t* v1, int counter)
     GENERIC_SHIFT_BY(SHIFT_LEFT_BY, false)

DEFINEFN
vinfo_t* bint_rshift_i(PsycoObject* po, vinfo_t* v1, int counter)
     GENERIC_SHIFT_BY(SHIFT_SIGNED_RIGHT_BY, is_nonneg(v1->source))

DEFINEFN
vinfo_t* bint_urshift_i(PsycoObject* po, vinfo_t* v1, int counter)
     GENERIC_SHIFT_BY(SHIFT_RIGHT_BY, true)

DEFINEFN
vinfo_t* bint_mul_i(PsycoObject* po, vinfo_t* v1, long value2, bool ovf)
{
  reg_t rg;
  BEGIN_CODE
  NEED_CC();
  NEED_FREE_REG(rg);
  IMUL_IMMED_FROM_RT(v1->source, value2, rg);
  END_CODE
  if (ovf && runtime_condition_f(po, CC_O))
    return NULL;
  return new_rtvinfo(po, rg, false,
                     ovf && value2>=0 && is_rtnonneg(v1->source));
}

DEFINEFN
vinfo_t* bininstrmul(PsycoObject* po, bool ovf,
                     bool nonneg, vinfo_t* v1, vinfo_t* v2)
{
  reg_t rg;
  BEGIN_CODE
  NEED_CC();
  COPY_IN_REG(v1, rg);               /* MOV rg, (v1) */
  IMUL_REG_FROM_RT(v2->source, rg);  /* IMUL rg, (v2) */
  END_CODE
  if (ovf && runtime_condition_f(po, CC_O))
    return NULL;  /* if overflow */
  return new_rtvinfo(po, rg, false, nonneg);
}

DEFINEFN
vinfo_t* unaryinstrgrp(PsycoObject* po, int group, bool ovf,
                       bool nonneg, vinfo_t* v1)
{
  reg_t rg;
  BEGIN_CODE
  NEED_CC();
  COPY_IN_REG(v1, rg);                  /* MOV rg, (v1) */
  UNARY_INSTR_ON_REG(group, rg);        /* XXX rg       */
  END_CODE
  if (ovf && runtime_condition_f(po, CC_O))
    return NULL;  /* if overflow */
  return new_rtvinfo(po, rg, false, nonneg);
}

/* XXX assumes RAX is free to trash */
DEFINEFN
vinfo_t* unaryinstrabs(PsycoObject* po, bool ovf,
                                bool nonneg, vinfo_t* v1)
{
    reg_t rg;
    BEGIN_CODE
    NEED_CC();
    if(getreg(v1->source) == REG_NONE) {
        LOAD_REG_FROM_EBP_BASE(REG_X64_RAX, RUNTIME_STACK(v1));
    } else {
        MOV_R_R(REG_X64_RAX, getreg(v1->source));
    }
    NEED_FREE_REG_COND(rg, (rg != REG_X64_RAX && rg != REG_X64_RDX && rg != getreg(v1->source)));
    if(REG_NUMBER(po, REG_X64_RDX) != NULL) {
        MOV_R_R(rg, REG_X64_RDX);
    }
    /* ABS RAX */
    CQO();
    ADD_R_R(REG_X64_RAX, REG_X64_RDX);
    XOR_R_R(REG_X64_RAX, REG_X64_RDX);
    /*     */
    if(REG_NUMBER(po, REG_X64_RDX) != NULL) {
        MOV_R_R(REG_X64_RDX, rg);
    }
    MOV_R_R(rg, REG_X64_RAX);
    END_CODE
    /* if number is negative we overflowed */
    if (ovf && runtime_condition_f(po, CC_S))
        return NULL;  
    return new_rtvinfo(po, rg, false, nonneg);
}

static const condition_code_t direct_results[16] = {
	  /*****   signed comparison      **/
          /* Py_LT: */  CC_L,
          /* Py_LE: */  CC_LE,
          /* Py_EQ: */  CC_E,
          /* Py_NE: */  CC_NE,
          /* Py_GT: */  CC_G,
          /* Py_GE: */  CC_GE,
	  /* (6)    */  CC_ERROR,
	  /* (7)    */  CC_ERROR,
	  /*****  unsigned comparison     **/
          /* Py_LT: */  CC_uL,
          /* Py_LE: */  CC_uLE,
          /* Py_EQ: */  CC_E,
          /* Py_NE: */  CC_NE,
          /* Py_GT: */  CC_uG,
          /* Py_GE: */  CC_uGE,
	  /* (14)   */  CC_ERROR,
	  /* (15)   */  CC_ERROR };

DEFINEFN
condition_code_t bint_cmp_i(PsycoObject* po, int base_py_op,
                            vinfo_t* rt1, long immed2)
{
  BEGIN_CODE
  NEED_CC();
  COMPARE_IMMED_FROM_RT(rt1->source, immed2);
  END_CODE
  return direct_results[base_py_op];
}

DEFINEFN
condition_code_t bininstrcmp(PsycoObject* po, int base_py_op,
                             vinfo_t* v1, vinfo_t* v2)
{
  BEGIN_CODE
  NEED_CC();
  RTVINFO_IN_REG(v1);         /* CMP v1, v2 */
  COMMON_INSTR_FROM_RT(7, getreg(v1->source), v2->source);
  END_CODE
  return direct_results[base_py_op];
}

DEFINEFN
vinfo_t* bininstrcond(PsycoObject* po, condition_code_t cc,
                      long immed_true, long immed_false)
{
  reg_t rg;
  BEGIN_CODE
  NEED_FREE_REG(rg);
  LOAD_REG_FROM_IMMED(rg, immed_true);
  BEGIN_SHORT_COND_JUMP(1, cc);
  LOAD_REG_FROM_IMMED(rg, immed_false);
  END_SHORT_JUMP(1);
  END_CODE
  return new_rtvinfo(po, rg, false, immed_true >= 0 && immed_false >= 0);
}

DEFINEFN
vinfo_t* bfunction_result(PsycoObject* po, bool ref)
{
  reg_t rg;
  BEGIN_CODE
  NEED_FREE_REG(rg);
  MOV_R_R(rg, REG_FUNCTIONS_RETURN);
  END_CODE
  return new_rtvinfo(po, rg, ref, false);
}

DEFINEFN
vinfo_t* make_runtime_copy(PsycoObject* po, vinfo_t* v)
{
	reg_t rg;
	if (!compute_vinfo(v, po)) return NULL;
	BEGIN_CODE
	NEED_FREE_REG(rg);
	LOAD_REG_FROM(v->source, rg);
	END_CODE
	return new_rtvinfo(po, rg, false, is_nonneg(v->source));
}


#if 0
DEFINEFN   -- unused --
vinfo_t* integer_and_i(PsycoObject* po, vinfo_t* v1, long value2)
     GENERIC_BINARY_INSTR_2(4, a & b,    /* AND */
			    value2>=0 || is_rtnonneg(v1->source))
#define GENERIC_BINARY_INSTR_2(group, c_code, nonneg)                   \
{                                                                       \
  if (!compute_vinfo(v1, po)) return NULL;                              \
  if (is_compiletime(v1->source))                                       \
    {                                                                   \
      long a = CompileTime_Get(v1->source)->value;                      \
      long b = value2;                                                  \
      long c = (c_code);                                                \
      return vinfo_new(CompileTime_New(c));                             \
    }                                                                   \
  else                                                                  \
    {                                                                   \
      reg_t rg;                                                         \
      BEGIN_CODE                                                        \
      NEED_CC();                                                        \
      COPY_IN_REG(v1, rg);                   /* MOV rg, (v1) */         \
      COMMON_INSTR_IMMED(group, rg, value2); /* XXX rg, value2 */       \
      END_CODE                                                          \
      return new_rtvinfo(po, rg, false, nonneg);                        \
    }                                                                   \
}
#endif

#if 0
DEFINEFN      (not used)
vinfo_t* integer_seqindex(PsycoObject* po, vinfo_t* vi, vinfo_t* vn, bool ovf)
{
  NonVirtualSource vns, vis;
  vns = vinfo_compute(vn, po);
  if (vns == SOURCE_ERROR) return NULL;
  vis = vinfo_compute(vi, po);
  if (vis == SOURCE_ERROR) return NULL;
  
  if (!is_compiletime(vis))
    {
      reg_t rg, tmprg;
      BEGIN_CODE
      NEED_CC_SRC(vis);
      NEED_FREE_REG(rg);
      LOAD_REG_FROM_RT(vis, rg);
      DELAY_USE_OF(rg);
      NEED_FREE_REG(tmprg);

      /* Increase 'rg' by 'vns' unless it is already in the range(0, vns). */
         /* CMP i, n */
      vns = vn->source;  /* reload, could have been moved by NEED_FREE_REG */
      COMMON_INSTR_FROM(7, rg, vns);
         /* SBB t, t */
      COMMON_INSTR_FROM_RT(3, tmprg, RunTime_New(tmprg, false...));
         /* AND t, n */
      COMMON_INSTR_FROM(4, tmprg, vns);
         /* SUB i, t */
      COMMON_INSTR_FROM_RT(5, rg, RunTime_New(tmprg, false...));
         /* ADD i, n */
      COMMON_INSTR_FROM(0, rg, vns);
      END_CODE

      if (ovf && runtime_condition_f(po, CC_NB))  /* if out of range */
        return NULL;
      return new_rtvinfo(po, rg, false...);
    }
  else
    {
      long index = CompileTime_Get(vis)->value;
      long reqlength;
      if (index >= 0)
        reqlength = index;  /* index is known, length must be greater than it */
      else
        reqlength = ~index;  /* idem for negative index */
      if (ovf)
        {
          /* test for out of range index -- more precisely, test that the
             length is not large enough for the known index */
          condition_code_t cc = integer_cmp_i(po, vn, reqlength, Py_LE);
          if (cc == CC_ERROR || runtime_condition_f(po, cc))
            return NULL;
        }
      if (index >= 0)
        {
          vinfo_incref(vi);
          return vi;
        }
      else
        return integer_add_i(po, vn, index...);
    }
}
#endif  /* 0 */
