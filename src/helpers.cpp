#include "Context.h"
#include "State.h"
#include "lookup.h"
#include "utilities.h"
#include <cassert>
#include <sstream>

rid_t get_sexp_address(SEXP e) { return (rid_t)e; }

prom_id_t get_promise_id(dyntrace_context_t *context, SEXP promise) {
    if (promise == R_NilValue)
        return RID_INVALID;

    if (TYPEOF(promise) != PROMSXP)
        return RID_INVALID;

    // A new promise is always created for each argument.
    // Even if the argument is already a promise passed from the caller, it gets
    // re-wrapped.
    prom_addr_t prom_addr = get_sexp_address(promise);
    prom_id_t prom_id;

    unsigned int prom_type = TYPEOF(PRCODE(promise));
    unsigned int orig_type =
        (prom_type == BCODESXP) ? TYPEOF(BODY_EXPR(PRCODE(promise))) : 0;
    prom_key_t key(prom_addr, prom_type, orig_type);

    auto &promise_ids = tracer_state(context).promise_ids;
    auto it = promise_ids.find(key);
    if (it != promise_ids.end()) {
        prom_id = it->second;
    } else {
        prom_id = make_promise_id(context, promise, true);
    }

    return prom_id;
}

prom_id_t make_promise_id(dyntrace_context_t *context, SEXP promise,
                          bool negative) {
    if (promise == R_NilValue)
        return RID_INVALID;

    prom_addr_t prom_addr = get_sexp_address(promise);
    prom_id_t prom_id;

    if (negative) {
        prom_id = --tracer_state(context).prom_neg_id_counter;
    } else {
        prom_id = tracer_state(context).prom_id_counter++;
    }
    unsigned int prom_type = TYPEOF(PRCODE(promise));
    unsigned int orig_type =
        (prom_type == 21) ? TYPEOF(BODY_EXPR(PRCODE(promise))) : 0;
    prom_key_t key(prom_addr, prom_type, orig_type);
    tracer_state(context).promise_ids[key] = prom_id;

    auto &already_inserted_negative_promises =
        tracer_state(context).already_inserted_negative_promises;
    already_inserted_negative_promises.insert(prom_id);

    return prom_id;
}

string get_function_definition(dyntrace_context_t *context, const SEXP function) {
    auto &definitions = tracer_state(context).function_definitions;
    auto it = definitions.find(function);
    if (it != definitions.end()) {
#ifdef RDT_DEBUG
        string test = get_expression(function);
        if (it->second.compare(test) != 0) {
            cout << "Function definitions are wrong.";
        }
#endif
        return it->second;
    } else {
        string definition = get_expression(function);
        tracer_state(context).function_definitions[function] = definition;
        return definition;
    }
}

void remove_function_definition(dyntrace_context_t *context, const SEXP function) {
    auto &definitions = tracer_state(context).function_definitions;
    auto it = definitions.find(function);
    if (it != definitions.end())
        tracer_state(context).function_definitions.erase(it);
}

fn_id_t get_function_id(dyntrace_context_t *context,const string &function_definition, bool builtin) {
    fn_key_t definition(function_definition);

    auto &function_ids = tracer_state(context).function_ids;
    auto it = function_ids.find(definition);

    if (it != function_ids.end()) {
        return it->second;
    } else {
        /*Use hash on the function body to compute a unique (hopefully) id
         for each function.*/

        fn_id_t fn_id = compute_hash(definition.c_str());
        tracer_state(context).function_ids[definition] = fn_id;
        return fn_id;
    }
}

bool register_inserted_function(dyntrace_context_t *context, fn_id_t id) {
    auto &already_inserted_functions =
        tracer_state(context).already_inserted_functions;
    auto result = already_inserted_functions.insert(id);
    return result.second;
}

bool negative_promise_already_inserted(dyntrace_context_t *context,
                                       prom_id_t id) {
    auto &already_inserted =
        tracer_state(context).already_inserted_negative_promises;
    return already_inserted.count(id) > 0;
}

bool function_already_inserted(dyntrace_context_t *context, fn_id_t id) {
    auto &already_inserted_functions =
        tracer_state(context).already_inserted_functions;
    return already_inserted_functions.count(id) > 0;
}

fn_addr_t get_function_addr(SEXP func) { return get_sexp_address(func); }

call_id_t make_funcall_id(dyntrace_context_t *context, SEXP function) {
    if (function == R_NilValue)
        return RID_INVALID;

    return ++tracer_state(context).call_id_counter;
}

// FIXME use general parent function by type instead.
prom_id_t get_parent_promise(dyntrace_context_t *context) {
    for (std::vector<stack_event_t>::reverse_iterator iterator =
             tracer_state(context).full_stack.rbegin();
         iterator != tracer_state(context).full_stack.rend(); ++iterator) {
        if (iterator->type == stack_type::PROMISE)
            return iterator->promise_id;
    }
    return 0; // FIXME should return a special value or something
}

size_t get_no_of_ancestor_promises_on_stack(dyntrace_context_t *context) {
    size_t result = 0;
    vector<stack_event_t> & stack = tracer_state(context).full_stack;
    for (auto it = stack.begin(); it != stack.end(); ++it) {
        if (it->type == stack_type::PROMISE)
            result++;
    }
    return result;
}

arg_id_t get_argument_id(dyntrace_context_t *context, call_id_t call_id,
                         const string &argument) {
    arg_id_t argument_id = ++tracer_state(context).argument_id_sequence;
    return argument_id;
}

inline void get_one_argument(closure_info_t &info,
                             dyntrace_context_t *context, call_id_t call_id,
                             const SEXP argument_expression,
                             const SEXP expression,
                             const SEXP environment, bool dot_argument,
                             int position) {

    arg_t argument;

    SEXPTYPE expression_type = TYPEOF(expression);
    SEXPTYPE argument_type = TYPEOF(argument_expression);

    if (argument_expression != R_NilValue) {
        argument.name = string(get_name(argument_expression));
    }

    if (expression_type == PROMSXP) {
        argument.promise_id = get_promise_id(context, expression);
        argument.default_argument = (PRENV(expression) == environment) ?
                                    ternary::TRUE :
                                    ternary::FALSE;
    } else {
        argument.promise_id = 0;
        argument.default_argument = ternary::OMEGA;
    }

    argument.id = get_argument_id(context, call_id, argument.name);
    argument.argument_type = static_cast<sexp_type>(argument_type);
    argument.expression_type = static_cast<sexp_type>(expression_type);
    argument.dot_argument = dot_argument;
    argument.position = position;

    info.arguments.push_back(argument);
}

void get_arguments(closure_info_t &info, dyntrace_context_t *context,
                   const call_id_t call_id, const SEXP op,
                   const SEXP environment) {

    int formal_position = 0;
    SEXP formals = FORMALS(op);
    for (; formals != R_NilValue; formals = CDR(formals), formal_position++) {

        // Retrieve the argument name.
        SEXP argument_expression = TAG(formals);

        SEXP expression;
        // We want the promise associated with the symbol.
        // Generally, the argument_expression should be the promise.
        // But if JIT is enabled, its possible for the argument_expression
        // to be unpromised. In this case, FIXME?.
        if (TYPEOF(argument_expression) == SYMSXP) {
            lookup_result r = find_binding_in_environment(argument_expression,
                                                          environment);
            if (r.status == lookup_status::SUCCESS) {
                expression = r.value;
            } else {
                // So... since this is a function, then I assume we shouldn't
                // get any arguments that are active bindings or anything like
                // that. If we do, then we should fail here and re-think our
                // life choices.
                string msg = lookup_status_to_string(r.status);
                dyntrace_log_error("%s", msg.c_str());
            }
        }
        // If the argument already has an associated promise, then use that.
        // In case we see something else like NILSXP, the various helper functions
        // below should be geared to deal with it.
        else {
            expression = argument_expression;
        }

        // Encountered a triple-dot argument, break it up further.
        if (TYPEOF(expression) == DOTSXP) {
            for (SEXP dots = expression; dots != R_NilValue;
                 dots = CDR(dots)) {
                get_one_argument(info, context, call_id, TAG(dots),
                                 environment, CAR(dots), true,
                                 formal_position);
            }
            return;
        }

        // The general case: single argument.
        get_one_argument(info, context, call_id, argument_expression,
                         expression, environment, false, formal_position);
    }
}

string recursive_type_to_string(recursion_type type) {
    switch (type) {
        case recursion_type::MUTUALLY_RECURSIVE:
            return "mutually_recursive";
        case recursion_type::RECURSIVE:
            return "recursive";
        case recursion_type::NOT_RECURSIVE:
            return "not_recursive";
        case recursion_type::UNKNOWN:
            return "unknown";
        default:
            return "???????";
    }
}
