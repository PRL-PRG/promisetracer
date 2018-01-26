#ifndef __UTILITIES_H__
#define __UTILITIES_H__

#include "stdlibs.h"
#include <openssl/evp.h>

extern size_t SQLITE3_ERROR_MESSAGE_BUFFER_SIZE;
extern size_t SQLITE3_EXPANDED_SQL_BUFFER_SIZE;

int get_file_size(std::ifstream &file);

std::string readfile(std::ifstream &file);

bool file_exists(const std::string &filepath);

char *copy_string(char *destination, const char *source, size_t buffer_size);

std::string repeat(const std::string &pattern, int count);

std::stringstream &overwrite_suffix(std::stringstream &stream, std::string suffix);

std::string pad(const std::string &field, size_t field_width);

bool sexp_to_bool(SEXP value);

int sexp_to_int(SEXP value);

std::string sexp_to_string(SEXP value);

template <typename T>
std::underlying_type_t<T> to_underlying_type(const T &enum_val) {
    return static_cast<std::underlying_type_t<T>>(enum_val);
}

std::string compute_hash(const char *data);
const char *get_ns_name(SEXP op);
const char *get_name(SEXP call);
char *get_location(SEXP op);
char *get_callsite(int);
const char *get_call(SEXP call);
int is_byte_compiled(SEXP op);
// char *to_string(SEXP var);
std::string get_expression(SEXP e);
const char *remove_null(const char *value);
std::string clock_ticks_to_string(clock_t ticks);
#endif /* __UTILITIES_H__ */
