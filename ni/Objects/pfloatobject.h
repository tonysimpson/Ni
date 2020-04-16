 /***************************************************************/
/***             Psyco equivalent of floatobject.h               ***/
 /***************************************************************/

#ifndef _PSY_FLOATOBJECT_H
#define _PSY_FLOATOBJECT_H

#include <iencoding.h>
#if HAVE_FP_FN_CALLS    /* disable float optimizations if functions with
                           float/double arguments are not implemented
                           in the back-end */
#include "pobject.h"
#include "pabstract.h"

#define FLOAT_ob_fval   DEF_FIELD(PyFloatObject, double, ob_fval, OB_type)
#define iFLOAT_OB_FVAL  FIELD_INDEX(FLOAT_ob_fval)
#define FLOAT_TOTAL     FIELDS_TOTAL(FLOAT_ob_fval)

#define PsycoFloat_Check(tp) PyType_TypeCheck(tp, &PyFloat_Type)


/***************************************************************/
/***   Virtual-time object builder                           ***/

/* not-yet-computed integers; it will call PyFloat_FromDouble */
EXTERNVAR source_virtual_t psyco_computed_float;

/* !! consumes a references to vdouble. PsycoFloat_FromLong() does not. */
PSY_INLINE vinfo_t* PsycoFloat_FROM_DOUBLE(vinfo_t* vdouble)
{
	vinfo_t* result = vinfo_new(VirtualTime_New(&psyco_computed_float));
	result->array = array_new(FLOAT_TOTAL);
	result->array->items[iOB_TYPE] =
		vinfo_new(CompileTime_New((long)(&PyFloat_Type)));
	result->array->items[iFLOAT_OB_FVAL] = vdouble;
	return result;
}

PSY_INLINE vinfo_t* PsycoFloat_FromDouble(vinfo_t* vdouble)
{
	vinfo_incref(vdouble);
	return PsycoFloat_FROM_DOUBLE(vdouble);
}

EXTERNFN vinfo_t* PsycoFloat_FromFloat(PsycoObject* po, vinfo_t* vfloat);

PSY_INLINE vinfo_t* PsycoFloat_AS_DOUBLE(PsycoObject* po, vinfo_t* v)
{	/* no type check; does not return a new reference. */
	return psyco_get_const(po, v, FLOAT_ob_fval);
}

/* return a new ref */
EXTERNFN bool PsycoFloat_AsDouble(PsycoObject* po, vinfo_t* v, vinfo_t** vd);

/* return true if successful, false if error or promotion, or
   -1 if the convertion is impossible */
EXTERNFN int psyco_convert_to_double(PsycoObject* po, vinfo_t* vobj,
                                     vinfo_t** pv);

/*EXTERNFN condition_code_t float_cmp(PsycoObject* po, vinfo_t* a1, vinfo_t* a2, 
  vinfo_t* b1, vinfo_t* b2, int op);*/


/* Some C implementations made visible for Modules/pmath.c */
/*
EXTERNFN void 
cimpl_fp_from_long(long value, double* result);
EXTERNFN void 
cimpl_fp_from_float(float value, double* result);
*/

#endif /* HAVE_FP_FN_CALLS */

#endif /* _PSY_FLOATOBJECT_H */
