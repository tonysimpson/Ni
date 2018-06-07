#ifndef _V_MEM_MOV_H
#define _V_MEM_MOV_H

#include "../psyco.h"
#include "iencoding.h"
#define V_IS_RUNTIME(v) ((v) != NULL && is_runtime(((vinfo_t*)(v))->source))
#define V_GET_REG(v) (V_IS_RUNTIME(v) ? getreg(((vinfo_t*)(v))->source) : REG_NONE)
#define V_MEM_MOV_SCALE_BYTE  0
#define V_MEM_MOV_SCALE_WORD  1
#define V_MEM_MOV_SCALE_DWORD 2
#define V_MEM_MOV_SCALE_QWORD 3
#define V_MEM_MOV_SCALE_POINTER V_MEM_MOV_SCALE_QWORD
#define V_MEM_MOV_SCALE_TO_BYTES(scale) (1 << (scale))
#define V_CT_GET_VALUE(v) CompileTime_Get(((vinfo_t*)(v))->source)->value

static vinfo_t* v_mem_mov(PsycoObject *po, vinfo_t *v, vinfo_t *base, vinfo_t *index, long offset, unsigned char scale, bool signed_) {
    reg_t v_reg, base_reg, index_reg;
    reg_t r;
    long value = 0;
    long offset_or_address = (offset);
    unsigned char op_len = 0, b1 = 0, b2 = 0, b3 = 0;
    bool rex_w = false;
    bool read = false;

    if (v != NULL) {
        /* may emit code */
        if (!compute_vinfo(v, po)) {
            return NULL;
        }
    }
    
    BEGIN_CODE
    if (v == NULL) {
        NEED_FREE_REG_COND(v_reg, (v_reg != V_GET_REG(base) && v_reg != V_GET_REG(index)));
        v = vinfo_new(RunTime_New(v_reg, false, (signed_) == false));
        REG_NUMBER(po, v_reg) = v;
        read = true;
    } else {
        if (V_IS_RUNTIME(v)) {
            v_reg = V_GET_REG(v);
            if (v_reg == REG_NONE) {
                NEED_FREE_REG_COND(v_reg, (v_reg != V_GET_REG(base) && v_reg != V_GET_REG(index)));
                RTVINFO_FROM_STACK_TO_REG(v, v_reg);
            }
        } else {
            v_reg = REG_NONE;
            value = V_CT_GET_VALUE(v);
            /* 
             * If the value is too big we need to load it into a 
             * register first, as x86_64 only allows 32bit signed
             *  extended immediates 
             */
            if(((signed_) && ((value < 0) || (value >= 2147483648))) || (!(signed_) && !FITS_IN_32BITS(value))) {
                v_reg = REG_TRANSIENT_1;
                MOV_R_I(v_reg, value);
            }
        }
    }
    if (V_IS_RUNTIME(base)) {
        base_reg = V_GET_REG(base);
        if (base_reg == REG_NONE) {
            NEED_FREE_REG_COND(base_reg, (base_reg != V_GET_REG(index) && base_reg != V_GET_REG(v)));
            RTVINFO_FROM_STACK_TO_REG(base, base_reg);
        }
    } else {
        base_reg = REG_NONE;
        offset_or_address += V_CT_GET_VALUE(base);
    }
    if (index == NULL) {
        index_reg = REG_NONE;
    } else {
        if (V_IS_RUNTIME(index)) {
            index_reg = V_GET_REG(index);
            if (index_reg == REG_NONE) {
                NEED_FREE_REG_COND(index_reg, (index_reg != V_GET_REG(base) && index_reg != V_GET_REG(v)));
                RTVINFO_FROM_STACK_TO_REG(index, index_reg);
            }
        } else {
            index_reg = REG_NONE;
            offset_or_address += V_CT_GET_VALUE(index) << scale;
        }
    }
    if (read) {
        if (scale == V_MEM_MOV_SCALE_BYTE) {
            op_len = 2;
            b1 = 0x0F;
            b2 = (signed_) ? 0xBE : 0xB6;
        } else if (scale == V_MEM_MOV_SCALE_WORD) {
            op_len = 2;
            b1 = 0x0F;
            b2 = (signed_) ? 0xBF : 0xB7;
        } else if (scale == V_MEM_MOV_SCALE_DWORD) {
            if(signed_) {
                rex_w = true;
                op_len = 1;
                b1 = 0x63;
            } else {
                op_len = 1;
                b1 = 0x8B;
            }
        } else {
            rex_w = true;
            op_len = 1;
           b1 = 0x8B;
        }
    } else {
        if (v_reg != REG_NONE) {
            /* from register */
            if (scale == V_MEM_MOV_SCALE_BYTE) {
                op_len = 1;
                b1 = 0x88;
            } else if (scale == V_MEM_MOV_SCALE_WORD) {
                op_len = 2;
                b1 = 0x66;
                b2 = 0x89;
            } else if (scale == V_MEM_MOV_SCALE_DWORD) {
                op_len = 1;
                b1 = 0x89;
            } else {
                rex_w = true;
                op_len = 1;
                b1 = 0x89;
            }
        } else {
            /* from immediate */
            if (scale == V_MEM_MOV_SCALE_BYTE) {
                op_len = 1;
                b1 = 0xC6;
            } else if (scale == V_MEM_MOV_SCALE_WORD) {
                op_len = 2;
                b1 = 0x66;
                b2 = 0xC7;
            } else if (scale == V_MEM_MOV_SCALE_DWORD) {
                op_len = 1;
                b1 = 0xC7;
            } else {
                rex_w = true;
                op_len = 1;
                b1 = 0xC7;
            }
        }
    }
    r = (v_reg != REG_NONE ? v_reg : 0);
    if (base_reg == REG_NONE) {
        if (index_reg == REG_NONE) {
            ADDRESS_ENCODING(rex_w, op_len, b1, b2, b3, 0, r, offset_or_address);
        } else {
            SIB_ENCODING(false, rex_w, op_len, b1, b2, b3, 0, 0, r, 0x05, offset_or_address, index_reg, V_MEM_MOV_SCALE_TO_BYTES(scale));
        }
    } else if (index_reg == REG_NONE) {
        OFFSET_ENCODING(false, rex_w, op_len, b1, b2, b3, 0, 0, r, base_reg, offset_or_address);
    } else {
        SIB_ENCODING(false, rex_w, op_len, b1, b2, b3, 0, 0, r, base_reg, offset_or_address, index_reg, V_MEM_MOV_SCALE_TO_BYTES(scale));
    }
    if ((!read) && v_reg == REG_NONE) {
        switch (scale) {
            case V_MEM_MOV_SCALE_BYTE:
                WRITE_8BIT(value);
                break;
            case V_MEM_MOV_SCALE_WORD:
                WRITE_16BIT(value);
                break;
            default: /* both dword and qwords in 32bit range */
                WRITE_32BIT(value);
       }
    }
    END_CODE
    return v;
}

#endif /* _V_MEM_MOV_H */
