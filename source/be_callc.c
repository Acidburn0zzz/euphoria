/*****************************************************************************/
/*      (c) Copyright - See License.txt       */
/*****************************************************************************/
/*                                                                           */
/*                           CALLS TO C ROUTINES                             */
/*                                                                           */
/*****************************************************************************/

/* DIRTY CODE! Modify this to push values onto the call stack 
 * N.B.!!! stack offset for variables "arg", and "argsize"
 * below must be correct! - make a .asm file to be sure.
 *
 * C Calling Conventions (there are many #define synonyms for these):
 * __cdecl: The caller must pop the arguments off the stack after the call.
 *          (i.e. increment the stack pointer, ESP). This allows a 
 *          variable number of arguments to be passed.
 *          This is the default.
 *
 * __stdcall: The subroutine must pop the arguments off the stack.
 *            It assumes a fixed number of arguments are passed.
 *            The WIN32 API is like this.
 *
 * Both conventions return floating-point results on the top of
 * the hardware floating-point stack, except that Watcom is non-standard 
 * for __cdecl, since it returns a pointer to the floating-point result 
 * in EAX, (although the final result could also be on the top of the
 * f.p. stack just by chance).
 *
 * Watcom restores the stack pointer ESP at the end of call_c() below, 
 * just after making the call to the C routine, so it doesn't matter 
 * if it neglects to increment the stack pointer. This means that
 * Watcom can call __cdecl (of other compilers but not it's own!)
 * and __stdcall routines, using the *same* __stdcall convention.
 */           

#include <stdio.h>
#ifdef EWINDOWS
#include <windows.h>
#endif
#include "alldefs.h"
#include "be_runtime.h"
#include "be_machine.h"
#include "be_alloc.h"


/*******************/
/* Local variables */
/*******************/

/* void push() : put /arg/ on to the stack
   void pop()  : GCC:pull /as_offset/ bytes off the stack discarding the stack's values
         WATCOM:do nothing, the stack will be restored at the end of the call to call_c 
		 by code generated by WATCOM.
   */

#if defined(EUNIX) || defined(EMINGW)
#if ARCH == ix86 
#define push() asm("movl %0,%%ecx; pushl (%%ecx);" : /* no out */ : "r"(last_offset) : "%ecx" )
#define  pop() asm( "movl %0,%%ecx; addl (%%ecx),%%esp;" : /* no out */ : "r"(as_offset) : "%ecx" )
#endif
#endif  // EUNIX

#ifdef EMSVC
#define push() __asm { PUSH [last_offset] } do { } while (0)
#define  pop() __asm { ADD esp,[as_offset] } do { } while (0)
#endif

#ifdef EWATCOM
void wcpush(long X);
#define push() wcpush(last_offset);
#define pop()
#pragma aux wcpush = \
		"PUSH [EAX]" \
		modify [ESP] \
		parm [EAX];
#endif // EWATCOM

typedef union {
	double dbl;
	int ints[2];
} double_arg;

typedef union {
	float flt;
	int intval;
} float_arg;

#if !defined(EMINGW) && !defined(EMSVC)
object call_c(int func, object proc_ad, object arg_list)
/* Call a WIN32 or Linux C function in a DLL or shared library. 
   Alternatively, call a machine-code routine at a given address. */
{
	volatile unsigned long arg;  // !!!! magic var to push values on the stack
	volatile int argsize;        // !!!! number of bytes to pop 
	
	s1_ptr arg_list_ptr, arg_size_ptr;
	object_ptr next_arg_ptr, next_size_ptr;
	object next_arg, next_size;
	int iresult, i;
	double dresult;
	double_arg dbl_arg;
	float_arg flt_arg;
	float fresult;
	unsigned long size;
	int proc_index;
	int (*int_proc_address)();
	unsigned return_type;
	unsigned long as_offset;
	unsigned long last_offset;
#if defined(EWINDOWS) && !defined(EWATCOM)
	int cdecl_call;
#endif

	// this code relies on arg always being the first variable and last_offset 
	// always being the last variable
	last_offset = (unsigned long)&arg;
	as_offset = (unsigned long)&argsize;
	// as_offset = last_offset - 4;

	// Setup and Check for Errors
	
	proc_index = get_pos_int("c_proc/c_func", proc_ad); 
	if (proc_index >= (unsigned)c_routine_next) {
		RTFatal("c_proc/c_func: bad routine number (%d)", proc_index);
	}
	
	int_proc_address = c_routine[proc_index].address;
#if defined(EWINDOWS) && !defined(EWATCOM)
	cdecl_call = c_routine[proc_index].convention;
#endif
	if (IS_ATOM(arg_list)) {
		RTFatal("c_proc/c_func: argument list must be a sequence");
	}
	
	arg_list_ptr = SEQ_PTR(arg_list);
	next_arg_ptr = arg_list_ptr->base + arg_list_ptr->length;
	
	// only look at length of arg size sequence for now
	arg_size_ptr = c_routine[proc_index].arg_size;
	next_size_ptr = arg_size_ptr->base + arg_size_ptr->length;
	
	return_type = c_routine[proc_index].return_size; // will be INT
	
	if ((func && return_type == 0) || (!func && return_type != 0) ) {
		if (c_routine[proc_index].name->length < TEMP_SIZE)
			MakeCString(TempBuff, MAKE_SEQ(c_routine[proc_index].name), TEMP_SIZE);
		else
			TempBuff[0] = '\0';
		RTFatal(func ? "%s does not return a value" :
				"%s returns a value",
				TempBuff);
	}
		
	if (arg_list_ptr->length != arg_size_ptr->length) {
		if (c_routine[proc_index].name->length < 100)
			MakeCString(TempBuff, MAKE_SEQ(c_routine[proc_index].name), TEMP_SIZE);
		else
			TempBuff[0] = '\0';
		RTFatal("C routine %s() needs %d argument%s, not %d",
				TempBuff,
				arg_size_ptr->length,
				(arg_size_ptr->length == 1) ? "" : "s",
				arg_list_ptr->length);
	}
	
	argsize = arg_list_ptr->length << 2;
	
	// Push the Arguments
	
	for (i = 1; i <= arg_list_ptr->length; i++) {
	
		next_arg = *next_arg_ptr--;
		next_size = *next_size_ptr--;
		
		if (IS_ATOM_INT(next_size))
			size = INT_VAL(next_size);
		else if (IS_ATOM(next_size))
			size = (unsigned long)DBL_PTR(next_size)->dbl;
		else 
			RTFatal("This C routine was defined using an invalid argument type");

		if (size == C_DOUBLE || size == C_FLOAT) {
			/* push 8-byte double or 4-byte float */
			if (IS_ATOM_INT(next_arg))
				dbl_arg.dbl = (double)next_arg;
			else if (IS_ATOM(next_arg))
				dbl_arg.dbl = DBL_PTR(next_arg)->dbl;
			else { 
				arg = arg+argsize+9999; // 9999 = 270f hex - just a marker for asm code
				RTFatal("arguments to C routines must be atoms");
			}

			if (size == C_DOUBLE) {
				arg = dbl_arg.ints[1];

				push();  // push high-order half first
				argsize += 4;
				arg = dbl_arg.ints[0];
				push(); // don't combine this with the push() below - Lcc bug
			}
			else {
				/* C_FLOAT */
				flt_arg.flt = (float)dbl_arg.dbl;
				arg = (unsigned long)flt_arg.intval;
				push();
			}
		}
		else {
			/* push 4-byte integer */
			if (size >= E_INTEGER) {
				if (IS_ATOM_INT(next_arg)) {
					if (size == E_SEQUENCE)
						RTFatal("passing an integer where a sequence is required");
				}
				else {
					if (IS_SEQUENCE(next_arg)) {
						if (size != E_SEQUENCE && size != E_OBJECT)
							RTFatal("passing a sequence where an atom is required");
					}
					else {
						if (size == E_SEQUENCE)
							RTFatal("passing an atom where a sequence is required");
					}
					RefDS(next_arg);
				}
				arg = next_arg;
				push();
			} 
			else if (IS_ATOM_INT(next_arg)) {
				arg = next_arg;
				push();
			}
			else if (IS_ATOM(next_arg)) {
				// atoms are rounded to integers
				
				arg = (unsigned long)DBL_PTR(next_arg)->dbl; //correct
				// if it's a -ve f.p. number, Watcom converts it to int and
				// then to unsigned int. This is exactly what we want.
				// Works with the others too. 
				push();
			}
			else {
				arg = arg+argsize+9999; // just a marker for asm code
				RTFatal("arguments to C routines must be atoms");
			}
		}
	}    

	// Make the Call - The C compiler thinks it's a 0-argument call
	
	// might be VOID C routine, but shouldn't crash

	if (return_type == C_DOUBLE) {
		// expect double to be returned from C routine
#if defined(EWINDOWS) && !defined(EWATCOM)
		if (cdecl_call) {
			dresult = (*((double (  __cdecl *)())int_proc_address))();
			pop();
		}
		else
#endif          
			dresult = (*((double (__stdcall *)())int_proc_address))();

#ifdef EUNIX       
		pop();
#endif      
		return NewDouble(dresult);
	}
	
	else if (return_type == C_FLOAT) {
		// expect float to be returned from C routine
#if defined(EWINDOWS) && !defined(EWATCOM)
		if (cdecl_call) {
			fresult = (*((float (  __cdecl *)())int_proc_address))();
			pop();
		}
		else
#endif          
			fresult = (*((float (__stdcall *)())int_proc_address))();

#ifdef EUNIX       
		pop();
#endif      
		return NewDouble((double)fresult);
	}
	
	else {
		// expect integer to be returned
#if defined(EWINDOWS) && !defined(EWATCOM)
		if (cdecl_call) {
			iresult = (*((int (  __cdecl *)())int_proc_address))();
			pop();
		}
		else
#endif          
			iresult = (*((int (__stdcall *)())int_proc_address))();
#ifdef EUNIX       
		pop();
#endif      
		if ((return_type & 0x000000FF) == 04) {
			/* 4-byte integer - usual case */
			// check if unsigned result is required 
			if ((return_type & C_TYPE) == 0x02000000) {
				// unsigned integer result
				if ((unsigned)iresult <= (unsigned)MAXINT) {
					return iresult;
				}
				else
					return NewDouble((double)(unsigned)iresult);
			}
			else {
				// signed integer result
				if (return_type >= E_INTEGER ||
					(iresult >= MININT && iresult <= MAXINT)) {
					return iresult;
				}
				else
					return NewDouble((double)iresult);
			}
		}
		else if (return_type == 0) {
			return 0; /* void - procedure */
		}
		/* less common cases */
		else if (return_type == C_UCHAR) {
			return (unsigned char)iresult;
		}
		else if (return_type == C_CHAR) {
			return (signed char)iresult;
		}
		else if (return_type == C_USHORT) {
			return (unsigned short)iresult;
		}
		else if (return_type == C_SHORT) {
			return (short)iresult;
		}
		else
			return 0; // unknown function return type
	}
}

#else //EMINGW
/*******************/
/* Local variables */
/*******************/

/* for c_proc */
typedef void (__stdcall *proc0)();
typedef void (__stdcall *proc1)(long);
typedef void (__stdcall *proc2)(long,long);
typedef void (__stdcall *proc3)(long,long,long);
typedef void (__stdcall *proc4)(long,long,long,long);
typedef void (__stdcall *proc5)(long,long,long,long,long);
typedef void (__stdcall *proc6)(long,long,long,long,long,long);
typedef void (__stdcall *proc7)(long,long,long,long,long,long,long);
typedef void (__stdcall *proc8)(long,long,long,long,long,long,long,long);
typedef void (__stdcall *proc9)(long,long,long,long,long,long,long,long,long);
typedef void (__stdcall *procA)(long,long,long,long,long,long,long,long,long,long);
typedef void (__stdcall *procB)(long,long,long,long,long,long,long,long,long,long,long);
typedef void (__stdcall *procC)(long,long,long,long,long,long,long,long,long,long,long,long);
typedef void (__stdcall *procD)(long,long,long,long,long,long,long,long,long,long,long,long,long);
typedef void (__stdcall *procE)(long,long,long,long,long,long,long,long,long,long,long,long,long,long);
typedef void (__stdcall *procF)(long,long,long,long,long,long,long,long,long,long,long,long,long,long,long);

/* for c_func */
typedef long (__stdcall *func0)();
typedef long (__stdcall *func1)(long);
typedef long (__stdcall *func2)(long,long);
typedef long (__stdcall *func3)(long,long,long);
typedef long (__stdcall *func4)(long,long,long,long);
typedef long (__stdcall *func5)(long,long,long,long,long);
typedef long (__stdcall *func6)(long,long,long,long,long,long);
typedef long (__stdcall *func7)(long,long,long,long,long,long,long);
typedef long (__stdcall *func8)(long,long,long,long,long,long,long,long);
typedef long (__stdcall *func9)(long,long,long,long,long,long,long,long,long);
typedef long (__stdcall *funcA)(long,long,long,long,long,long,long,long,long,long);
typedef long (__stdcall *funcB)(long,long,long,long,long,long,long,long,long,long,long);
typedef long (__stdcall *funcC)(long,long,long,long,long,long,long,long,long,long,long,long);
typedef long (__stdcall *funcD)(long,long,long,long,long,long,long,long,long,long,long,long,long);
typedef long (__stdcall *funcE)(long,long,long,long,long,long,long,long,long,long,long,long,long,long);
typedef long (__stdcall *funcF)(long,long,long,long,long,long,long,long,long,long,long,long,long,long,long);

/* for c_proc */
typedef void (__cdecl *cdproc0)();
typedef void (__cdecl *cdproc1)(long);
typedef void (__cdecl *cdproc2)(long,long);
typedef void (__cdecl *cdproc3)(long,long,long);
typedef void (__cdecl *cdproc4)(long,long,long,long);
typedef void (__cdecl *cdproc5)(long,long,long,long,long);
typedef void (__cdecl *cdproc6)(long,long,long,long,long,long);
typedef void (__cdecl *cdproc7)(long,long,long,long,long,long,long);
typedef void (__cdecl *cdproc8)(long,long,long,long,long,long,long,long);
typedef void (__cdecl *cdproc9)(long,long,long,long,long,long,long,long,long);
typedef void (__cdecl *cdprocA)(long,long,long,long,long,long,long,long,long,long);
typedef void (__cdecl *cdprocB)(long,long,long,long,long,long,long,long,long,long,long);
typedef void (__cdecl *cdprocC)(long,long,long,long,long,long,long,long,long,long,long,long);
typedef void (__cdecl *cdprocD)(long,long,long,long,long,long,long,long,long,long,long,long,long);
typedef void (__cdecl *cdprocE)(long,long,long,long,long,long,long,long,long,long,long,long,long,long);
typedef void (__cdecl *cdprocF)(long,long,long,long,long,long,long,long,long,long,long,long,long,long,long);

/* for c_func */
typedef long (__cdecl *cdfunc0)();
typedef long (__cdecl *cdfunc1)(long);
typedef long (__cdecl *cdfunc2)(long,long);
typedef long (__cdecl *cdfunc3)(long,long,long);
typedef long (__cdecl *cdfunc4)(long,long,long,long);
typedef long (__cdecl *cdfunc5)(long,long,long,long,long);
typedef long (__cdecl *cdfunc6)(long,long,long,long,long,long);
typedef long (__cdecl *cdfunc7)(long,long,long,long,long,long,long);
typedef long (__cdecl *cdfunc8)(long,long,long,long,long,long,long,long);
typedef long (__cdecl *cdfunc9)(long,long,long,long,long,long,long,long,long);
typedef long (__cdecl *cdfuncA)(long,long,long,long,long,long,long,long,long,long);
typedef long (__cdecl *cdfuncB)(long,long,long,long,long,long,long,long,long,long,long);
typedef long (__cdecl *cdfuncC)(long,long,long,long,long,long,long,long,long,long,long,long);
typedef long (__cdecl *cdfuncD)(long,long,long,long,long,long,long,long,long,long,long,long,long);
typedef long (__cdecl *cdfuncE)(long,long,long,long,long,long,long,long,long,long,long,long,long,long);
typedef long (__cdecl *cdfuncF)(long,long,long,long,long,long,long,long,long,long,long,long,long,long,long);

/* for c_func */
typedef float (__stdcall *ffunc0)();
typedef float (__stdcall *ffunc1)(long);
typedef float (__stdcall *ffunc2)(long,long);
typedef float (__stdcall *ffunc3)(long,long,long);
typedef float (__stdcall *ffunc4)(long,long,long,long);
typedef float (__stdcall *ffunc5)(long,long,long,long,long);
typedef float (__stdcall *ffunc6)(long,long,long,long,long,long);
typedef float (__stdcall *ffunc7)(long,long,long,long,long,long,long);
typedef float (__stdcall *ffunc8)(long,long,long,long,long,long,long,long);
typedef float (__stdcall *ffunc9)(long,long,long,long,long,long,long,long,long);
typedef float (__stdcall *ffuncA)(long,long,long,long,long,long,long,long,long,long);
typedef float (__stdcall *ffuncB)(long,long,long,long,long,long,long,long,long,long,long);
typedef float (__stdcall *ffuncC)(long,long,long,long,long,long,long,long,long,long,long,long);
typedef float (__stdcall *ffuncD)(long,long,long,long,long,long,long,long,long,long,long,long,long);
typedef float (__stdcall *ffuncE)(long,long,long,long,long,long,long,long,long,long,long,long,long,long);
typedef float (__stdcall *ffuncF)(long,long,long,long,long,long,long,long,long,long,long,long,long,long,long);

/* for c_func */
typedef float (__cdecl *cdffunc0)();
typedef float (__cdecl *cdffunc1)(long);
typedef float (__cdecl *cdffunc2)(long,long);
typedef float (__cdecl *cdffunc3)(long,long,long);
typedef float (__cdecl *cdffunc4)(long,long,long,long);
typedef float (__cdecl *cdffunc5)(long,long,long,long,long);
typedef float (__cdecl *cdffunc6)(long,long,long,long,long,long);
typedef float (__cdecl *cdffunc7)(long,long,long,long,long,long,long);
typedef float (__cdecl *cdffunc8)(long,long,long,long,long,long,long,long);
typedef float (__cdecl *cdffunc9)(long,long,long,long,long,long,long,long,long);
typedef float (__cdecl *cdffuncA)(long,long,long,long,long,long,long,long,long,long);
typedef float (__cdecl *cdffuncB)(long,long,long,long,long,long,long,long,long,long,long);
typedef float (__cdecl *cdffuncC)(long,long,long,long,long,long,long,long,long,long,long,long);
typedef float (__cdecl *cdffuncD)(long,long,long,long,long,long,long,long,long,long,long,long,long);
typedef float (__cdecl *cdffuncE)(long,long,long,long,long,long,long,long,long,long,long,long,long,long);
typedef float (__cdecl *cdffuncF)(long,long,long,long,long,long,long,long,long,long,long,long,long,long,long);

/* for c_func */
typedef double (__stdcall *dfunc0)();
typedef double (__stdcall *dfunc1)(long);
typedef double (__stdcall *dfunc2)(long,long);
typedef double (__stdcall *dfunc3)(long,long,long);
typedef double (__stdcall *dfunc4)(long,long,long,long);
typedef double (__stdcall *dfunc5)(long,long,long,long,long);
typedef double (__stdcall *dfunc6)(long,long,long,long,long,long);
typedef double (__stdcall *dfunc7)(long,long,long,long,long,long,long);
typedef double (__stdcall *dfunc8)(long,long,long,long,long,long,long,long);
typedef double (__stdcall *dfunc9)(long,long,long,long,long,long,long,long,long);
typedef double (__stdcall *dfuncA)(long,long,long,long,long,long,long,long,long,long);
typedef double (__stdcall *dfuncB)(long,long,long,long,long,long,long,long,long,long,long);
typedef double (__stdcall *dfuncC)(long,long,long,long,long,long,long,long,long,long,long,long);
typedef double (__stdcall *dfuncD)(long,long,long,long,long,long,long,long,long,long,long,long,long);
typedef double (__stdcall *dfuncE)(long,long,long,long,long,long,long,long,long,long,long,long,long,long);
typedef double (__stdcall *dfuncF)(long,long,long,long,long,long,long,long,long,long,long,long,long,long,long);

/* for c_func */
typedef double (__cdecl *cddfunc0)();
typedef double (__cdecl *cddfunc1)(long);
typedef double (__cdecl *cddfunc2)(long,long);
typedef double (__cdecl *cddfunc3)(long,long,long);
typedef double (__cdecl *cddfunc4)(long,long,long,long);
typedef double (__cdecl *cddfunc5)(long,long,long,long,long);
typedef double (__cdecl *cddfunc6)(long,long,long,long,long,long);
typedef double (__cdecl *cddfunc7)(long,long,long,long,long,long,long);
typedef double (__cdecl *cddfunc8)(long,long,long,long,long,long,long,long);
typedef double (__cdecl *cddfunc9)(long,long,long,long,long,long,long,long,long);
typedef double (__cdecl *cddfuncA)(long,long,long,long,long,long,long,long,long,long);
typedef double (__cdecl *cddfuncB)(long,long,long,long,long,long,long,long,long,long,long);
typedef double (__cdecl *cddfuncC)(long,long,long,long,long,long,long,long,long,long,long,long);
typedef double (__cdecl *cddfuncD)(long,long,long,long,long,long,long,long,long,long,long,long,long);
typedef double (__cdecl *cddfuncE)(long,long,long,long,long,long,long,long,long,long,long,long,long,long);
typedef double (__cdecl *cddfuncF)(long,long,long,long,long,long,long,long,long,long,long,long,long,long,long);

float float_std_func(long i, long * op, long len) {
    
	switch(len) {
	    case 0: return ((ffunc0)i)();
	    case 1: return ((ffunc1)i)(op[0]);
	    case 2: return ((ffunc2)i)(op[0],op[1]);
	    case 3: return ((ffunc3)i)(op[0],op[1],op[2]);
	    case 4: return ((ffunc4)i)(op[0],op[1],op[2],op[3]);
	    case 5: return ((ffunc5)i)(op[0],op[1],op[2],op[3],op[4]);
	    case 6: return ((ffunc6)i)(op[0],op[1],op[2],op[3],op[4],op[5]); 
		case 7: return ((func7)i)(op[0],op[1],op[2],op[3],op[4],op[5],op[6]);
	    case 8: return ((ffunc8)i)(op[0],op[1],op[2],op[3],op[4],op[5],op[6],op[7]);
	    case 9: return ((ffunc9)i)(op[0],op[1],op[2],op[3],op[4],op[5],op[6],op[7],op[8]);
	    case 10: return ((ffuncA)i)(op[0],op[1],op[2],op[3],op[4],op[5],op[6],op[7],op[8],op[9]);
	    case 11: return ((ffuncB)i)(op[0],op[1],op[2],op[3],op[4],op[5],op[6],op[7],op[8],op[9],op[10]);
	    case 12: return ((ffuncC)i)(op[0],op[1],op[2],op[3],op[4],op[5],op[6],op[7],op[8],op[9],op[10],op[11]);
	    case 13: return ((ffuncD)i)(op[0],op[1],op[2],op[3],op[4],op[5],op[6],op[7],op[8],op[9],op[10],op[11],op[12]);
	    case 14: return ((ffuncE)i)(op[0],op[1],op[2],op[3],op[4],op[5],op[6],op[7],op[8],op[9],op[10],op[11],op[12],op[13]);
	    case 15: return ((ffuncF)i)(op[0],op[1],op[2],op[3],op[4],op[5],op[6],op[7],op[8],op[9],op[10],op[11],op[12],op[13],op[14]);
	}
    return 0.0;
}

float float_cdecl_func(long i, long * op, long len) {
    
	switch(len) {
	    case 0: return ((cdffunc0)i)();
	    case 1: return ((cdffunc1)i)(op[0]);
	    case 2: return ((cdffunc2)i)(op[0],op[1]);
	    case 3: return ((cdffunc3)i)(op[0],op[1],op[2]);
	    case 4: return ((cdffunc4)i)(op[0],op[1],op[2],op[3]);
	    case 5: return ((cdffunc5)i)(op[0],op[1],op[2],op[3],op[4]);
	    case 6: return ((cdffunc6)i)(op[0],op[1],op[2],op[3],op[4],op[5]);
	    case 7: return ((cdffunc7)i)(op[0],op[1],op[2],op[3],op[4],op[5],op[6]);
	    case 8: return ((cdffunc8)i)(op[0],op[1],op[2],op[3],op[4],op[5],op[6],op[7]);
	    case 9: return ((cdffunc9)i)(op[0],op[1],op[2],op[3],op[4],op[5],op[6],op[7],op[8]);
	    case 10: return ((cdffuncA)i)(op[0],op[1],op[2],op[3],op[4],op[5],op[6],op[7],op[8],op[9]);
	    case 11: return ((cdffuncB)i)(op[0],op[1],op[2],op[3],op[4],op[5],op[6],op[7],op[8],op[9],op[10]);
	    case 12: return ((cdffuncC)i)(op[0],op[1],op[2],op[3],op[4],op[5],op[6],op[7],op[8],op[9],op[10],op[11]);
	    case 13: return ((cdffuncD)i)(op[0],op[1],op[2],op[3],op[4],op[5],op[6],op[7],op[8],op[9],op[10],op[11],op[12]);
	    case 14: return ((cdffuncE)i)(op[0],op[1],op[2],op[3],op[4],op[5],op[6],op[7],op[8],op[9],op[10],op[11],op[12],op[13]);
	    case 15: return ((cdffuncF)i)(op[0],op[1],op[2],op[3],op[4],op[5],op[6],op[7],op[8],op[9],op[10],op[11],op[12],op[13],op[14]);
	}
    return 0.0;
}

double double_std_func(long i, long * op, long len) {
    
	switch(len) {
	    case 0: return ((dfunc0)i)();
	    case 1: return ((dfunc1)i)(op[0]);
	    case 2: return ((dfunc2)i)(op[0],op[1]);
	    case 3: return ((dfunc3)i)(op[0],op[1],op[2]);
	    case 4: return ((dfunc4)i)(op[0],op[1],op[2],op[3]);
	    case 5: return ((dfunc5)i)(op[0],op[1],op[2],op[3],op[4]);
	    case 6: return ((dfunc6)i)(op[0],op[1],op[2],op[3],op[4],op[5]); 
		case 7: return ((func7)i)(op[0],op[1],op[2],op[3],op[4],op[5],op[6]);
	    case 8: return ((dfunc8)i)(op[0],op[1],op[2],op[3],op[4],op[5],op[6],op[7]);
	    case 9: return ((dfunc9)i)(op[0],op[1],op[2],op[3],op[4],op[5],op[6],op[7],op[8]);
	    case 10: return ((dfuncA)i)(op[0],op[1],op[2],op[3],op[4],op[5],op[6],op[7],op[8],op[9]);
	    case 11: return ((dfuncB)i)(op[0],op[1],op[2],op[3],op[4],op[5],op[6],op[7],op[8],op[9],op[10]);
	    case 12: return ((dfuncC)i)(op[0],op[1],op[2],op[3],op[4],op[5],op[6],op[7],op[8],op[9],op[10],op[11]);
	    case 13: return ((dfuncD)i)(op[0],op[1],op[2],op[3],op[4],op[5],op[6],op[7],op[8],op[9],op[10],op[11],op[12]);
	    case 14: return ((dfuncE)i)(op[0],op[1],op[2],op[3],op[4],op[5],op[6],op[7],op[8],op[9],op[10],op[11],op[12],op[13]);
	    case 15: return ((dfuncF)i)(op[0],op[1],op[2],op[3],op[4],op[5],op[6],op[7],op[8],op[9],op[10],op[11],op[12],op[13],op[14]);
	}
    return 0.0;
}

double double_cdecl_func(long i, long * op, long len) {
    
	switch(len) {
	    case 0: return ((cddfunc0)i)();
	    case 1: return ((cddfunc1)i)(op[0]);
	    case 2: return ((cddfunc2)i)(op[0],op[1]);
	    case 3: return ((cddfunc3)i)(op[0],op[1],op[2]);
	    case 4: return ((cddfunc4)i)(op[0],op[1],op[2],op[3]);
	    case 5: return ((cddfunc5)i)(op[0],op[1],op[2],op[3],op[4]);
	    case 6: return ((cddfunc6)i)(op[0],op[1],op[2],op[3],op[4],op[5]);
	    case 7: return ((cddfunc7)i)(op[0],op[1],op[2],op[3],op[4],op[5],op[6]);
	    case 8: return ((cddfunc8)i)(op[0],op[1],op[2],op[3],op[4],op[5],op[6],op[7]);
	    case 9: return ((cddfunc9)i)(op[0],op[1],op[2],op[3],op[4],op[5],op[6],op[7],op[8]);
	    case 10: return ((cddfuncA)i)(op[0],op[1],op[2],op[3],op[4],op[5],op[6],op[7],op[8],op[9]);
	    case 11: return ((cddfuncB)i)(op[0],op[1],op[2],op[3],op[4],op[5],op[6],op[7],op[8],op[9],op[10]);
	    case 12: return ((cddfuncC)i)(op[0],op[1],op[2],op[3],op[4],op[5],op[6],op[7],op[8],op[9],op[10],op[11]);
	    case 13: return ((cddfuncD)i)(op[0],op[1],op[2],op[3],op[4],op[5],op[6],op[7],op[8],op[9],op[10],op[11],op[12]);
	    case 14: return ((cddfuncE)i)(op[0],op[1],op[2],op[3],op[4],op[5],op[6],op[7],op[8],op[9],op[10],op[11],op[12],op[13]);
	    case 15: return ((cddfuncF)i)(op[0],op[1],op[2],op[3],op[4],op[5],op[6],op[7],op[8],op[9],op[10],op[11],op[12],op[13],op[14]);
	}
    return 0.0;
}

void call_std_proc(long i, long * op, long len) {
    
	switch(len) {
	    case 0: ((proc0)i)(); return;
	    case 1: ((proc1)i)(op[0]); return;
	    case 2: ((proc2)i)(op[0],op[1]); return;
	    case 3: ((proc3)i)(op[0],op[1],op[2]); return;
	    case 4: ((proc4)i)(op[0],op[1],op[2],op[3]); return;
	    case 5: ((proc5)i)(op[0],op[1],op[2],op[3],op[4]); return;
	    case 6: ((proc6)i)(op[0],op[1],op[2],op[3],op[4],op[5]); return;
	    case 7: ((proc7)i)(op[0],op[1],op[2],op[3],op[4],op[5],op[6]); return;
	    case 8: ((proc8)i)(op[0],op[1],op[2],op[3],op[4],op[5],op[6],op[7]); return;
	    case 9: ((proc9)i)(op[0],op[1],op[2],op[3],op[4],op[5],op[6],op[7],op[8]); return;
	    case 10: ((procA)i)(op[0],op[1],op[2],op[3],op[4],op[5],op[6],op[7],op[8],op[9]); return;
	    case 11: ((procB)i)(op[0],op[1],op[2],op[3],op[4],op[5],op[6],op[7],op[8],op[9],op[10]); return;
	    case 12: ((procC)i)(op[0],op[1],op[2],op[3],op[4],op[5],op[6],op[7],op[8],op[9],op[10],op[11]); return;
	    case 13: ((procD)i)(op[0],op[1],op[2],op[3],op[4],op[5],op[6],op[7],op[8],op[9],op[10],op[11],op[12]); return;
	    case 14: ((procE)i)(op[0],op[1],op[2],op[3],op[4],op[5],op[6],op[7],op[8],op[9],op[10],op[11],op[12],op[13]); return;
	    case 15: ((procF)i)(op[0],op[1],op[2],op[3],op[4],op[5],op[6],op[7],op[8],op[9],op[10],op[11],op[12],op[13],op[14]); return;
	}
}

long call_std_func(long i, long * op, long len) {
    
	switch(len) {
	    case 0: return ((func0)i)();
	    case 1: return ((func1)i)(op[0]);
	    case 2: return ((func2)i)(op[0],op[1]);
	    case 3: return ((func3)i)(op[0],op[1],op[2]);
	    case 4: return ((func4)i)(op[0],op[1],op[2],op[3]);
	    case 5: return ((func5)i)(op[0],op[1],op[2],op[3],op[4]);
	    case 6: return ((func6)i)(op[0],op[1],op[2],op[3],op[4],op[5]);
	    case 7: return ((func7)i)(op[0],op[1],op[2],op[3],op[4],op[5],op[6]);
	    case 8: return ((func8)i)(op[0],op[1],op[2],op[3],op[4],op[5],op[6],op[7]);
	    case 9: return ((func9)i)(op[0],op[1],op[2],op[3],op[4],op[5],op[6],op[7],op[8]);
	    case 10: return ((funcA)i)(op[0],op[1],op[2],op[3],op[4],op[5],op[6],op[7],op[8],op[9]);
	    case 11: return ((funcB)i)(op[0],op[1],op[2],op[3],op[4],op[5],op[6],op[7],op[8],op[9],op[10]);
	    case 12: return ((funcC)i)(op[0],op[1],op[2],op[3],op[4],op[5],op[6],op[7],op[8],op[9],op[10],op[11]);
	    case 13: return ((funcD)i)(op[0],op[1],op[2],op[3],op[4],op[5],op[6],op[7],op[8],op[9],op[10],op[11],op[12]);
	    case 14: return ((funcE)i)(op[0],op[1],op[2],op[3],op[4],op[5],op[6],op[7],op[8],op[9],op[10],op[11],op[12],op[13]);
	    case 15: return ((funcF)i)(op[0],op[1],op[2],op[3],op[4],op[5],op[6],op[7],op[8],op[9],op[10],op[11],op[12],op[13],op[14]);
	}
    return 0;
}

void call_cdecl_proc(long i, long * op, long len) {
    
	switch(len) {
	    case 0: ((cdproc0)i)(); return;
	    case 1: ((cdproc1)i)(op[0]); return;
	    case 2: ((cdproc2)i)(op[0],op[1]); return;
	    case 3: ((cdproc3)i)(op[0],op[1],op[2]); return;
	    case 4: ((cdproc4)i)(op[0],op[1],op[2],op[3]); return;
	    case 5: ((cdproc5)i)(op[0],op[1],op[2],op[3],op[4]); return;
	    case 6: ((cdproc6)i)(op[0],op[1],op[2],op[3],op[4],op[5]); return;
	    case 7: ((cdproc7)i)(op[0],op[1],op[2],op[3],op[4],op[5],op[6]); return;
	    case 8: ((cdproc8)i)(op[0],op[1],op[2],op[3],op[4],op[5],op[6],op[7]); return;
	    case 9: ((cdproc9)i)(op[0],op[1],op[2],op[3],op[4],op[5],op[6],op[7],op[8]); return;
	    case 10: ((cdprocA)i)(op[0],op[1],op[2],op[3],op[4],op[5],op[6],op[7],op[8],op[9]); return;
	    case 11: ((cdprocB)i)(op[0],op[1],op[2],op[3],op[4],op[5],op[6],op[7],op[8],op[9],op[10]); return;
	    case 12: ((cdprocC)i)(op[0],op[1],op[2],op[3],op[4],op[5],op[6],op[7],op[8],op[9],op[10],op[11]); return;
	    case 13: ((cdprocD)i)(op[0],op[1],op[2],op[3],op[4],op[5],op[6],op[7],op[8],op[9],op[10],op[11],op[12]); return;
	    case 14: ((cdprocE)i)(op[0],op[1],op[2],op[3],op[4],op[5],op[6],op[7],op[8],op[9],op[10],op[11],op[12],op[13]); return;
	    case 15: ((cdprocF)i)(op[0],op[1],op[2],op[3],op[4],op[5],op[6],op[7],op[8],op[9],op[10],op[11],op[12],op[13],op[14]); return;
	}
}

long call_cdecl_func(long i, long * op, long len) {
	switch(len) {
	    case 0: return ((cdfunc0)i)();
	    case 1: return ((cdfunc1)i)(op[0]);
	    case 2: return ((cdfunc2)i)(op[0],op[1]);
	    case 3: return ((cdfunc3)i)(op[0],op[1],op[2]);
	    case 4: return ((cdfunc4)i)(op[0],op[1],op[2],op[3]);
	    case 5: return ((cdfunc5)i)(op[0],op[1],op[2],op[3],op[4]);
	    case 6: return ((cdfunc6)i)(op[0],op[1],op[2],op[3],op[4],op[5]);
	    case 7: return ((cdfunc7)i)(op[0],op[1],op[2],op[3],op[4],op[5],op[6]);
	    case 8: return ((cdfunc8)i)(op[0],op[1],op[2],op[3],op[4],op[5],op[6],op[7]);
	    case 9: return ((cdfunc9)i)(op[0],op[1],op[2],op[3],op[4],op[5],op[6],op[7],op[8]);
	    case 10: return ((cdfuncA)i)(op[0],op[1],op[2],op[3],op[4],op[5],op[6],op[7],op[8],op[9]);
	    case 11: return ((cdfuncB)i)(op[0],op[1],op[2],op[3],op[4],op[5],op[6],op[7],op[8],op[9],op[10]);
	    case 12: return ((cdfuncC)i)(op[0],op[1],op[2],op[3],op[4],op[5],op[6],op[7],op[8],op[9],op[10],op[11]);
	    case 13: return ((cdfuncD)i)(op[0],op[1],op[2],op[3],op[4],op[5],op[6],op[7],op[8],op[9],op[10],op[11],op[12]);
	    case 14: return ((cdfuncE)i)(op[0],op[1],op[2],op[3],op[4],op[5],op[6],op[7],op[8],op[9],op[10],op[11],op[12],op[13]);
	    case 15: return ((cdfuncF)i)(op[0],op[1],op[2],op[3],op[4],op[5],op[6],op[7],op[8],op[9],op[10],op[11],op[12],op[13],op[14]);
	}
    return 0;
}

object call_c(int func, object proc_ad, object arg_list)
/* Call a WIN32 or Linux C function in a DLL or shared library. 
   Alternatively, call a machine-code routine at a given address. */
{
	s1_ptr arg_list_ptr, arg_size_ptr;
	object_ptr next_arg_ptr, next_size_ptr;
	object next_arg, next_size;
	long iresult, i;
	double_arg dbl_arg;
	double dresult;
	float_arg flt_arg;
	float fresult;
	unsigned long size;
	long proc_index;
	long cdecl_call;
	long long_proc_address;
	unsigned return_type;
	char NameBuff[100];
	unsigned long arg;

	long arg_op[16];
	long arg_len;
	long arg_i = 0;
	long is_double, is_float;

	// Setup and Check for Errors
	proc_index = get_pos_int("c_proc/c_func", proc_ad); 
	if (proc_index >= (unsigned)c_routine_next) {
		sprintf(TempBuff, "c_proc/c_func: bad routine number (%ld)", proc_index);
		RTFatal(TempBuff);
	}
	
	long_proc_address = (long)(c_routine[proc_index].address);
#if defined(EWINDOWS) && !defined(EWATCOM)
	cdecl_call = c_routine[proc_index].convention;
#else
	cdecl_call = 1;
#endif
	if (IS_ATOM(arg_list)) {
		RTFatal("c_proc/c_func: argument list must be a sequence");
	}
	
	arg_list_ptr = SEQ_PTR(arg_list);
	next_arg_ptr = arg_list_ptr->base + 1;
	
	// only look at length of arg size sequence for now
	arg_size_ptr = c_routine[proc_index].arg_size;
	next_size_ptr = arg_size_ptr->base + 1;
	
	return_type = c_routine[proc_index].return_size; // will be INT

	arg_len = arg_list_ptr->length;
	
	if ( (func && return_type == 0) || (!func && return_type != 0) ) {
		if (c_routine[proc_index].name->length < 100)
			MakeCString(NameBuff, MAKE_SEQ(c_routine[proc_index].name), c_routine[proc_index].name->length );
		else
			NameBuff[0] = '\0';
		sprintf(TempBuff, func ? "%s does not return a value" :
								 "%s returns a value",
								 NameBuff);
		RTFatal(TempBuff);
	}
		
	if (arg_list_ptr->length != arg_size_ptr->length) {
		if (c_routine[proc_index].name->length < 100)
			MakeCString(NameBuff, MAKE_SEQ(c_routine[proc_index].name), c_routine[proc_index].name->length );
		else
			NameBuff[0] = '\0';
		sprintf(TempBuff, "C routine %s() needs %ld argument%s, not %ld",
						  NameBuff,
						  arg_size_ptr->length,
						  (arg_size_ptr->length == 1) ? "" : "s",
						  arg_list_ptr->length);
		RTFatal(TempBuff);
	}
	
	// Push the Arguments
	
	for (i = 1; i <= arg_list_ptr->length; i++) {
	
		next_arg = *next_arg_ptr++;
		next_size = *next_size_ptr++;
		
		if (IS_ATOM_INT(next_size))
			size = INT_VAL(next_size);
		else if (IS_ATOM(next_size))
			size = (unsigned long)DBL_PTR(next_size)->dbl;
		else 
			RTFatal("This C routine was defined using an invalid argument type");

		if (size == C_DOUBLE || size == C_FLOAT) {
			/* push 8-byte double or 4-byte float */
			if (IS_ATOM_INT(next_arg))
				dbl_arg.dbl = (double)next_arg;
			else if (IS_ATOM(next_arg))
				dbl_arg.dbl = DBL_PTR(next_arg)->dbl;
			else { 
				RTFatal("arguments to C routines must be atoms");
			}

			if (size == C_DOUBLE) {
				#if EBITS == 32
					arg_op[arg_i++] = dbl_arg.ints[1];
					arg_op[arg_i++] = dbl_arg.ints[0];
				#elif EBITS == 64
					arg_op[arg_i++] = dbl_arg.longint;
				#endif
				
			}
			else {
				/* C_FLOAT */
				flt_arg.flt = (float)dbl_arg.dbl;
				arg_op[arg_i++] = (unsigned long)flt_arg.intval;
			}
		}
		else {
			/* push 4-byte longeger */
			if (size >= E_INTEGER) {
				if (IS_ATOM_INT(next_arg)) {
					if (size == E_SEQUENCE)
						RTFatal("passing an longeger where a sequence is required");
				}
				else {
					if (IS_SEQUENCE(next_arg)) {
						if (size != E_SEQUENCE && size != E_OBJECT)
							RTFatal("passing a sequence where an atom is required");
					}
					else {
						if (size == E_SEQUENCE)
							RTFatal("passing an atom where a sequence is required");
					}
					RefDS(next_arg);
				}
				arg = next_arg;
				arg_op[arg_i++] = arg;
			} 
			else if (IS_ATOM_INT(next_arg)) {
				arg = next_arg;
				arg_op[arg_i++] = arg;
			}
			else if (IS_ATOM(next_arg)) {
				// atoms are rounded to longegers
				
				arg = (unsigned long)DBL_PTR(next_arg)->dbl; //correct
				// if it's a -ve f.p. number, Watcom converts it to long and
				// then to unsigned long. This is exactly what we want.
				// Works with the others too. 
				arg_op[arg_i++] = arg;
			}
			else {
				RTFatal("arguments to C routines must be atoms");
			}
		}
	}    

	// Make the Call - The C compiler thinks it's a 0-argument call
	
	// might be VOID C routine, but shouldn't crash
	
	is_double = (return_type == C_DOUBLE);
	is_float = (return_type == C_FLOAT);

	if (func) {
		if (cdecl_call && is_double) {
			dresult = double_cdecl_func(long_proc_address, arg_op, arg_len);
		} else if (cdecl_call && is_float) {
			fresult = float_cdecl_func(long_proc_address, arg_op, arg_len);
		} else if (is_double) {
			dresult = double_std_func(long_proc_address, arg_op, arg_len);
		} else if (is_float) {
			fresult = float_std_func(long_proc_address, arg_op, arg_len);
		} else if (cdecl_call) {
			iresult = call_cdecl_func(long_proc_address, arg_op, arg_len);
		} else {
			iresult = call_std_func(long_proc_address, arg_op, arg_len);
		}
	} else {
		if (cdecl_call) {
			call_cdecl_proc(long_proc_address, arg_op, arg_len);
			iresult = 0;
		} else {
			call_std_proc(long_proc_address, arg_op, arg_len);
			iresult = 0;
		}
	}

	if (return_type == C_DOUBLE) {
		return NewDouble(dresult);
	}
	
	else if (return_type == C_FLOAT) {
		return NewDouble((double)fresult);
	}
	
	else {
		// expect longeger to be returned
		if ((return_type & 0x000000FF) == 04) {
			/* 4-byte longeger - usual case */
			// check if unsigned result is required 
			if ((return_type & C_TYPE) == 0x02000000) {
				// unsigned longeger result
				if ((unsigned)iresult <= (unsigned)MAXINT) {
					return iresult;
				}
				else
					return NewDouble((double)(unsigned)iresult);
			}
			else {
				// signed longeger result
				if (return_type >= E_INTEGER ||
					(iresult >= MININT && iresult <= MAXINT)) {
					return iresult;
				}
				else
					return NewDouble((double)iresult);
			}
		}
		else if (return_type == 0) {
			return 0; /* void - procedure */
		}
		/* less common cases */
		else if (return_type == C_UCHAR) {
			return (unsigned char)iresult;
		}
		else if (return_type == C_CHAR) {
			return (signed char)iresult;
		}
		else if (return_type == C_USHORT) {
			return (unsigned short)iresult;
		}
		else if (return_type == C_SHORT) {
			return (short)iresult;
		}
		else
			return 0; // unknown function return type
	}
}
#endif // EMINGW
