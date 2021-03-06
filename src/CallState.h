#ifndef PROMISE_DYNTRACER_CALL_STATE_H
#define PROMISE_DYNTRACER_CALL_STATE_H

#include "ParameterUse.h"
#include "State.h"
#include "utilities.h"

class CallState {
  public:
    explicit CallState(call_id_t call_id, fn_id_t fn_id,
                       std::size_t formal_parameter_count)
        : call_id_{call_id}, fn_id_{fn_id},
          formal_parameter_count_{formal_parameter_count},
          parameter_uses_{formal_parameter_count}, order_{} {

        /* INFO - Reserve size to 15 bytes to prevent repeated string
         * allocations when forced arguments are added. This increases
         * the memory requirement but should speed up the program. */
        order_.reserve(15);
    }

    const call_id_t get_call_id() const { return call_id_; }

    const fn_id_t &get_function_id() const { return fn_id_; }

    const int get_formal_parameter_count() const {
        return formal_parameter_count_;
    }

    void set_type(std::size_t position, sexptype_t type) {
        parameter_uses_[position].set_type(type);
    }

    void force(std::size_t position) {

        bool previous_forced_state = parameter_uses_[position].get_force();
        parameter_uses_[position].force();

        /* iff the argument is forced and has not been previously forced
           do we add it to the order. This ensures that there is a single
           order entry for all elements of ... */
        if (!previous_forced_state)
            order_.append("|").append(std::to_string(position));
    }

    void lookup(std::size_t position) { parameter_uses_[position].lookup(); }

    void metaprogram(std::size_t position) {
        parameter_uses_[position].metaprogram();
    }

    void set_parameter_mode(std::size_t position, parameter_mode_t mode) {
        parameter_uses_[position].set_parameter_mode(mode);
    }

    const std::vector<ParameterUse> &get_parameter_uses() const {
        return parameter_uses_;
    }

    const std::string &get_order() const { return order_; }

  private:
    fn_id_t fn_id_;
    call_id_t call_id_;
    std::size_t formal_parameter_count_;
    std::vector<ParameterUse> parameter_uses_;
    std::string order_;
};

inline std::ostream &operator<<(std::ostream &os, const CallState &call_state) {
    os << "CallState(" << call_state.get_function_id() << ","
       << call_state.get_call_id() << ","
       << call_state.get_formal_parameter_count() << ")";
    return os;
}

#endif /* PROMISE_DYTRACER_CALL_STATE_H */
