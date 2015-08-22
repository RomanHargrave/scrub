/**
 * Attempt to collapse a directory tree while avoiding certain files.
 * Copyright 2015 Roman Hargrave <roman@hargrave.info> under the GNU GPL v3
 */

#define _GNU_SOURCE

// Function attributes 

#if defined(__GNUC__)
#   define pure 
#   define hot  __attribute__((hot))
#   define cold __attribute__((cold))
#else
#   define pure 
#   define hot
#   define cold 
#endif 

#define unless(x)   if(!(x))
#define until(x)    while(!(x))

#define dispose(ptr) \
    {\
        if (ptr) { \
            free(ptr); \
        } \
        ptr = NULL; \
    }

/*
 * getopt_long()
 * struct option 
 */
#include <getopt.h>

/*
 * struct stat 
 * stat()
 */
#include <sys/stat.h>

/*
 * readdir()
 * closedir()
 */
#include <dirent.h>

/*
 * basename()
 */
#include <libgen.h>

/*
 * CHAR_MAX
 */
#include <limits.h>

/*
 * unlink()
 * rmdir()
 */
#include <unistd.h>

#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <errno.h>

typedef unsigned int    u32;
typedef struct option   Option;

typedef DIR             Directory;
typedef struct dirent   DirEntry;

/**
 * "Success value" for errno as defined in errno(3)
 */
static const u32        ENONE = 0;

/*
 * SECTION: Program configuration
 * This section handles making sense of the commandline passed to the program
 */

/**
 * Option flag ordinals with values corresponding to the numerical value of their short form
 *
 * Long-form-only flags should have a value of CHAR_MAX + n and come _after_ all the short-form flags are defined
 */
typedef enum {
    HELP                = 'h',
    CLOBBER_EXT         = 'c',
    CLOBBER_NAME        = 'C',
    PRESERVE_HIDDEN     = 'H',

    PRESERVE_SPECIAL    = CHAR_MAX + 1,
    RUN_SIMULATE,
    VERBOSE_LOGGING
} Flag;

/**
 * Long-form options for getopt.h
 */
static Option Options[] = {
    // Short version: `h`
    { "help",               no_argument,        0,  HELP            },
    // Short version: `c`
    { "clobber-extension",  required_argument,  0,  CLOBBER_EXT     },
    // Short version: `C`
    { "clobber-name",       required_argument,  0,  CLOBBER_NAME    },
    // Short version: `H`
    { "preserve-hidden",    no_argument,        0,  PRESERVE_HIDDEN },
    // Do not delete "special" files 
    { "preserve-special",   no_argument,        0,  PRESERVE_SPECIAL},
    // Print actions only 
    { "simulate",           no_argument,        0,  RUN_SIMULATE    },
    { "verbose",            no_argument,        0,  VERBOSE_LOGGING },
    { NULL,                 0,                  0,  0               }
};

/**
 * Structure that stores the configuration passed on the commandline
 */
typedef struct {
    bool verbose;

    /*
     * Whether to simulate operations or not
     */
    bool simulate;

    /*
     * Whether to avoid hidden folders
     */
    bool preserveHidden;

    /*
     * Wheter to preserve special files
     */
    bool preserveSpecial;

    /*
     * Extensions to clobber
     */
    char**  clobberExtensions;
    size_t  clobberExtensionsLen;

    /*
     * Names to clobber
     */
    char**  clobberNames;
    size_t  clobberNamesLen;
} Configuration;

/**
 * Initialize a new configuration structure
 * The stucture will be allocated on the heap
 */
static Configuration* 
Configuration_new() {
    Configuration* self = (Configuration*) malloc(sizeof(Configuration));
    
    self->verbose              = false;
    self->simulate             = false;
    self->preserveHidden       = false;
    self->preserveSpecial      = false;
    self->clobberExtensions    = (char**) malloc(sizeof(char**));
    self->clobberExtensionsLen = 0;
    self->clobberNames         = (char**) malloc(sizeof(char**));
    self->clobberNamesLen      = 0;

    return self;
}

/**
 * Add an extension to the list of things to clobber (delete)
 *
 * @param config    configuration
 * @param extension extension
 */
static void 
Configuration_clobberExtension(Configuration* config, char* extension) {
    config->clobberExtensions = realloc(config->clobberExtensions, config->clobberExtensionsLen + 1);
    char** nextPtr = config->clobberExtensions + config->clobberExtensionsLen;
    *nextPtr = malloc(strlen(extension) * sizeof(char));
    strcpy(*nextPtr, extension);
    ++config->clobberExtensionsLen;
}

/**
 * Returns true if the provided extension should be clobbered 
 *
 * @param config    configuration
 * @param extension extension
 */
static hot pure bool
Configuration_shouldClobberExtension(Configuration* config, char* extension) {
    // Linear search, though this should not be too impactful as I highly doubt you would ever need to remove
    // a significantly large number of unique extension names 
 
    size_t index = 0;

    while (index < config->clobberExtensionsLen) {
        if (strcmp(extension, *(config->clobberExtensions + index)) == 0) {
            return true;
        }

        ++index;
    }

    return false;
}

/**
 * Add a file name to the list of things to clobber (delete)
 *
 * @param config    configuration
 * @param name      file name
 */
static void
Configuration_clobberName(Configuration* config, char* name) {
    config->clobberNames = realloc(config->clobberNames, config->clobberNamesLen + 1);
    char** nextPtr = config->clobberNames + config->clobberNamesLen;
    *nextPtr = malloc(strlen(name) * sizeof(char));
    strcpy(*nextPtr, name);
    ++config->clobberNamesLen;
}

/**
 * Returns true if the provided file name should be clobbered 
 *
 * @param config    configuration
 * @param name      file name
 */
static hot pure bool
Configuration_shouldClobberName(Configuration* config, char* name) {
    size_t index = 0;

    while (index < config->clobberNamesLen) {
        if (strcmp(name, *(config->clobberNames + index)) == 0) {
            return true;
        }

        ++index;
    }

    return false;
}

static pure void 
Runtime_putError(char* format, ...) {
    va_list args;
    va_start(args, format);
    vfprintf(stderr, format, args);
    va_end(args);
}

static pure void 
Runtime_verbose(Configuration* config, char* format, ...) {
    if (config->verbose) {
        va_list args;
        va_start(args, format);
        vfprintf(stderr, format, args);
        va_end(args);
    }
}

/**
 * Print help
 *
 * @param executableName    argv+0 (image name)
 */
static pure cold void 
Runtime_printHelp(char* executableName) {
    Runtime_putError(
        "%s - try to clean a directory tree\n"
        "\n"
        "-h     --help\n"
        "   Print help (this message)\n"
        "\n"
        "-cext  --clobber-extension=ext\n"
        "   Add `ext` to the list of extensions to be deleted\n"
        "\n"
        "-Cname --clobber-name=name\n"
        "   Add `name` to the list of file names to be deleted\n"
        "\n"
        "-H     --preserve-hidden\n"
        "   Rather than treating hidden directories as normal directories, halt when one is discovered\n"
        "\n"
        "--preserve-special\n"
        "   Do not delete special files (such as sockets, block devices, and pipes)\n"
        "\n"
        "--simulate\n"
        "   Rather than calling unlink() and the like, output a message\n"
        "\n"
        "--verbose\n"
        "   Verbose logging output\n"
        , executableName
    );
}

/*
 * SECTION: Implementation
 * Routines relating to deleting things
 */

static pure u32 
File_unlink(Configuration* config, char* path) {
    if (config->simulate) {
        Runtime_putError("unlink(%s)\n", path);
        return 0;
    } else {
        // pick RMDIR or UNLINK
        struct stat statBuffer;
        stat(path, &statBuffer);

        if (S_ISDIR(statBuffer.st_mode)) {
            return rmdir(path);
        } else {
            return unlink(path);
        }
    }
}

/**
 * Returns true if a file should be clobbered according to the configuration
 *
 * @param config    configuration
 * @param basename  file name
 */
static hot pure bool
File_shouldClobber(Configuration* config, char* basename) {
    if (Configuration_shouldClobberName(config, basename)) {
        return true;
    } else {
        // Get the file's extension, if present.
        char* extensionStart = strrchr(basename, '.');

        if (extensionStart) {
            return Configuration_shouldClobberExtension(config, ++extensionStart);
        } else {
            return false;
        }
    }
}

/**
 * Returns true if a file is hidden (name begins with `.`)
 *
 * @param basename  file name 
 */
static pure bool 
File_isHidden(char* basename) {
    return (basename - strchrnul(basename, '.')) == 0;
}

/**
 * Returns true if a directory is empty
 *
 * @param dir   directory (must be opened/closed by the caller)
 */
static pure bool 
Directory_isEmpty(char* path) {
    Directory* dir = opendir(path);

    if (dir) {
        u32         index = 0;
        DirEntry*   entry = NULL;

        while (entry = readdir(dir)) {
            // Account for `.` and `..`
            if (++index > 2) {
                break;
            }
        }

        closedir(dir);

        return index <= 2;
    } else {
        Runtime_putError("Directory_isEmpty(%s): could not open directory: ERRNO %u\n", path, errno);
        return false;
    }
}

static hot pure int // errno 
File_process(Configuration* config, char* path) {
    char* pathCopy = malloc(strlen(path) * sizeof(char));
    strcpy(pathCopy, path);
    char* fileName = basename(pathCopy);
    
    bool shouldClobber = File_shouldClobber(config, fileName);
    
    dispose(pathCopy);

    if (shouldClobber) {
        if (File_unlink(config, path) == -1) {
            Runtime_putError("Could not unlink %s: ERRNO %u\n", path, errno);
            return errno;
        } else {
            return ENONE;
        }
    } else {
        return ENONE;
    }
}

static hot pure int // errno 
Directory_process(Configuration* config, char* path) {
    Directory*  dir     = opendir(path);
    u32         result  = ENONE;
    
    if (dir) {
        DirEntry* currentEntry = NULL;

        while (currentEntry = readdir(dir)) {
            if ((strcmp(currentEntry->d_name, ".") == 0) || (strcmp(currentEntry->d_name, "..") == 0)) {
                continue;
            }

            char* currentEntryPath;
            {
                asprintf(&currentEntryPath, "%s/%s", path, currentEntry->d_name);
            }
            
            switch (currentEntry->d_type) {
                case DT_DIR: 
                    unless (config->preserveHidden && File_isHidden(currentEntry->d_name)) {
                        u32 completionState = Directory_process(config, currentEntryPath);

                        if (completionState == ENONE) {
                            if (Directory_isEmpty(currentEntryPath)) {
                                if (File_unlink(config, currentEntryPath) == -1) {
                                    Runtime_putError("Could not unlink directory %s: ERRNO %u\n", currentEntryPath, errno);
                                }
                            } else {
                                Runtime_verbose(config, "Directory %s is not empty. Not unlinking.\n", currentEntryPath);
                            }
                        } else {
                            Runtime_putError("Could not process directory %s: ERRNO %u\n", currentEntryPath, completionState);
                        }
                    }
                    break;

                /*
                 * Allow custom handling "for special" files 
                 * Usually, a lot of these are synthetic and can be removed without concern
                 */
                case DT_BLK:
                case DT_CHR:
                case DT_FIFO:
                case DT_LNK: 
                case DT_SOCK:
                    if (config->preserveSpecial) {
                        break;
                    }

                /*
                 * Several filesystems will return DT_UNKOWN as they do not implement d_type support
                 * as such, it should be treated as DT_REG.
                 */
                case DT_UNKNOWN:
                case DT_REG: {
                        u32 returnStatus = File_process(config, currentEntryPath);

                        unless (returnStatus == ENONE) {
                            Runtime_verbose(config, "File_process(%s) failed: ERRNO %u\n", currentEntryPath, returnStatus);
                        }
                    }
                    break;
            }

            dispose(currentEntryPath);
        }

        closedir(dir);
    } else {
        result = errno;
    }
    
    return result;
}

/**
 * Entry point
 */
int 
main (int argc, char** argv) {
    static char* const ShortOptions = "h c: C: H";

    char* const imageName = *argv;
    Configuration* runtimeConfig = Configuration_new();

    {
        int optionOrd;

        until ((optionOrd = getopt_long(argc, argv, ShortOptions, Options, NULL)) == -1) {
            switch (optionOrd) {
                case HELP:
                    Runtime_printHelp(imageName);
                    return 0;
                    break;
                case CLOBBER_EXT:
                    if (optarg) {
                        Configuration_clobberExtension(runtimeConfig, optarg);
                    } else {
                        Runtime_putError("A parameter must be passed to --clobber-extension\n");
                        return EINVAL;
                    }
                    break;
                case CLOBBER_NAME:
                    if (optarg) {
                        Configuration_clobberName(runtimeConfig, optarg);
                    } else {
                        Runtime_putError("A parameter must be passed to --clobber-name\n");
                        return EINVAL;
                    }
                    break;
                case PRESERVE_HIDDEN:
                    runtimeConfig->preserveHidden = true;
                    break;
                case PRESERVE_SPECIAL:
                    runtimeConfig->preserveSpecial = true;
                    break;
                case RUN_SIMULATE:
                    runtimeConfig->simulate = true;
                    break;
                case VERBOSE_LOGGING:
                    runtimeConfig->verbose = true;
                    break;
                default:
                    Runtime_putError("\n"); // Put space between getopt.h's message and our blurb 
                    Runtime_printHelp(imageName);
                    return EINVAL;
                    break;
            }
        }
    }

    {
        char** files    = argv + optind;
        size_t n_files  = argc - optind;
        size_t index    = 0;

        if (n_files == 0) {
            Runtime_printHelp(imageName);
            return ENONE;
        }

        bool   dirty    = false;

        while (index < n_files) {
            char* fileName = *(files + index);

            // Check if it's a directory or otherwise.
            // If it's a file, remove it according to clobber etc...
            struct stat statBuffer;
            stat(fileName, &statBuffer);

            if (S_ISDIR(statBuffer.st_mode)) {
                Runtime_verbose(runtimeConfig, "Processing directory %s\n", fileName);
                Directory_process(runtimeConfig, fileName);

                unless (Directory_isEmpty(fileName)) {
                    dirty = true;
                }
            } else {
                Runtime_verbose(runtimeConfig, "Processing node %s\n", fileName);
                File_process(runtimeConfig, fileName);
            }

            ++index;
        }

        if (dirty) {
            return ENOTEMPTY;
        } else {
            return ENONE;
        }
    }
}
