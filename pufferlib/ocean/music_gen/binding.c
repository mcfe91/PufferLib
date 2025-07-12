#include "music_gen.h"

#define Env MusicGen 
#include "../env_binding.h"

static int my_init(Env* env, PyObject* args, PyObject* kwargs) {
    // Audio engine doesn't need additional parameters
    // Initialize any environment-specific parameters here if needed
    env->env_initialized = 0;  // Will be set to 1 in c_reset
    return 0;
}

static int my_log(PyObject* dict, Log* log) {
    assign_to_dict(dict, "score", log->score);
    assign_to_dict(dict, "n", log->n);
    return 0;
}