#include "pclassobject.h"
#include "ptupleobject.h"


DEFINEFN
vinfo_t* PsycoMethod_New(PyObject* func, vinfo_t* self, PyObject* cls)
{
	vinfo_t* result = vinfo_new(VirtualTime_New(&psyco_computed_method));
	
	extra_assert(METHOD_SIZE > METHOD_IM_FUNC);
	extra_assert(METHOD_SIZE > METHOD_IM_SELF);
	extra_assert(METHOD_SIZE > METHOD_IM_CLASS);
	
	result->array = array_new(METHOD_SIZE);
	result->array->items[OB_TYPE] =
		vinfo_new(CompileTime_New((long)(&PyMethod_Type)));
        
        Py_INCREF(func);
	result->array->items[METHOD_IM_FUNC] =
		vinfo_new(CompileTime_NewSk(sk_new((long) func, SkFlagPyObj)));
        
	vinfo_incref(self);
	result->array->items[METHOD_IM_SELF] = self;
        
        Py_INCREF(cls);
	result->array->items[METHOD_IM_CLASS] =
		vinfo_new(CompileTime_NewSk(sk_new((long) cls, SkFlagPyObj)));
        
	return result;
}


static bool compute_method(PsycoObject* po, vinfo_t* methobj)
{
	vinfo_t* newobj;
	vinfo_t* im_func;
	vinfo_t* im_self;
	vinfo_t* im_class;
	
	/* get the fields from the Python object 'methobj' */
	im_func = get_array_item(po, methobj, METHOD_IM_FUNC);
	if (im_func == NULL)
		return false;
	im_self = get_array_item(po, methobj, METHOD_IM_SELF);
	if (im_self == NULL)
		return false;
	im_class = get_array_item(po, methobj, METHOD_IM_CLASS);
	if (im_class == NULL)
		return false;

	/* call PyMethod_New() */
	newobj = psyco_generic_call(po, PyMethod_New,
				    CfPure|CfReturnRef|CfPyErrIfNull,
				    "vvv", im_func, im_self, im_class);
	if (newobj == NULL)
		return false;

	/* move the resulting non-virtual Python object back into 'methobj' */
	vinfo_move(po, methobj, newobj);
	return true;
}


DEFINEVAR source_virtual_t psyco_computed_method;


 /***************************************************************/
  /*** instance method objects meta-implementation             ***/


static vinfo_t* pinstancemethod_call(PsycoObject* po, vinfo_t* methobj,
				     vinfo_t* arg, vinfo_t* kw)
{
	vinfo_t* im_func;
	vinfo_t* im_self;
	vinfo_t* result;
	condition_code_t cc;
	
	/* get the 'im_self' field from the Python object 'methobj' */
	im_self = get_array_item(po, methobj, METHOD_IM_SELF);
	if (im_self == NULL)
		return NULL;

	cc = integer_non_null(po, im_self);
	if (cc == CC_ERROR)  /* error or more likely promotion */
		return NULL;
	
	if (!runtime_condition_t(po, cc)) {
		/* Unbound methods, XXX implement me */
		goto fallback;
	}
	else
	{
		int i, argcount;
		vinfo_t* newarg;
		if (PycException_Occurred(po))
			return NULL;
		
		argcount = PsycoTuple_Load(arg);
		if (argcount < 0)
			goto fallback;
		
		newarg = PsycoTuple_New(argcount+1, NULL);
		vinfo_incref(im_self);
		PsycoTuple_GET_ITEM(newarg, 0) = im_self;
		for (i = 0; i < argcount; i++) {
			vinfo_t* v =  PsycoTuple_GET_ITEM(arg, i);
			vinfo_incref(v);
			PsycoTuple_GET_ITEM(newarg, i+1) = v;
		}
		arg = newarg;
	}

	im_func = get_array_item(po, methobj, METHOD_IM_FUNC);
	if (im_func == NULL)
		result = NULL;
	else
		result = PsycoObject_Call(po, im_func, arg, kw);
	vinfo_decref(arg, po);
	return result;

  fallback:
	return psyco_generic_call(po, PyMethod_Type.tp_call,
				  CfReturnRef|CfPyErrIfNull,
				  "vvv", methobj, arg, kw);
}


INITIALIZATIONFN
void psy_classobject_init(void)
{
	Psyco_DefineMeta(PyMethod_Type.tp_call, pinstancemethod_call);
	psyco_computed_method.compute_fn = &compute_method;
}
