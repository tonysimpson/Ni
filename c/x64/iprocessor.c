#include "../processor.h"
#include "iencoding.h"
#include "../dispatcher.h"
#include "../codemanager.h"
#include "../Python/frames.h"


typedef PyObject* (*glue_run_code_fn) (code_t* code_target,
				       long* stack_end,
				       long* initial_stack,
				       struct stack_frame_info_s*** finfo);


static glue_run_code_fn glue_run_code_aligned;

static glue_run_code_fn glue_run_code_unaligned;

static void write_glue_run_code_fn(PsycoObject *po, bool aligned) {
    BEGIN_CODE
    PUSH_R(REG_X64_RBP);
#if RBP_IS_RESERVED
    MOV_R_R(REG_X64_RBP, REG_X64_RSP);
#endif
    PUSH_R(REG_X64_RBX);
    PUSH_R(REG_X64_R12);
    PUSH_R(REG_X64_R13);
    PUSH_R(REG_X64_R14);
    PUSH_R(REG_X64_R15);
    if (!aligned) {
        SUB_R_I8(REG_X64_RSP, 8);
    }
#if CHECK_STACK_DEPTH
    MOV_R_R(REG_TRANSIENT_1, REG_X64_RSP);
    SUB_R_I8(REG_TRANSIENT_1, 8);
    PUSH_R(REG_TRANSIENT_1);
#endif
    PUSH_I(-1); /* finfo */
    MOV_A_R(REG_X64_RCX, REG_X64_RSP);
    BEGIN_SHORT_JUMP(0);
    BEGIN_REVERSE_SHORT_JUMP(1);
    SUB_R_I8(REG_X64_RSI, 8);
    PUSH_A(REG_X64_RSI);
    END_SHORT_JUMP(0);
    CMP_R_R(REG_X64_RDX, REG_X64_RSI);
    END_REVERSE_SHORT_COND_JUMP(1, CC_NE);
    CALL_R(REG_X64_RDI);
    ADD_R_I8(REG_X64_RSP, (1 + (aligned ? 0 : 1) + (CHECK_STACK_DEPTH ? 1 : 0)) * sizeof(long));
    POP_R(REG_X64_R15);
    POP_R(REG_X64_R14);
    POP_R(REG_X64_R13);
    POP_R(REG_X64_R12);
    POP_R(REG_X64_RBX);
    POP_R(REG_X64_RBP);
    RET();
    END_CODE
}

#if TRACE_EXECUTION_LOG
typedef struct {
    void* r15;
    void* r14;
    void* r13;
    void* r12;
    void* r11;
    void* r10;
    void* r9;
    void* r8;
    void* rdi;
    void* rsi;
    void* rbp;
    void* rsp;
    void* rdx;
    void* rcx;
    void* rbx;
    void* rax;
    void* eflags;
    void* return_address;
} trace_state_t;

FILE *trace_log;
static void trace_execution(trace_state_t *state) {
    fprintf(trace_log, "pc:%p sp:%p\n", state->return_address, (void*)((char*)state->rsp + (sizeof(trace_state_t) - offsetof(trace_state_t, rsp))));
    fflush(trace_log);
}

typedef void (*call_trace_execution_fn) (void);

call_trace_execution_fn call_trace_execution;

/* 
 * Write code that can call the c trace_execution function.
 * We write this reusable block to keep generated code consise.
 */
static void write_call_trace_execution(PsycoObject *po) {
    BEGIN_CODE
    PUSH_CC();
    PUSH_R(REG_X64_RAX);
    PUSH_R(REG_X64_RBX);
    PUSH_R(REG_X64_RCX);
    PUSH_R(REG_X64_RDX);
    PUSH_R(REG_X64_RSP);
    PUSH_R(REG_X64_RBP);
    PUSH_R(REG_X64_RSI);
    PUSH_R(REG_X64_RDI);
    PUSH_R(REG_X64_R8);
    PUSH_R(REG_X64_R9);
    PUSH_R(REG_X64_R10);
    PUSH_R(REG_X64_R11);
    PUSH_R(REG_X64_R12);
    PUSH_R(REG_X64_R13);
    PUSH_R(REG_X64_R14);
    PUSH_R(REG_X64_R15);
    LEA_R_O(REG_X64_RDI, REG_X64_RSP, 0);
    CALL_I(&trace_execution);
    POP_R(REG_X64_R15);
    POP_R(REG_X64_R14);
    POP_R(REG_X64_R13);
    POP_R(REG_X64_R12);
    POP_R(REG_X64_R11);
    POP_R(REG_X64_R10);
    POP_R(REG_X64_R9);
    POP_R(REG_X64_R8);
    POP_R(REG_X64_RDI);
    POP_R(REG_X64_RSI);
    POP_R(REG_X64_RBP);
    POP_R(REG_X64_RSP);
    POP_R(REG_X64_RDX);
    POP_R(REG_X64_RCX);
    POP_R(REG_X64_RBX);
    POP_R(REG_X64_RAX);
    POP_CC();
    RET();
    END_CODE;
}
#endif

DEFINEFN
PyObject* psyco_processor_run(CodeBufferObject* codebuf,
                              long initial_stack[],
                              struct stack_frame_info_s*** finfo,
                              PyObject* tdict)
{
  int argc = RUN_ARGC(codebuf);
  int regs_saved = 6;
  int finfo_pushed = 1;
  int return_address = 1;
  int stack_check = CHECK_STACK_DEPTH;
  /* we need to work out if the call to psyco code would be made with the stack 16 bytes aligned
   * and adjust accordingly - we assume everything pushed to the stack is 8 bytes wide
   * and the call from C will be aligned before the call (in the call it is off by 8 due to return address),
   * so we only need to know if an odd number of this is pushed to the stack before the call.
   */
  if((argc + regs_saved + stack_check + finfo_pushed + return_address) & 1) {
    return glue_run_code_unaligned(codebuf->codestart, initial_stack + argc, initial_stack, finfo);
  } else {
    return glue_run_code_aligned(codebuf->codestart, initial_stack + argc, initial_stack, finfo);
  }
}


typedef char (*psyco_int_mul_ovf_fn) (long a, long b);


psyco_int_mul_ovf_fn psyco_int_mul_ovf;


void write_psyco_int_mul_ovf(PsycoObject *po) {
    BEGIN_CODE
    IMUL_R_R(REG_X64_RDI, REG_X64_RSI);
    XOR_R_R(REG_X64_RAX, REG_X64_RAX);
    SET_R_CC(REG_X64_RAX, CC_O);
    RET();
    END_CODE
}



DEFINEFN struct stack_frame_info_s**
psyco_next_stack_frame(struct stack_frame_info_s** finfo)
{
	/* Hack to pick directly from the machine stack the stored
	   "stack_frame_info_t*" pointers */
	return (struct stack_frame_info_s**)
		(((char*) finfo) - finfo_last(*finfo)->link_stack_depth);
}


INITIALIZATIONFN
void psyco_processor_init(void)
{
    code_t *limit;
#if TRACE_EXECUTION_LOG
    code_t *call_trace_execution_loc;
    trace_log = fopen("trace_execution.log", "w");
#endif
    CodeBufferObject* codebuf = psyco_new_code_buffer(NULL, NULL, &limit);
    PsycoObject *po = PsycoObject_New(0);
    po->code = codebuf->codestart;
    po->codelimit = limit;
    glue_run_code_aligned = (glue_run_code_fn)po->code;
    write_glue_run_code_fn(po, true);
    glue_run_code_unaligned = (glue_run_code_fn)po->code;
    write_glue_run_code_fn(po, false);
    psyco_int_mul_ovf = (psyco_int_mul_ovf_fn)po->code;
    write_psyco_int_mul_ovf(po);
#if TRACE_EXECUTION_LOG
    call_trace_execution_loc = po->code;
    write_call_trace_execution(po);
    call_trace_execution = (call_trace_execution_fn)call_trace_execution_loc;
#endif
    SHRINK_CODE_BUFFER(codebuf, po->code, "glue");
    PsycoObject_Delete(po);
}


