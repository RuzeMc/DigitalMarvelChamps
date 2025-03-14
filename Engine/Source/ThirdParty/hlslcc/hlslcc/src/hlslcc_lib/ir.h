// Copyright Epic Games, Inc. All Rights Reserved.

// This code is modified from that in the Mesa3D Graphics library available at
// http://mesa3d.org/
// The license for the original code follows:

/* -*- c++ -*- */
/*
* Copyright © 2010 Intel Corporation
*
* Permission is hereby granted, free of charge, to any person obtaining a
* copy of this software and associated documentation files (the "Software"),
* to deal in the Software without restriction, including without limitation
* the rights to use, copy, modify, merge, publish, distribute, sublicense,
* and/or sell copies of the Software, and to permit persons to whom the
* Software is furnished to do so, subject to the following conditions:
*
* The above copyright notice and this permission notice (including the next
* paragraph) shall be included in all copies or substantial portions of the
* Software.
*
* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
* IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
* FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
* THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
* LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
* FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
* DEALINGS IN THE SOFTWARE.
*/

#pragma once
#ifndef IR_H
#define IR_H

#include "ralloc.h"
#include "glsl_types.h"
#include "cclist.h"
#include "ir_visitor.h"
#include "ir_hierarchical_visitor.h"
#include "glsl_parser_extras.h"

/**
* \defgroup IR Intermediate representation nodes
*
* @{
*/

/**
* Class tags
*
* Each concrete class derived from \c ir_instruction has a value in this
* enumerant.  The value for the type is stored in \c ir_instruction::ir_type
* by the constructor.  While using type tags is not very C++, it is extremely
* convenient.  For example, during debugging you can simply inspect
* \c ir_instruction::ir_type to find out the actual type of the object.
*
* In addition, it is possible to use a switch-statement based on \c
* \c ir_instruction::ir_type to select different behavior for different object
* types.  For functions that have only slight differences for several object
* types, this allows writing very straightforward, readable code.
*/
enum ir_node_type
{
	/**
	* Zero is unused so that the IR validator can detect cases where
	* \c ir_instruction::ir_type has not been initialized.
	*/
	ir_type_unset,
	ir_type_variable,
	ir_type_assignment,
	ir_type_call,
	ir_type_constant,
	ir_type_dereference_array,
	ir_type_dereference_image,
	ir_type_dereference_record,
	ir_type_dereference_variable,
	ir_type_discard,
	ir_type_expression,
	ir_type_function,
	ir_type_function_signature,
	ir_type_if,
	ir_type_loop,
	ir_type_loop_jump,
	ir_type_return,
	ir_type_swizzle,
	ir_type_texture,
	ir_type_atomic,
	ir_type_max /**< maximum ir_type enum number, for validation */
};

bool AreEquivalent(class ir_instruction* A, class ir_instruction* B);

/**
* Base class of all IR instructions
*/
class ir_instruction : public exec_node
{
public:
	enum ir_node_type ir_type;

	/** ir_print_visitor helper for debugging. */
	void print(void) const;

	virtual void accept(ir_visitor*) = 0;
	virtual ir_visitor_status accept(ir_hierarchical_visitor*) = 0;
	virtual ir_instruction* clone(void *mem_ctx, struct hash_table* ht) const = 0;

	/**
	* \name IR instruction downcast functions
	*
	* These functions either cast the object to a derived class or return
	* \c NULL if the object's type does not match the specified derived class.
	* Additional downcast functions will be added as needed.
	*/
	/*@{*/
	virtual class ir_variable *          as_variable()         { return NULL; }
	virtual class ir_function *          as_function()         { return NULL; }
	virtual class ir_dereference *       as_dereference()      { return NULL; }
	virtual class ir_dereference_array *	as_dereference_array() { return NULL; }
	virtual class ir_dereference_image *	as_dereference_image() { return NULL; }
	virtual class ir_dereference_record * as_dereference_record() { return NULL; }
	virtual class ir_dereference_variable *as_dereference_variable() { return NULL; }
	virtual class ir_expression *        as_expression()       { return NULL; }
	virtual class ir_rvalue *            as_rvalue()           { return NULL; }
	virtual class ir_loop *              as_loop()             { return NULL; }
	virtual class ir_assignment *        as_assignment()       { return NULL; }
	virtual class ir_call *              as_call()             { return NULL; }
	virtual class ir_return *            as_return()           { return NULL; }
	virtual class ir_if *                as_if()               { return NULL; }
	virtual class ir_swizzle *           as_swizzle()          { return NULL; }
	virtual class ir_constant *          as_constant()         { return NULL; }
	virtual class ir_discard *           as_discard()          { return NULL; }
	virtual class ir_atomic *            as_atomic()           { return NULL; }
	virtual class ir_texture*			as_texture()          { return NULL; }
	/*@}*/

	const int id;
	SSourceLocation SourceLocation;

protected:
	ir_instruction() :
		id(ID++)
	{
		ir_type = ir_type_unset;
		if (id == BREAK_ON_ID)
		{
#if defined(WIN32) && defined(DEBUG)
			__debugbreak();
#endif
		}
	}

	static int ID;
	static int BREAK_ON_ID;
};


/**
* The base class for all "values"/expression trees.
*/
class ir_rvalue : public ir_instruction
{
public:
	const struct glsl_type *type;

	virtual ir_rvalue *clone(void *mem_ctx, struct hash_table *) const override;

	virtual void accept(ir_visitor *v) override
	{
		v->visit(this);
	}

	virtual ir_visitor_status accept(ir_hierarchical_visitor *) override;

	virtual ir_constant* constant_expression_value();

	virtual ir_rvalue * as_rvalue() override
	{
		return this;
	}

	ir_rvalue *as_rvalue_to_saturate();

	virtual bool is_lvalue() const
	{
		return false;
	}

	/**
	* Get the variable that is ultimately referenced by an r-value
	*/
	virtual ir_variable *variable_referenced() const
	{
		return NULL;
	}


	/**
	* If an r-value is a reference to a whole variable, get that variable
	*
	* \return
	* Pointer to a variable that is completely dereferenced by the r-value.  If
	* the r-value is not a dereference or the dereference does not access the
	* entire variable (i.e., it's just one array element, struct field), \c NULL
	* is returned.
	*/
	virtual ir_variable *whole_variable_referenced()
	{
		return NULL;
	}

	/**
	* Determine if an r-value has the value zero
	*
	* The base implementation of this function always returns \c false.  The
	* \c ir_constant class over-rides this function to return \c true \b only
	* for vector and scalar types that have all elements set to the value
	* zero (or \c false for booleans).
	*
	* \sa ir_constant::has_value, ir_rvalue::is_one, ir_rvalue::is_negative_one
	*/
	virtual bool is_zero() const;

	/**
	* Determine if an r-value has the value one
	*
	* The base implementation of this function always returns \c false.  The
	* \c ir_constant class over-rides this function to return \c true \b only
	* for vector and scalar types that have all elements set to the value
	* one (or \c true for booleans).
	*
	* \sa ir_constant::has_value, ir_rvalue::is_zero, ir_rvalue::is_negative_one
	*/
	virtual bool is_one() const;

	/**
	* Determine if an r-value has the value negative one
	*
	* The base implementation of this function always returns \c false.  The
	* \c ir_constant class over-rides this function to return \c true \b only
	* for vector and scalar types that have all elements set to the value
	* negative one.  For boolean times, the result is always \c false.
	*
	* \sa ir_constant::has_value, ir_rvalue::is_zero, ir_rvalue::is_one
	*/
	virtual bool is_negative_one() const;


	/**
	* Return a generic value of error_type.
	*
	* Allocation will be performed with 'mem_ctx' as ralloc owner.
	*/
	static ir_rvalue *error_value(void *mem_ctx);

	/**
	* Return a generic value of void.
	*
	* Allocation will be performed with 'mem_ctx' as ralloc owner.
	*/
	static ir_rvalue *void_value(void *mem_ctx);

protected:
	ir_rvalue();
};


/**
* Variable storage classes
*/
enum ir_variable_mode
{
	ir_var_auto = 0,     /**< Function local variables and globals. */
	ir_var_uniform,      /**< Variable declared as a uniform. */
	ir_var_in,
	ir_var_out,
	ir_var_inout,
	ir_var_const_in,	/**< "in" param that must be a constant expression */
	ir_var_temporary,	/**< Temporary variable generated during compilation. */
	ir_var_shared,	/** Variable declared in shared memory for compute shader */
	ir_var_ref,
	ir_var_ref_image
	/** When updating this enum, check the bitfield size in ir_variable */
};

enum ir_variable_interpolation
{
	ir_interp_qualifier_none = 0,
	ir_interp_qualifier_smooth,
	ir_interp_qualifier_flat,
	ir_interp_qualifier_noperspective
};

/**
* \brief Layout qualifiers for gl_FragDepth.
*
* The AMD/ARB_conservative_depth extensions allow gl_FragDepth to be redeclared
* with a layout qualifier.
*/
enum ir_depth_layout
{
	ir_depth_layout_none, /**< No depth layout is specified. */
	ir_depth_layout_any,
	ir_depth_layout_greater,
	ir_depth_layout_less,
	ir_depth_layout_unchanged
};

/**
* \brief Geometry shader input qualifiers.
*
*/
enum ir_geometry_input
{
	ir_geometry_input_none, /**< Variable is not a geometry shader input. */
	ir_geometry_input_points,
	ir_geometry_input_lines,
	ir_geometry_input_lines_adjacent,
	ir_geometry_input_triangles,
	ir_geometry_input_triangles_adjacent
};

/**
* \brief Convert depth layout qualifier to string.
*/
const char*
depth_layout_string(ir_depth_layout layout);

/**
* Description of built-in state associated with a uniform
*
* \sa ir_variable::state_slots
*/
struct ir_state_slot
{
	int tokens[5];
	int swizzle;
};

class ir_variable : public ir_instruction
{
public:
	ir_variable(const struct glsl_type *, const char *, ir_variable_mode);

	virtual ir_variable *clone(void *mem_ctx, struct hash_table *ht) const override;

	virtual ir_variable *as_variable() override
	{
		return this;
	}

	bool IsEquivalent(ir_variable* IR);

	virtual void accept(ir_visitor *v) override
	{
		v->visit(this);
	}

	virtual ir_visitor_status accept(ir_hierarchical_visitor *) override;


	/**
	* Get the string value for the interpolation qualifier
	*
	* \return The string that would be used in a shader to specify \c
	* mode will be returned.
	*
	* This function is used to generate error messages of the form "shader
	* uses %s interpolation qualifier", so in the case where there is no
	* interpolation qualifier, it returns "no".
	*
	* This function should only be used on a shader input or output variable.
	*/
	const char *interpolation_string() const;

	/**
	* Declared type of the variable
	*/
	const struct glsl_type *type;

	/**
	* Delcared name of the variable
	*/
	const char *name;

	/**
	* Input/output semantic.
	*/
	const char *semantic;

	/**
	* Highest element accessed with a constant expression array index
	*
	* Not used for non-array variables.
	*/
	unsigned max_array_access;

	/**
	* Is the variable read-only?
	*
	* This is set for variables declared as \c const, shader inputs,
	* and uniforms.
	*/
	unsigned read_only : 1;
	unsigned centroid : 1;
	unsigned invariant : 1;

	/**
	* Has this variable been used for reading or writing?
	*
	* Several GLSL semantic checks require knowledge of whether or not a
	* variable has been used.  For example, it is an error to redeclare a
	* variable as invariant after it has been used.
	*/
	unsigned used : 1;

	/**
	* Storage class of the variable.
	*
	* \sa ir_variable_mode
	*/
	ir_variable_mode mode;

	/**
	* When true, arithmetic refactoring will not be applied to expressions assigned to this variable.
	*/
	unsigned precise : 1;

	/**
	* Interpolation mode for shader inputs / outputs
	*
	* \sa ir_variable_interpolation
	*/
	unsigned interpolation : 2;

	/**
	* Geometry shader input characteristic
	*
	* \sa ir_variable_geometryinput
	*/
	unsigned geometryinput : 3;

	/**
	* This variable is a struct or arrays of structs that will be translated to GLSL as interface block.
	*
	* So no struct splitting or variable eliminating is allowed for it.
	*/
	unsigned is_interface_block : 1;

	/**
	* \name ARB_fragment_coord_conventions
	* @{
	*/
	unsigned origin_upper_left : 1;
	unsigned pixel_center_integer : 1;
	/*@}*/

	/**
	* Was the location explicitly set in the shader?
	*
	* If the location is explicitly set in the shader, it \b cannot be changed
	* by the linker or by the API (e.g., calls to \c glBindAttribLocation have
	* no effect).
	*/
	unsigned explicit_location : 1;

	/**
	* Does this variable have an initializer?
	*
	* This is used by the linker to cross-validiate initializers of global
	* variables.
	*/
	unsigned has_initializer : 1;

	/**
	* Has this variable been sampled from? Only valid for sampler type variables.
	*/
	unsigned has_been_sampled : 1;

	/**
	* Image read/write flags for tracking image access modes
	*/
	unsigned image_read : 1;
	unsigned image_write : 1;

	/**
	* Qualifier for coherency
	*/
	unsigned coherent : 1;

	/**

	**/

	unsigned is_patch_constant : 1;

	/**
	* \brief Layout qualifier for gl_FragDepth.
	*
	* This is not equal to \c ir_depth_layout_none if and only if this
	* variable is \c gl_FragDepth and a layout qualifier is specified.
	*/
	ir_depth_layout depth_layout;

	/**
	* Storage location of the base of this variable
	*
	* The precise meaning of this field depends on the nature of the variable.
	*
	*   - Vertex shader input: one of the values from \c gl_vert_attrib.
	*   - Vertex shader output: one of the values from \c gl_vert_result.
	*   - Fragment shader input: one of the values from \c gl_frag_attrib.
	*   - Fragment shader output: one of the values from \c gl_frag_result.
	*   - Uniforms: Per-stage uniform slot number.
	*   - Other: This field is not currently used.
	*
	* If the variable is a uniform, shader input, or shader output, and the
	* slot has not been assigned, the value will be -1.
	*/
	int location;

	/**
	* Built-in state that backs this uniform
	*
	* Once set at variable creation, \c state_slots must remain invariant.
	* This is because, ideally, this array would be shared by all clones of
	* this variable in the IR tree.  In other words, we'd really like for it
	* to be a fly-weight.
	*
	* If the variable is not a uniform, \c num_state_slots will be zero and
	* \c state_slots will be \c NULL.
	*/
	/*@{*/
	unsigned num_state_slots;    /**< Number of state slots used */
	ir_state_slot *state_slots;  /**< State descriptors. */
	/*@}*/

	/**
	* Emit a warning if this variable is accessed.
	*/
	const char *warn_extension;

	/**
	* Value assigned in the initializer of a variable declared "const"
	*/
	ir_constant *constant_value;

	/**
	* Constant expression assigned in the initializer of the variable
	*
	* \warning
	* This field and \c ::constant_value are distinct.  Even if the two fields
	* refer to constants with the same value, they must point to separate
	* objects.
	*/
	ir_constant *constant_initializer;
};


/*@{*/
/**
* The representation of a function instance; may be the full definition or
* simply a prototype.
*/
class ir_function_signature : public ir_instruction
{
	/* An ir_function_signature will be part of the list of signatures in
	* an ir_function.
	*/
public:
	ir_function_signature(const glsl_type *return_type);

	virtual ir_function_signature *clone(void *mem_ctx, struct hash_table *ht) const override;
	ir_function_signature *clone_prototype(void *mem_ctx, struct hash_table *ht) const;

	virtual void accept(ir_visitor *v) override
	{
		v->visit(this);
	}

	virtual ir_visitor_status accept(ir_hierarchical_visitor *) override;

	/**
	* Attempt to evaluate this function as a constant expression, given
	* a list of the actual parameters.  Returns NULL for non-built-ins.
	*/
	ir_constant *constant_expression_value(exec_list *actual_parameters);

	/**
	* Get the name of the function for which this is a signature
	*/
	const char *function_name() const;

	/**
	* Get a handle to the function for which this is a signature
	*
	* There is no setter function, this function returns a \c const pointer,
	* and \c ir_function_signature::_function is private for a reason.  The
	* only way to make a connection between a function and function signature
	* is via \c ir_function::add_signature.  This helps ensure that certain
	* invariants (i.e., a function signature is in the list of signatures for
	* its \c _function) are met.
	*
	* \sa ir_function::add_signature
	*/
	inline const class ir_function *function() const
	{
		return this->_function;
	}

	/**
	* Check whether the qualifiers match between this signature's parameters
	* and the supplied parameter list.  If not, returns the name of the first
	* parameter with mismatched qualifiers (for use in error messages).
	*/
	const char *qualifiers_match(exec_list *params);

	/**
	* Replace the current parameter list with the given one.  This is useful
	* if the current information came from a prototype, and either has invalid
	* or missing parameter names.
	*/
	void replace_parameters(exec_list *new_params);

	/**
	* Function return type.
	*
	* \note This discards the optional precision qualifier.
	*/
	const struct glsl_type *return_type;

	/**
	* Function return semantic.
	*/
	const char *return_semantic;

	/**
	* List of ir_variable of function parameters.
	*
	* This represents the storage.  The paramaters passed in a particular
	* call will be in ir_call::actual_paramaters.
	*/
	struct exec_list parameters;

	/** Whether or not this function has a body (which may be empty). */
	unsigned is_defined : 1;

	/** Whether or not this function signature is a built-in. */
	unsigned is_builtin : 1;

	/** Whether or not this function signature has output parameters. */
	unsigned has_output_parameters : 1;

	/** Whether the function signature is the main shader entry point. */
	unsigned is_main : 1;

	/** When the function signature is geometry shader entry point, maximal number of vertices geometry shader can output. */
	unsigned maxvertexcount;

	/** Whether the function is marked for early depth stencil. */
	unsigned is_early_depth_stencil : 1;

	/** Size of the workgroup for compute shaders. */
	int wg_size_x;
	int wg_size_y;
	int wg_size_z;

	/** State needed for tessellation (hull/domain) shaders */

	glsl_tessellation_info tessellation;


	/** Body of instructions in the function. */
	struct exec_list body;

private:
	/** Function of which this signature is one overload. */
	class ir_function *_function;

	friend class ir_function;
};


/**
* Header for tracking multiple overloaded functions with the same name.
* Contains a list of ir_function_signatures representing each of the
* actual functions.
*/
class ir_function : public ir_instruction
{
public:
	ir_function(const char *name);

	virtual ir_function *clone(void *mem_ctx, struct hash_table *ht) const override;

	virtual ir_function *as_function() override
	{
		return this;
	}

	virtual void accept(ir_visitor *v) override
	{
		v->visit(this);
	}

	virtual ir_visitor_status accept(ir_hierarchical_visitor *) override;

	void add_signature(ir_function_signature *sig)
	{
		sig->_function = this;

		// Use this as an opportunity to check if there are any out parameters in the signature
		bool bHasOut = false;
		foreach_list_const(Node, &sig->parameters)
		{
			ir_variable* Parameter = (ir_variable*)Node;
			if (Parameter->mode == ir_var_out || Parameter->mode == ir_var_inout)
			{
				bHasOut = true;
				break;
			}
		}
		// Assert if somehow it was marked as out and now it's not
		//check(!sig->has_output_parameters || bHasOut);
		sig->has_output_parameters |= bHasOut;

		this->signatures.push_tail(sig);
	}

	/**
	* Get an iterator for the set of function signatures
	*/
	exec_list_iterator iterator()
	{
		return signatures.iterator();
	}

	/**
	* Find a signature that matches a set of actual parameters, taking implicit
	* conversions into account.  Also flags whether the match was exact.
	*/
	ir_function_signature *matching_signature(const exec_list *actual_param,
		bool *match_is_exact);

	/**
	* Find a signature that matches a set of actual parameters, taking implicit
	* conversions into account.
	*/
	ir_function_signature *matching_signature(const exec_list *actual_param);

	/**
	* Find a signature that exactly matches a set of actual parameters without
	* any implicit type conversions.
	*/
	ir_function_signature *exact_matching_signature(const exec_list *actual_ps);

	/**
	* Name of the function.
	*/
	const char *name;

	/** Whether or not this function has a signature that isn't a built-in. */
	bool has_user_signature();

	/**
	* List of ir_function_signature for each overloaded function with this name.
	*/
	struct exec_list signatures;
};

inline const char *ir_function_signature::function_name() const
{
	return this->_function->name;
}
/*@}*/


/**
* IR instruction representing high-level if-statements
*/
class ir_if : public ir_instruction
{
public:
	enum if_mode
	{
		if_branch,
		if_flatten,
		if_dont_care
	};

	ir_if(ir_rvalue* InCondition)
		: condition(InCondition), mode(if_dont_care)
	{
		ir_type = ir_type_if;
	}

	virtual ir_if *clone(void *mem_ctx, struct hash_table *ht) const override;

	virtual ir_if *as_if() override
	{
		return this;
	}

	virtual void accept(ir_visitor *v) override
	{
		v->visit(this);
	}

	virtual ir_visitor_status accept(ir_hierarchical_visitor *) override;

	ir_rvalue *condition;
	/** List of ir_instruction for the body of the then branch */
	exec_list  then_instructions;
	/** List of ir_instruction for the body of the else branch */
	exec_list  else_instructions;

	/** Preferred mode of branch implementation */
	if_mode mode;
};


/**
* IR instruction representing a high-level loop structure.
*/
class ir_loop : public ir_instruction
{
public:
	enum loop_mode
	{
		loop_loop,
		loop_unroll,
		loop_dont_care
	};

	ir_loop();

	virtual ir_loop *clone(void *mem_ctx, struct hash_table *ht) const override;

	virtual void accept(ir_visitor *v) override
	{
		v->visit(this);
	}

	virtual ir_visitor_status accept(ir_hierarchical_visitor *) override;

	virtual ir_loop *as_loop() override
	{
		return this;
	}

	/**
	* Get an iterator for the instructions of the loop body
	*/
	exec_list_iterator iterator()
	{
		return body_instructions.iterator();
	}

	/** List of ir_instruction that make up the body of the loop. */
	exec_list body_instructions;

	/**
	* \name Loop counter and controls
	*
	* Represents a loop like a FORTRAN \c do-loop.
	*
	* \note
	* If \c from and \c to are the same value, the loop will execute once.
	*/
	/*@{*/
	ir_rvalue *from;             /** Value of the loop counter on the first
								 * iteration of the loop.
								 */
	ir_rvalue *to;               /** Value of the loop counter on the last
								 * iteration of the loop.
								 */
	ir_rvalue *increment;
	ir_variable *counter;

	/**
	* Comparison operation in the loop terminator.
	*
	* If any of the loop control fields are non-\c NULL, this field must be
	* one of \c ir_binop_less, \c ir_binop_greater, \c ir_binop_lequal,
	* \c ir_binop_gequal, \c ir_binop_equal, or \c ir_binop_nequal.
	*/
	int cmp;
	/*@}*/

	/** loop mode for unrolling control */
	loop_mode mode;

	/** fastopt mode */
	unsigned int fastopt : 1;

	/** unroll size -1 means no size */
	int unroll_size;
};


class ir_assignment : public ir_instruction
{
public:
	ir_assignment(ir_rvalue *lhs, ir_rvalue *rhs, ir_rvalue *condition = NULL);

	/**
	* Construct an assignment with an explicit write mask
	*
	* \note
	* Since a write mask is supplied, the LHS must already be a bare
	* \c ir_dereference.  The cannot be any swizzles in the LHS.
	*/
	ir_assignment(ir_dereference *lhs, ir_rvalue *rhs, ir_rvalue *condition,
		unsigned write_mask);

	virtual ir_assignment *clone(void *mem_ctx, struct hash_table *ht) const override;

	virtual ir_constant* constant_expression_value();

	virtual void accept(ir_visitor *v) override
	{
		v->visit(this);
	}

	virtual ir_visitor_status accept(ir_hierarchical_visitor *) override;

	virtual ir_assignment * as_assignment() override
	{
		return this;
	}

	bool IsEquivalent(ir_assignment* IR);

	/**
	* Get a whole variable written by an assignment
	*
	* If the LHS of the assignment writes a whole variable, the variable is
	* returned.  Otherwise \c NULL is returned.  Examples of whole-variable
	* assignment are:
	*
	*  - Assigning to a scalar
	*  - Assigning to all components of a vector
	*  - Whole array (or matrix) assignment
	*  - Whole structure assignment
	*/
	ir_variable *whole_variable_written();

	/**
	* Set the LHS of an assignment
	*/
	void set_lhs(ir_rvalue *lhs);

	/**
	* Left-hand side of the assignment.
	*
	* This should be treated as read only.  If you need to set the LHS of an
	* assignment, use \c ir_assignment::set_lhs.
	*/
	ir_dereference *lhs;

	/**
	* Value being assigned
	*/
	ir_rvalue *rhs;

	/**
	* Optional condition for the assignment.
	*/
	ir_rvalue *condition;


	/**
	* Component mask written
	*
	* For non-vector types in the LHS, this field will be zero.  For vector
	* types, a bit will be set for each component that is written.  Note that
	* for \c vec2 and \c vec3 types only the lower bits will ever be set.
	*
	* A partially-set write mask means that each enabled channel gets
	* the value from a consecutive channel of the rhs.  For example,
	* to write just .xyw of gl_FrontColor with color:
	*
	* (assign (constant bool (1)) (xyw)
	*     (var_ref gl_FragColor)
	*     (swiz xyw (var_ref color)))
	*/
	unsigned write_mask;
};

/* Update ir_expression::num_operands() and operator_strs when
* updating this list.
*/
enum ir_expression_operation
{
	ir_unop_bit_not,
	ir_unop_logic_not,
	ir_unop_neg,
	ir_unop_abs,
	ir_unop_sign,
	ir_unop_rcp,
	ir_unop_rsq,
	ir_unop_sqrt,
	ir_unop_exp,      /**< Log base e on gentype */
	ir_unop_log,	     /**< Natural log on gentype */
	ir_unop_exp2,
	ir_unop_log2,

	ir_unop_first_conversion,
	ir_unop_f2i = ir_unop_first_conversion,      /**< Float-to-integer conversion. */
	ir_unop_i2f,      /**< Integer-to-float conversion. */
	ir_unop_f2b,      /**< Float-to-boolean conversion */
	ir_unop_b2f,      /**< Boolean-to-float conversion */
	ir_unop_i2b,      /**< int-to-boolean conversion */
	ir_unop_b2i,      /**< Boolean-to-int conversion */
	ir_unop_b2u,
	ir_unop_u2b,
	ir_unop_f2u,
	ir_unop_u2f,      /**< Unsigned-to-float conversion. */
	ir_unop_i2u,      /**< Integer-to-unsigned conversion. */
	ir_unop_u2i,      /**< Unsigned-to-integer conversion. */
	ir_unop_h2i,
	ir_unop_i2h,
	ir_unop_h2f,
	ir_unop_f2h,
	ir_unop_h2b,
	ir_unop_b2h,
	ir_unop_h2u,
	ir_unop_u2h,
	ir_unop_last_conversion = ir_unop_u2h,

	ir_unop_transpose,

	ir_unop_any,
	ir_unop_all,

	/**
	* \name Unary floating-point rounding operations.
	*/
	/*@{*/
	ir_unop_trunc,
	ir_unop_ceil,
	ir_unop_floor,
	ir_unop_fract,
	ir_unop_round,
	/*@}*/

	/**
	* \name Trigonometric operations.
	*/
	/*@{*/
	ir_unop_sin,
	ir_unop_cos,
	ir_unop_tan,
	ir_unop_asin,
	ir_unop_acos,
	ir_unop_atan,
	ir_unop_sinh,
	ir_unop_cosh,
	ir_unop_tanh,
	/*@}*/

	/**
	* \name Normalize.
	*/
	/*@{*/
	ir_unop_normalize,
	/*@}*/

	/**
	* \name Partial derivatives.
	*/
	/*@{*/
	ir_unop_dFdx,
	ir_unop_dFdy,
	ir_unop_dFdxFine,
	ir_unop_dFdyFine,
	ir_unop_dFdxCoarse,
	ir_unop_dFdyCoarse,
	/*@}*/

	ir_unop_isnan,
	ir_unop_isinf,

	/**
	* bit pattern casting operations
	*/
	ir_unop_fasu,
	ir_unop_fasi,
	ir_unop_iasf,
	ir_unop_uasf,

	/**
	* Integer SM5 operations
	*/
	ir_unop_bitreverse,
	ir_unop_bitcount,
	ir_unop_msb,
	ir_unop_lsb,
	
	ir_unop_saturate,

	ir_unop_noise,

	/**
	* A sentinel marking the last of the unary operations.
	*/
	ir_last_unop = ir_unop_noise,

	ir_binop_add,
	ir_binop_sub,
	ir_binop_mul,
	ir_binop_div,

	/**
	* Takes one of two combinations of arguments:
	*
	* - mod(vecN, vecN)
	* - mod(vecN, float)
	*
	* Does not take integer types.
	*/
	ir_binop_mod,
	ir_binop_modf,

	ir_binop_step,

	/**
	* \name Binary comparison operators which return a boolean vector.
	* The type of both operands must be equal.
	*/
	/*@{*/
	ir_binop_less,
	ir_binop_greater,
	ir_binop_lequal,
	ir_binop_gequal,
	ir_binop_equal,
	ir_binop_nequal,

	ir_binop_first_comparison = ir_binop_less,
	ir_binop_last_comparison = ir_binop_nequal,

	/**
	* Returns single boolean for whether all components of operands[0]
	* equal the components of operands[1].
	*/
	ir_binop_all_equal,
	/**
	* Returns single boolean for whether any component of operands[0]
	* is not equal to the corresponding component of operands[1].
	*/
	ir_binop_any_nequal,
	/*@}*/

	/**
	* \name Bit-wise binary operations.
	*/
	/*@{*/
	ir_binop_lshift,
	ir_binop_rshift,
	ir_binop_bit_and,
	ir_binop_bit_xor,
	ir_binop_bit_or,
	/*@}*/

	ir_binop_logic_and,
	ir_binop_logic_xor,
	ir_binop_logic_or,

	ir_binop_first_logic = ir_binop_logic_and,
	ir_binop_last_logic = ir_binop_logic_or,

	ir_binop_dot,
	ir_binop_cross,
	ir_binop_min,
	ir_binop_max,

	ir_binop_atan2,

	ir_binop_pow,

	/**
	* A sentinel marking the last of the binary operations.
	*/
	ir_last_binop = ir_binop_pow,

	ir_ternop_lerp,
	ir_ternop_smoothstep,
	ir_ternop_clamp,
	ir_ternop_fma,

	/**
	* A sentinel marking the last of the ternary operations.
	*/
	ir_last_ternop = ir_ternop_fma,

	ir_quadop_vector,

	ir_opcode_count,

	/**
	* A sentinel marking the last of all operations.
	*/
	ir_last_opcode = ir_last_ternop,

	ir_invalid_opcode = -1
};

class ir_expression : public ir_rvalue
{
public:
	/**
	* Constructor for unary operation expressions
	*/
	ir_expression(int op, const struct glsl_type *type, ir_rvalue *);
	ir_expression(int op, ir_rvalue *);

	/**
	* Constructor for binary operation expressions
	*/
	ir_expression(int op, const struct glsl_type *type,
		ir_rvalue *, ir_rvalue *);
	ir_expression(int op, ir_rvalue *op0, ir_rvalue *op1);

	/**
	* Constructor for quad operator expressions
	*/
	ir_expression(int op, const struct glsl_type *type,
		ir_rvalue *, ir_rvalue *, ir_rvalue *, ir_rvalue *);

	virtual ir_expression *as_expression() override
	{
		return this;
	}

	bool IsEquivalent(ir_expression* IR);

	virtual ir_expression *clone(void *mem_ctx, struct hash_table *ht) const override;

	/**
	* Attempt to constant-fold the expression
	*
	* If the expression cannot be constant folded, this method will return
	* \c NULL.
	*/
	virtual ir_constant* constant_expression_value() override;

	/**
	* Determine the number of operands used by an expression
	*/
	static unsigned int get_num_operands(ir_expression_operation);

	/**
	* Determine the number of operands used by an expression
	*/
	unsigned int get_num_operands() const
	{
		return (this->operation == ir_quadop_vector)
			? this->type->vector_elements : get_num_operands(operation);
	}

	/**
	* Return a string representing this expression's operator.
	*/
	const char *operator_string();

	/**
	* Return a string representing this expression's operator.
	*/
	static const char *operator_string(ir_expression_operation);


	/**
	* Do a reverse-lookup to translate the given string into an operator.
	*/
	static ir_expression_operation get_operator(const char *);

	virtual void accept(ir_visitor *v) override
	{
		v->visit(this);
	}

	virtual ir_visitor_status accept(ir_hierarchical_visitor *) override;

	ir_expression_operation operation;
	ir_rvalue *operands[4];
};


/**
* HIR instruction representing a high-level function call, containing a list
* of parameters and returning a value in the supplied temporary.
*/
class ir_call : public ir_instruction
{
public:
	ir_call(ir_function_signature* InCallee,
		ir_dereference_variable* InReturnDeref,
		exec_list* InActualParameters)
		: return_deref(InReturnDeref), callee(InCallee)
	{
		ir_type = ir_type_call;
		//check(callee->return_type != NULL);
		InActualParameters->move_nodes_to(&this->actual_parameters);
		this->use_builtin = callee->is_builtin;
	}

	virtual ir_call *clone(void *mem_ctx, struct hash_table *ht) const override;

	virtual ir_constant* constant_expression_value();

	virtual ir_call *as_call() override
	{
		return this;
	}

	virtual void accept(ir_visitor *v) override
	{
		v->visit(this);
	}

	virtual ir_visitor_status accept(ir_hierarchical_visitor *) override;

	/**
	* Get an iterator for the set of acutal parameters
	*/
	exec_list_iterator iterator()
	{
		return actual_parameters.iterator();
	}

	/**
	* Get the name of the function being called.
	*/
	const char *callee_name() const
	{
		return callee->function_name();
	}

	/**
	* Generates an inline version of the function before @ir,
	* storing the return value in return_deref.
	*/
	void generate_inline(ir_instruction *ir);

	/**
	* Storage for the function's return value.
	* This must be NULL if the return type is void.
	*/
	ir_dereference_variable *return_deref;

	/**
	* The specific function signature being called.
	*/
	ir_function_signature *callee;

	/* List of ir_rvalue of paramaters passed in this call. */
	exec_list actual_parameters;

	/** Should this call only bind to a built-in function? */
	bool use_builtin;
};


/**
* \name Jump-like IR instructions.
*
* These include \c break, \c continue, \c return, and \c discard.
*/
/*@{*/
class ir_jump : public ir_instruction
{
protected:
	ir_jump()
	{
		ir_type = ir_type_unset;
	}
};

class ir_return : public ir_jump
{
public:
	ir_return()
		: value(NULL)
	{
		this->ir_type = ir_type_return;
	}

	ir_return(ir_rvalue* InValue)
		: value(InValue)
	{
		this->ir_type = ir_type_return;
	}

	virtual ir_return *clone(void *mem_ctx, struct hash_table *) const override;

	virtual ir_return *as_return() override
	{
		return this;
	}

	bool IsEquivalent(ir_return* IR)
	{
		return AreEquivalent(value, IR->value);
	}

	ir_rvalue *get_value() const
	{
		return value;
	}

	virtual void accept(ir_visitor *v) override
	{
		v->visit(this);
	}

	virtual ir_visitor_status accept(ir_hierarchical_visitor *) override;

	ir_rvalue *value;
};


/**
* Jump instructions used inside loops
*
* These include \c break and \c continue.  The \c break within a loop is
* different from the \c break within a switch-statement.
*
* \sa ir_switch_jump
*/
class ir_loop_jump : public ir_jump
{
public:
	enum jump_mode
	{
		jump_break,
		jump_continue
	};

	ir_loop_jump(jump_mode InMode)
	{
		this->ir_type = ir_type_loop_jump;
		this->mode = InMode;
		//      this->loop = loop;	// unsafe instruction; commenting it out
	}

	virtual ir_loop_jump *clone(void *mem_ctx, struct hash_table *) const override;

	virtual void accept(ir_visitor *v) override
	{
		v->visit(this);
	}

	virtual ir_visitor_status accept(ir_hierarchical_visitor *) override;

	bool is_break() const
	{
		return mode == jump_break;
	}

	bool is_continue() const
	{
		return mode == jump_continue;
	}

	/** Mode selector for the jump instruction. */
	enum jump_mode mode;
private:
	/** Loop containing this break instruction. */
	//   ir_loop *loop;	// commented out because of unsafe initializer
};

/**
* IR instruction representing discard statements.
*/
class ir_discard : public ir_jump
{
public:
	ir_discard()
	{
		this->ir_type = ir_type_discard;
		this->condition = NULL;
	}

	ir_discard(ir_rvalue *cond)
	{
		this->ir_type = ir_type_discard;
		this->condition = cond;
	}

	virtual ir_discard *clone(void *mem_ctx, struct hash_table *ht) const override;

	virtual void accept(ir_visitor *v) override
	{
		v->visit(this);
	}

	virtual ir_visitor_status accept(ir_hierarchical_visitor *) override;

	virtual ir_discard *as_discard() override
	{
		return this;
	}

	bool IsEquivalent(ir_discard* IR)
	{
		return AreEquivalent(condition, IR->condition);
	}

	ir_rvalue *condition;
};
/*@}*/


/**
* Texture sampling opcodes used in ir_texture
*/
enum ir_texture_opcode
{
	ir_tex,		/**< Regular texture look-up */
	ir_txb,		/**< Texture look-up with LOD bias */
	ir_txl,		/**< Texture look-up with explicit LOD */
	ir_txd,		/**< Texture look-up with partial derivatvies */
	ir_txf,		/**< Texel fetch with explicit LOD */
	ir_txs,		/**< Texture size */
	ir_txg,		/**< Texture gather */
	ir_txm		/**< Texture query mip levels */
};

/**
* Texture channel for gather functions
*/
enum ir_texture_channel
{
	ir_channel_none,
	ir_channel_red,
	ir_channel_green,
	ir_channel_blue,
	ir_channel_alpha,
	ir_channel_unknown
};

/**
* IR instruction to sample a texture
*
* The specific form of the IR instruction depends on the \c mode value
* selected from \c ir_texture_opcodes.  In the printed IR, these will
* appear as:
*
*                                    Texel offset (0 or an expression)
*                                    | Projection divisor
*                                    | |  Shadow comparitor
*                                    | |  |
*                                    v v  v
* (tex <type> <sampler> <coordinate> 0 1 ( ))
* (txb <type> <sampler> <coordinate> 0 1 ( ) <bias>)
* (txl <type> <sampler> <coordinate> 0 1 ( ) <lod>)
* (txd <type> <sampler> <coordinate> 0 1 ( ) (dPdx dPdy))
* (txf <type> <sampler> <coordinate> 0       <lod>)
* (txs <type> <sampler> <lod>)
*/
class ir_texture : public ir_rvalue
{
public:
	ir_texture(enum ir_texture_opcode InOp, const SSourceLocation& InSourceLocation)
		: op(InOp), sampler(nullptr), coordinate(nullptr),
		projector(nullptr), shadow_comparitor(nullptr), offset(nullptr),
		channel(ir_channel_none), SamplerStateName(""), SamplerState(nullptr)
	{
		SourceLocation = InSourceLocation;
		this->ir_type = ir_type_texture;
		memset(&lod_info, 0, sizeof(lod_info));
	}

	virtual ir_texture *clone(void *mem_ctx, struct hash_table *) const override;

	virtual ir_constant* constant_expression_value() override;

	virtual void accept(ir_visitor *v) override
	{
		v->visit(this);
	}

	virtual ir_visitor_status accept(ir_hierarchical_visitor *) override;

	virtual class ir_texture* as_texture() override { return this; }

	bool IsEquivalent(ir_texture* IR);

	/**
	* Return a string representing the ir_texture_opcode.
	*/
	const char *opcode_string();

	/** Set the sampler and type. */
	void set_sampler(ir_dereference *sampler, const glsl_type *type);

	/**
	* Do a reverse-lookup to translate a string into an ir_texture_opcode.
	*/
	static ir_texture_opcode get_opcode(const char *);

	enum ir_texture_opcode op;

	/** Sampler to use for the texture access. */
	ir_dereference *sampler;

	/** Texture coordinate to sample */
	ir_rvalue *coordinate;

	/**
	* Value used for projective divide.
	*
	* If there is no projective divide (the common case), this will be
	* \c NULL.  Optimization passes should check for this to point to a constant
	* of 1.0 and replace that with \c NULL.
	*/
	ir_rvalue *projector;

	/**
	* Coordinate used for comparison on shadow look-ups.
	*
	* If there is no shadow comparison, this will be \c NULL.  For the
	* \c ir_txf opcode, this *must* be \c NULL.
	*/
	ir_rvalue *shadow_comparitor;

	/** Texel offset. */
	ir_rvalue *offset;



	union
	{
		ir_rvalue *lod;		/**< Floating point LOD */
		ir_rvalue *bias;		/**< Floating point LOD bias */
		ir_rvalue *sample_index; /** Sample index for MS textures. */
		struct
		{
			ir_rvalue *dPdx;	/**< Partial derivative of coordinate wrt X */
			ir_rvalue *dPdy;	/**< Partial derivative of coordinate wrt Y */
		} grad;
	} lod_info;

	/** Texture channel for gather functions */
	ir_texture_channel channel;

	// HLSL-only
	const char* SamplerStateName;
	ir_rvalue* SamplerState;
};


// Mask		NumComponents	x	y	z	w
//.x			1			0	0	0	0
//.w			1			3	0	0	0
//.xy			2			0	1	0	0
//.xx			2			0	0	0	0
//.yx			2			1	0	0	0
//.xyw			3			0	1	3	0
//.yyy			3			1	1	1	0
//.zyx			3			2	1	0	0
//.xyzw			4			0	1	2	3
//.wzyx			4			3	2	1	0
struct ir_swizzle_mask
{
	unsigned x : 4;
	unsigned y : 4;
	unsigned z : 4;
	unsigned w : 4;

	/**
	* Number of components in the swizzle.
	*/
	unsigned num_components : 3;

	/**
	* Does the swizzle contain duplicate components?
	*
	* L-value swizzles cannot contain duplicate components.
	*/
	unsigned has_duplicates : 1;
};


class ir_swizzle : public ir_rvalue
{
public:
	ir_swizzle(ir_rvalue *, unsigned x, unsigned y, unsigned z, unsigned w,
		unsigned count);

	ir_swizzle(ir_rvalue *val, const unsigned *components, unsigned count);

	ir_swizzle(ir_rvalue *val, ir_swizzle_mask mask);

	virtual ir_swizzle *clone(void *mem_ctx, struct hash_table *) const override;

	virtual ir_constant* constant_expression_value() override;

	virtual ir_swizzle *as_swizzle() override
	{
		return this;
	}

	bool IsEquivalent(ir_swizzle* IR);

	/**
	* Construct an ir_swizzle from the textual representation.  Can fail.
	*/
	static ir_swizzle *create(ir_rvalue *, const char *, unsigned vector_length);

	virtual void accept(ir_visitor *v) override
	{
		v->visit(this);
	}

	virtual ir_visitor_status accept(ir_hierarchical_visitor *) override;

	bool is_lvalue() const override
	{
		return val->is_lvalue() && !mask.has_duplicates;
	}

	/**
	* Get the variable that is ultimately referenced by an r-value
	*/
	virtual ir_variable *variable_referenced() const override;

	ir_rvalue *val;
	ir_swizzle_mask mask;

private:
	/**
	* Initialize the mask component of a swizzle
	*
	* This is used by the \c ir_swizzle constructors.
	*/
	void init_mask(const unsigned *components, unsigned count);
};


class ir_dereference : public ir_rvalue
{
public:
	virtual ir_dereference *clone(void *mem_ctx, struct hash_table *) const override = 0;

	virtual ir_dereference *as_dereference() override
	{
		return this;
	}

	bool is_lvalue() const override;

	/**
	* Get the variable that is ultimately referenced by an r-value
	*/
	virtual ir_variable *variable_referenced() const override = 0;
};


class ir_dereference_variable : public ir_dereference
{
public:
	ir_dereference_variable(ir_variable *var);

	virtual ir_dereference_variable *clone(void *mem_ctx, struct hash_table *) const override;

	virtual ir_constant* constant_expression_value() override;

	virtual ir_dereference_variable *as_dereference_variable() override
	{
		return this;
	}

	bool IsEquivalent(ir_dereference_variable* IR)
	{
		return var->IsEquivalent(IR->var);
	}

	/**
	* Get the variable that is ultimately referenced by an r-value
	*/
	virtual ir_variable *variable_referenced() const override
	{
		return this->var;
	}

	virtual ir_variable *whole_variable_referenced() override
	{
		/* ir_dereference_variable objects always dereference the entire
		* variable.  However, if this dereference is dereferenced by anything
		* else, the complete deferefernce chain is not a whole-variable
		* dereference.  This method should only be called on the top most
		* ir_rvalue in a dereference chain.
		*/
		return this->var;
	}

	virtual void accept(ir_visitor *v) override
	{
		v->visit(this);
	}

	virtual ir_visitor_status accept(ir_hierarchical_visitor *) override;

	/**
	* Object being dereferenced.
	*/
	ir_variable *var;
};


class ir_dereference_array : public ir_dereference
{
public:
	ir_dereference_array(ir_rvalue *value, ir_rvalue *array_index);

	ir_dereference_array(ir_variable *var, ir_rvalue *array_index);

	virtual ir_dereference_array *clone(void *mem_ctx, struct hash_table *) const override;

	virtual ir_constant* constant_expression_value() override;

	virtual ir_dereference_array *as_dereference_array() override
	{
		return this;
	}

	bool IsEquivalent(ir_dereference_array* IR);

	/**
	* Get the variable that is ultimately referenced by an r-value
	*/
	virtual ir_variable *variable_referenced() const override
	{
		return this->array->variable_referenced();
	}

	virtual void accept(ir_visitor *v) override
	{
		v->visit(this);
	}

	virtual ir_visitor_status accept(ir_hierarchical_visitor *) override;

	ir_rvalue *array;
	ir_rvalue *array_index;

private:
	void set_array(ir_rvalue *value);
};

enum ir_image_op
{
	ir_image_access,
	ir_image_dimensions
};

class ir_dereference_image : public ir_dereference
{
public:
	ir_dereference_image(ir_rvalue *value, ir_rvalue *array_index);

	ir_dereference_image(ir_variable *var, ir_rvalue *array_index);

	ir_dereference_image(ir_rvalue *value, ir_rvalue *array_index, ir_image_op op);

	ir_dereference_image(ir_variable *var, ir_rvalue *array_index, ir_image_op op);

	virtual ir_dereference_image *clone(void *mem_ctx, struct hash_table *) const override;

	virtual ir_constant* constant_expression_value() override;

	virtual ir_dereference_image *as_dereference_image() override
	{
		return this;
	}

	bool IsEquivalent(ir_dereference_image* IR);

	/**
	* Get the variable that is ultimately referenced by an r-value
	*/
	virtual ir_variable *variable_referenced() const override
	{
		return this->image->variable_referenced();
	}

	virtual void accept(ir_visitor *v) override
	{
		v->visit(this);
	}

	virtual ir_visitor_status accept(ir_hierarchical_visitor *) override;

	ir_rvalue *image;
	ir_rvalue *image_index;

	ir_image_op op;

private:
};


class ir_dereference_record : public ir_dereference
{
public:
	ir_dereference_record(ir_rvalue *value, const char *field);

	ir_dereference_record(ir_variable *var, const char *field);

	virtual ir_dereference_record *clone(void *mem_ctx, struct hash_table *) const override;

	virtual ir_constant* constant_expression_value() override;

	virtual ir_dereference_record *as_dereference_record() override
	{
		return this;
	}

	bool IsEquivalent(ir_dereference_record* IR);

	/**
	* Get the variable that is ultimately referenced by an r-value
	*/
	virtual ir_variable *variable_referenced() const override
	{
		return this->record->variable_referenced();
	}

	virtual void accept(ir_visitor *v) override
	{
		v->visit(this);
	}

	virtual ir_visitor_status accept(ir_hierarchical_visitor *) override;

	ir_rvalue *record;
	const char *field;
};


/**
* Data stored in an ir_constant
*/
union ir_constant_data
{
	unsigned u[16];
	int i[16];
	float f[16];
	bool b[16];
};


class ir_constant : public ir_rvalue
{
public:
	ir_constant(const struct glsl_type *type, const ir_constant_data *data);
	ir_constant(bool b);
	ir_constant(unsigned int u);
	ir_constant(int i);
	ir_constant(float f);

	/**
	* Construct an ir_constant from a list of ir_constant values
	*/
	ir_constant(const struct glsl_type *type, exec_list *values);

	/**
	* Construct an ir_constant from a scalar component of another ir_constant
	*
	* The new \c ir_constant inherits the type of the component from the
	* source constant.
	*
	* \note
	* In the case of a matrix constant, the new constant is a scalar, \b not
	* a vector.
	*/
	ir_constant(const ir_constant *c, unsigned i);

	/**
	* Return a new ir_constant of the specified type containing all zeros.
	*/
	static ir_constant *zero(void *mem_ctx, const glsl_type *type);

	virtual ir_constant *clone(void *mem_ctx, struct hash_table *) const override;

	virtual ir_constant* constant_expression_value() override;

	virtual ir_constant *as_constant() override
	{
		return this;
	}

	bool IsEquivalent(ir_constant* IR);

	virtual void accept(ir_visitor *v) override
	{
		v->visit(this);
	}

	virtual ir_visitor_status accept(ir_hierarchical_visitor *) override;

	/**
	* Get a particular component of a constant as a specific type
	*
	* This is useful, for example, to get a value from an integer constant
	* as a float or bool.  This appears frequently when constructors are
	* called with all constant parameters.
	*/
	/*@{*/
	bool get_bool_component(unsigned i) const;
	float get_float_component(unsigned i) const;
	int get_int_component(unsigned i) const;
	unsigned get_uint_component(unsigned i) const;

	// can't overload on return type, so this overload makes it template "friendly"
	void get_component(bool& result, unsigned i) const  { result = get_bool_component(i); }
	void get_component(float& result, unsigned i) const  { result = get_float_component(i); }
	void get_component(int& result, unsigned i) const  { result = get_int_component(i); }
	void get_component(unsigned& result, unsigned i) const  { result = get_uint_component(i); }
	/*@}*/

	ir_constant *get_array_element(unsigned i) const;

	ir_constant *get_record_field(const char *name);

	/**
	* Determine whether a constant has the same value as another constant
	*
	* \sa ir_constant::is_zero, ir_constant::is_one,
	* ir_constant::is_negative_one
	*/
	bool has_value(const ir_constant *) const;

	virtual bool is_zero() const override;
	virtual bool is_one() const override;
	virtual bool is_negative_one() const override;
	bool is_finite() const;
	bool is_component_finite(unsigned i) const;
	bool are_any_zero() const;

	/**
	* Value of the constant.
	*
	* The field used to back the values supplied by the constant is determined
	* by the type associated with the \c ir_instruction.  Constants may be
	* scalars, vectors, or matrices.
	*/
	union ir_constant_data value;

	/* Array elements */
	ir_constant **array_elements;

	/* Structure fields */
	exec_list components;

private:
	/**
	* Parameterless constructor only used by the clone method
	*/
	ir_constant(void);
};


enum ir_atomic_op
{
	ir_atomic_add,
	ir_atomic_and,
	ir_atomic_min,
	ir_atomic_max,
	ir_atomic_or,
	ir_atomic_xor,
	ir_atomic_swap,
	ir_atomic_cmp_swap,
	ir_atomic_load,
	ir_atomic_store,
	ir_atomic_count
};

/**
* HIR instruction representing an atomic operation.
*/
class ir_atomic : public ir_instruction
{
public:
	ir_atomic(ir_atomic_op op,
		ir_dereference *result,
		ir_dereference *target,
		ir_rvalue *arg0,
		ir_rvalue *arg1
		)
		: memory_ref(target), lhs(result), operation(op)
	{
		ir_type = ir_type_atomic;
		//check(target != NULL);
		//check(arg1 == NULL || op == ir_atomic_cmp_swap);
		//check(op < ir_atomic_count);
		operands[0] = arg0;
		operands[1] = arg1;
	}

	virtual ir_atomic *clone(void *mem_ctx, struct hash_table *ht) const override;

	virtual ir_constant *constant_expression_value()
	{
		return NULL;
	}

	virtual ir_atomic *as_atomic() override
	{
		return this;
	}

	bool IsEquivalent(ir_atomic* IR);

	virtual void accept(ir_visitor *v) override
	{
		v->visit(this);
	}

	virtual ir_visitor_status accept(ir_hierarchical_visitor *) override;

	const char* operator_string();

	/**
	* Value to perform the atomic on
	*/
	ir_dereference *memory_ref;

	/**
	* Var to receive the result from the atomic
	*/
	ir_dereference *lhs;

	/**
	* arguments
	*/
	ir_rvalue *operands[2];

	/**
	*
	*/
	ir_atomic_op operation;

};

/*@}*/

/**
* Apply a visitor to each IR node in a list
*/
void visit_exec_list(exec_list *list, ir_visitor *visitor);

/**
* Validate invariants on each IR node in a list
*/
void validate_ir_tree(exec_list *instructions, struct _mesa_glsl_parse_state *state);

struct _mesa_glsl_parse_state;
struct gl_shader_program;

/**
* Detect whether an unlinked shader contains static recursion
*
* If the list of instructions is determined to contain static recursion,
* \c _mesa_glsl_error will be called to emit error messages for each function
* that is in the recursion cycle.
*/
void detect_recursion_unlinked(struct _mesa_glsl_parse_state *state, exec_list *instructions);

/**
* Make a clone of each IR instruction in a list
*
* \param in   List of IR instructions that are to be cloned
* \param out  List to hold the cloned instructions
*/
void clone_ir_list(void *mem_ctx, exec_list *out, const exec_list *in);

extern void _mesa_glsl_initialize_functions(exec_list *instructions,
	_mesa_glsl_parse_state *state);

struct glsl_symbol_table;

extern void import_prototypes(const exec_list *source, exec_list *dest,
	struct glsl_symbol_table *symbols, void *mem_ctx);

extern char* prototype_string(const glsl_type *return_type, const char *name, exec_list *parameters);

ir_rvalue* gen_texture_op(
	const class ast_expression *expr,
	ir_dereference *sampler,
	exec_list *instructions,
struct _mesa_glsl_parse_state *state);

ir_rvalue* gen_image_op(
	const class ast_expression *expr,
	ir_dereference *image,
	exec_list *instructions,
struct _mesa_glsl_parse_state *state);

bool apply_type_conversion(
	const struct glsl_type *to, ir_rvalue * &from,
	exec_list* instructions, struct _mesa_glsl_parse_state *state,
	bool is_explicit, const struct YYLTYPE *loc);

ir_rvalue* validate_assignment(struct _mesa_glsl_parse_state *state, exec_list *instructions,
	const glsl_type *lhs_type, ir_rvalue *rhs, bool is_initializer, const YYLTYPE *loc);

// bAIsLHS is used on modify/Op cases (A*=B), where we want to coerce B to match A's type
const struct glsl_type* arithmetic_result_type(ir_rvalue * &value_a, ir_rvalue * &value_b,
	exec_list *instructions, _mesa_glsl_parse_state *state, YYLTYPE *loc, bool bAIsLHS);

unsigned ir_hash_table_pointer_hash(const void *key);

int ir_hash_table_pointer_compare(const void *key1, const void *key2);

#endif /* IR_H */
