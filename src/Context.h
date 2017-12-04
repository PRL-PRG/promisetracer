#ifndef __CONTEXT_H__
#define __CONTEXT_H__

#include "SqlSerializer.h"
#include "State.h"
#include <string>

#define tracer_state(context)                                                  \
    ((static_cast<Context *>(context->dyntracer_context))->get_state())
#define tracer_serializer(context)                                             \
    ((static_cast<Context *>(context->dyntracer_context))->get_serializer())

class Context {
  public:
    Context(std::string database, std::string schema, bool truncate,
            bool verbose);
    tracer_state_t &get_state();
    SqlSerializer &get_serializer();

  private:
    tracer_state_t *state;
    SqlSerializer *serializer;
};

#endif /* __CONTEXT_H__ */
