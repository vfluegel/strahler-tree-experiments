#ifndef CLI_VERSION_H
#define CLI_VERSION_H 1

#ifdef __cplusplus
extern "C" {
#endif

#define CLI_VERSION_NOT_REQUESTED (-1)

// Return CLI_VERSION_NOT_REQUESTED unless the sole argument is "--version".
// Otherwise print "PROGRAM VERSION" and return an EXIT_* status.
[[nodiscard]] int cli_handle_version_argument(int argc,
                                              char const *program_path,
                                              char const *argument);

#ifdef __cplusplus
}
#endif

#endif
