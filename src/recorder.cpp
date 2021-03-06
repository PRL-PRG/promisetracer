#include "recorder.h"
#include "Timer.h"
#include "lookup.h"

void update_closure_argument(closure_info_t &info, dyntracer_t *dyntracer,
                             call_id_t call_id, const SEXP arg_name,
                             const SEXP arg_value, const SEXP environment,
                             bool dot_argument, int position) {

    arg_t argument;
    SEXPTYPE arg_value_type = TYPEOF(arg_value);
    SEXPTYPE arg_name_type = TYPEOF(arg_name);

    if (arg_name != R_NilValue) {
        argument.name = string(get_name(arg_name));
    } else {
        argument.name = "promise_dyntracer::missing_name";
    }

    if (arg_value_type == PROMSXP) {
        argument.promise_id = get_promise_id(dyntracer, arg_value);
        argument.parameter_mode = PRENV(arg_value) == environment
                                      ? parameter_mode_t::DEFAULT
                                      : parameter_mode_t::CUSTOM;
        argument.promise_environment = PRENV(arg_value);
    } else {
        argument.promise_id = 0;
        argument.parameter_mode = parameter_mode_t::UNKNOWN;
        argument.promise_environment = NULL;
    }

    if (dot_argument) {
        argument.name_type = (sexptype_t)DOTSXP;
    } else {
        argument.name_type = static_cast<sexptype_t>(arg_name_type);
    }

    argument.id = get_argument_id(dyntracer, call_id, argument.name);
    argument.value_type = static_cast<sexptype_t>(arg_value_type);
    argument.formal_parameter_position = position;

    info.arguments.push_back(argument);
}

void update_closure_arguments(closure_info_t &info, dyntracer_t *dyntracer,
                              const call_id_t call_id, const SEXP formals,
                              const SEXP args, const SEXP environment) {

    int formal_parameter_position = 0;
    SEXP arg_name = R_NilValue;
    SEXP arg_value = R_NilValue;
    for (SEXP current_formal = formals; current_formal != R_NilValue;
         current_formal = CDR(current_formal), formal_parameter_position++) {
        // Retrieve the argument name.
        arg_name = TAG(current_formal);
        arg_value = dyntrace_lookup_environment(environment, arg_name);

        // Encountered a triple-dot argument, break it up further.
        if (TYPEOF(arg_value) == DOTSXP) {
            for (SEXP dot_args = arg_value; dot_args != R_NilValue;
                 dot_args = CDR(dot_args)) {
                update_closure_argument(info, dyntracer, call_id, TAG(dot_args),
                                        CAR(dot_args), environment, true,
                                        formal_parameter_position);
            }
        } else {

            // We want the promise associated with the symbol.
            // Generally, the argument value should be the promise.
            // But if JIT is enabled, its possible for the argument_expression
            // to be unpromised. In this case, we dereference the argument.
            if (TYPEOF(arg_value) == SYMSXP) {
                lookup_result r =
                    find_binding_in_environment(arg_value, environment);
                if (r.status == lookup_status::SUCCESS) {
                    arg_value = r.value;
                } else {
                    // So... since this is a function, then I assume we
                    // shouldn't
                    // get any arguments that are active bindings or anything
                    // like
                    // that. If we do, then we should fail here and re-think our
                    // life choices.
                    string msg = lookup_status_to_string(r.status);
                    dyntrace_log_error("%s", msg.c_str());
                }
            }
            // The general case: single argument.
            update_closure_argument(info, dyntracer, call_id, arg_name,
                                    arg_value, environment, false,
                                    formal_parameter_position);
        }
    }

    info.formal_parameter_count = formal_parameter_position;
}

closure_info_t function_entry_get_info(dyntracer_t *dyntracer, const SEXP call,
                                       const SEXP op, const SEXP args,
                                       const SEXP rho) {
    RECORDER_TIMER_RESET();
    closure_info_t info;

    const char *name = get_name(call);
    const char *ns = get_ns_name(op);

    info.fn_compiled = is_byte_compiled(op);
    info.fn_type = function_type::CLOSURE;
    RECORDER_TIMER_END_SEGMENT(FUNCTION_ENTRY_RECORDER_OTHER);

    info.fn_definition = get_function_definition(dyntracer, op);
    RECORDER_TIMER_END_SEGMENT(FUNCTION_ENTRY_RECORDER_DEFINITION);

    info.fn_id = get_function_id(dyntracer, info.fn_definition);
    RECORDER_TIMER_END_SEGMENT(FUNCTION_ENTRY_RECORDER_FUNCTION_ID);

    info.fn_addr = op;
    info.call_ptr = get_sexp_address(rho);
    RECORDER_TIMER_END_SEGMENT(FUNCTION_ENTRY_RECORDER_OTHER);

    info.call_id = make_funcall_id(dyntracer, op);
    RECORDER_TIMER_END_SEGMENT(FUNCTION_ENTRY_RECORDER_CALL_ID);

    stack_event_t event = get_last_on_stack_by_type(
        tracer_state(dyntracer).full_stack, stack_type::CALL);
    info.parent_call_id = event.type == stack_type::NONE ? 0 : event.call_id;
    RECORDER_TIMER_END_SEGMENT(FUNCTION_ENTRY_RECORDER_PARENT_ID);

    info.definition_location = get_definition_location_cpp(op);
    info.callsite_location = get_callsite_cpp(1);
    RECORDER_TIMER_END_SEGMENT(FUNCTION_ENTRY_RECORDER_LOCATION);

    void (*probe)(dyntracer_t *, SEXP);
    probe = dyntrace_active_dyntracer->probe_promise_expression_lookup;
    dyntrace_active_dyntracer->probe_promise_expression_lookup = NULL;
    info.call_expression = get_expression(call);
    RECORDER_TIMER_END_SEGMENT(FUNCTION_ENTRY_RECORDER_EXPRESSION);

    dyntrace_active_dyntracer->probe_promise_expression_lookup = probe;
    if (ns) {
        info.name = string(ns) + "::" + check_string(name);
    } else {
        if (name != NULL)
            info.name = name;
    }
    RECORDER_TIMER_END_SEGMENT(FUNCTION_ENTRY_RECORDER_NAME);

    update_closure_arguments(info, dyntracer, info.call_id, FORMALS(op),
                             FRAME(rho), rho);
    RECORDER_TIMER_END_SEGMENT(FUNCTION_ENTRY_RECORDER_ARGUMENTS);

    get_stack_parent(info, tracer_state(dyntracer).full_stack);
    info.in_prom_id = get_parent_promise(dyntracer);
    RECORDER_TIMER_END_SEGMENT(FUNCTION_ENTRY_RECORDER_PARENT_PROMISE);

    return info;
}

closure_info_t function_exit_get_info(dyntracer_t *dyntracer, const SEXP call,
                                      const SEXP op, const SEXP args,
                                      const SEXP rho, const SEXP retval) {
    RECORDER_TIMER_RESET();
    closure_info_t info;

    const char *name = get_name(call);
    const char *ns = get_ns_name(op);

    info.fn_compiled = is_byte_compiled(op);
    RECORDER_TIMER_END_SEGMENT(FUNCTION_EXIT_RECORDER_OTHER);

    info.fn_definition = get_function_definition(dyntracer, op);
    RECORDER_TIMER_END_SEGMENT(FUNCTION_EXIT_RECORDER_DEFINITION);

    info.fn_id = get_function_id(dyntracer, info.fn_definition);
    RECORDER_TIMER_END_SEGMENT(FUNCTION_EXIT_RECORDER_FUNCTION_ID);

    info.fn_addr = op;

    stack_event_t call_event = get_last_on_stack_by_type(
        tracer_state(dyntracer).full_stack, stack_type::CALL);
    info.call_id = call_event.type == stack_type::NONE ? 0 : call_event.call_id;
    RECORDER_TIMER_END_SEGMENT(FUNCTION_EXIT_RECORDER_CALL_ID);

    info.fn_type = function_type::CLOSURE;
    RECORDER_TIMER_END_SEGMENT(FUNCTION_EXIT_RECORDER_OTHER);

    info.definition_location = get_definition_location_cpp(op);
    info.callsite_location = get_callsite_cpp(0);
    RECORDER_TIMER_END_SEGMENT(FUNCTION_EXIT_RECORDER_LOCATION);

    if (ns) {
        info.name = string(ns) + "::" + check_string(name);
    } else {
        if (name != NULL)
            info.name = name;
    }
    RECORDER_TIMER_END_SEGMENT(FUNCTION_EXIT_RECORDER_NAME);

    update_closure_arguments(info, dyntracer, info.call_id, FORMALS(op),
                             FRAME(rho), rho);
    RECORDER_TIMER_END_SEGMENT(FUNCTION_EXIT_RECORDER_ARGUMENTS);

    info.fn_definition = get_expression(op);

    stack_event_t parent_call = get_from_back_of_stack_by_type(
        tracer_state(dyntracer).full_stack, stack_type::CALL, 1);
    info.parent_call_id =
        parent_call.type == stack_type::NONE ? 0 : parent_call.call_id;
    RECORDER_TIMER_END_SEGMENT(FUNCTION_EXIT_RECORDER_PARENT_ID);

    get_stack_parent2(info, tracer_state(dyntracer).full_stack);
    info.in_prom_id = get_parent_promise(dyntracer);
    RECORDER_TIMER_END_SEGMENT(FUNCTION_EXIT_RECORDER_PARENT_PROMISE);

    info.return_value_type = static_cast<sexptype_t>(TYPEOF(retval));
    RECORDER_TIMER_END_SEGMENT(FUNCTION_EXIT_RECORDER_OTHER);

    return info;
}

builtin_info_t builtin_entry_get_info(dyntracer_t *dyntracer, const SEXP call,
                                      const SEXP op, const SEXP rho,
                                      function_type fn_type) {
    builtin_info_t info;
    const char *name = get_name(call);
    if (name != NULL)
        info.name = name;
    info.fn_definition = get_function_definition(dyntracer, op);
    info.fn_id = get_function_id(dyntracer, info.fn_definition, true);
    info.fn_addr = op;
    info.name = info.name;
    info.fn_type = fn_type;
    info.fn_compiled = is_byte_compiled(op);
    stack_event_t elem = get_last_on_stack_by_type(
        tracer_state(dyntracer).full_stack, stack_type::CALL);
    info.parent_call_id = elem.type == stack_type::NONE ? 0 : elem.call_id;
    info.definition_location = get_definition_location_cpp(op);
    info.callsite_location = get_callsite_cpp(0);
    info.call_ptr = get_sexp_address(rho);
    info.call_id = make_funcall_id(dyntracer, op);

    get_stack_parent(info, tracer_state(dyntracer).full_stack);
    info.in_prom_id = get_parent_promise(dyntracer);
    info.formal_parameter_count = PRIMARITY(op);

    return info;
}

builtin_info_t builtin_exit_get_info(dyntracer_t *dyntracer, const SEXP call,
                                     const SEXP op, const SEXP rho,
                                     function_type fn_type, const SEXP retval) {
    builtin_info_t info;

    const char *name = get_name(call);
    if (name != NULL)
        info.name = name;
    info.fn_definition = get_function_definition(dyntracer, op);
    info.fn_id = get_function_id(dyntracer, info.fn_definition, true);
    info.fn_addr = op;
    stack_event_t elem = get_last_on_stack_by_type(
        tracer_state(dyntracer).full_stack, stack_type::CALL);

    info.call_id = elem.type == stack_type::NONE ? 0 : elem.call_id;
    if (name != NULL)
        info.name = name;
    info.fn_type = fn_type;
    info.fn_compiled = is_byte_compiled(op);
    info.definition_location = get_definition_location_cpp(op);
    info.callsite_location = get_callsite_cpp(0);

    stack_event_t parent_call = get_from_back_of_stack_by_type(
        tracer_state(dyntracer).full_stack, stack_type::CALL, 1);
    info.parent_call_id =
        parent_call.type == stack_type::NONE ? 0 : parent_call.call_id;

    get_stack_parent2(info, tracer_state(dyntracer).full_stack);
    info.in_prom_id = get_parent_promise(dyntracer);
    info.return_value_type = static_cast<sexptype_t>(TYPEOF(retval));
    info.formal_parameter_count = PRIMARITY(op);

    return info;
}

prom_basic_info_t create_promise_get_info(dyntracer_t *dyntracer,
                                          const SEXP promise, const SEXP rho) {
    prom_basic_info_t info;

    info.prom_id = make_promise_id(dyntracer, promise);
    tracer_state(dyntracer).fresh_promises.insert(info.prom_id);

    info.prom_type = static_cast<sexptype_t>(TYPEOF(PRCODE(promise)));
    get_full_type(promise, info.full_type);

    get_stack_parent(info, tracer_state(dyntracer).full_stack);
    info.in_prom_id = get_parent_promise(dyntracer);
    info.depth = get_no_of_ancestor_promises_on_stack(dyntracer);
    void (*probe)(dyntracer_t *, SEXP);
    // probe = dyntrace_active_dyntracer->probe_promise_expression_lookup;
    // dyntrace_active_dyntracer->probe_promise_expression_lookup = NULL;
    info.expression =
        "not computed for efficiency"; // get_expression(PRCODE(promise));
    // dyntrace_active_dyntracer->probe_promise_expression_lookup = probe;
    return info;
}

prom_info_t force_promise_entry_get_info(dyntracer_t *dyntracer,
                                         const SEXP promise) {
    prom_info_t info;
    info.prom_id = get_promise_id(dyntracer, promise);

    stack_event_t elem = get_last_on_stack_by_type(
        tracer_state(dyntracer).full_stack, stack_type::CALL);
    info.in_call_id = elem.type == stack_type::NONE ? 0 : elem.call_id;
    info.from_call_id = tracer_state(dyntracer).promise_origin[info.prom_id];

    info.prom_type = static_cast<sexptype_t>(TYPEOF(PRCODE(promise)));
    get_full_type(promise, info.full_type);
    info.return_type = (sexptype_t)OMEGASXP;
    get_stack_parent(info, tracer_state(dyntracer).full_stack);
    info.in_prom_id = get_parent_promise(dyntracer);
    info.depth = get_no_of_ancestor_promises_on_stack(dyntracer);
    void (*probe)(dyntracer_t *, SEXP);
    // probe = dyntrace_active_dyntracer->probe_promise_expression_lookup;
    // dyntrace_active_dyntracer->probe_promise_expression_lookup = NULL;
    info.expression = "not computed for efficiency";
    get_expression(PRCODE(promise));
    // dyntrace_active_dyntracer->probe_promise_expression_lookup = probe;
    return info;
}

prom_info_t force_promise_exit_get_info(dyntracer_t *dyntracer,
                                        const SEXP promise) {
    prom_info_t info;
    info.prom_id = get_promise_id(dyntracer, promise);

    stack_event_t elem = get_last_on_stack_by_type(
        tracer_state(dyntracer).full_stack, stack_type::CALL);
    info.in_call_id = elem.type == stack_type::NONE ? 0 : elem.call_id;
    info.from_call_id = tracer_state(dyntracer).promise_origin[info.prom_id];

    info.prom_type = static_cast<sexptype_t>(TYPEOF(PRCODE(promise)));
    get_full_type(promise, info.full_type);
    info.return_type = static_cast<sexptype_t>(TYPEOF(PRVALUE(promise)));

    get_stack_parent2(info, tracer_state(dyntracer).full_stack);
    info.in_prom_id = get_parent_promise(dyntracer);
    info.depth = get_no_of_ancestor_promises_on_stack(dyntracer);

    return info;
}

prom_info_t promise_lookup_get_info(dyntracer_t *dyntracer,
                                    const SEXP promise) {
    prom_info_t info;
    info.prom_id = get_promise_id(dyntracer, promise);

    stack_event_t elem = get_last_on_stack_by_type(
        tracer_state(dyntracer).full_stack, stack_type::CALL);
    info.in_call_id = elem.type == stack_type::NONE ? 0 : elem.call_id;
    info.from_call_id = tracer_state(dyntracer).promise_origin[info.prom_id];

    info.prom_type = static_cast<sexptype_t>(TYPEOF(PRCODE(promise)));
    info.full_type.push_back((sexptype_t)OMEGASXP);
    info.return_type = static_cast<sexptype_t>(TYPEOF(PRVALUE(promise)));

    get_stack_parent(info, tracer_state(dyntracer).full_stack);
    info.in_prom_id = get_parent_promise(dyntracer);
    info.depth = get_no_of_ancestor_promises_on_stack(dyntracer);

    return info;
}

prom_info_t promise_expression_lookup_get_info(dyntracer_t *dyntracer,
                                               const SEXP prom) {
    prom_info_t info;

    info.prom_id = get_promise_id(dyntracer, prom);

    stack_event_t elem = get_last_on_stack_by_type(
        tracer_state(dyntracer).full_stack, stack_type::CALL);
    info.in_call_id = elem.type == stack_type::NONE ? 0 : elem.call_id;
    info.from_call_id = tracer_state(dyntracer).promise_origin[info.prom_id];

    info.prom_type = static_cast<sexptype_t>(TYPEOF(PRCODE(prom)));
    info.full_type.push_back((sexptype_t)OMEGASXP);
    info.return_type = static_cast<sexptype_t>(TYPEOF(PRCODE(prom)));

    return info;
}
