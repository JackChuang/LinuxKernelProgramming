/**
 * Header file for the linrary: contains just the interface presented to
 * user-space programs
 */

#ifndef KVLIB_H
#define KVLIB_H

/* writing a key/value couple */
int kvlib_set(const char *key, const char *value);

/* getting a value from a key */
int kvlib_get(const char *key, char *value);

/* fomat */
int kvlib_format();

#endif /* KVLIB_H */
