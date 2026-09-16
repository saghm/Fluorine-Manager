/* Preserve Wine's UTF-8 Unix filename encoding when steamclient changes the
 * launch environment after Proton's locale repair has already run.
 *
 * This small Linux/glibc interposer intentionally uses only libc exports and
 * compiler-provided headers. That lets us build both ELF classes without a
 * second libc SDK; no runtime or compiler support library is linked in.
 */
#include <stddef.h>
#include <stdatomic.h>

extern void *dlsym(void *, const char *);
extern char *getenv(const char *);
extern int strcmp(const char *, const char *);
extern int strncmp(const char *, const char *, size_t);

typedef int (*setenv_fn)(const char *, const char *, int);
typedef int (*putenv_fn)(char *);
static _Atomic(setenv_fn) next_setenv;
static _Atomic(putenv_fn) next_putenv;

static setenv_fn real_setenv(void)
{
    setenv_fn fn = atomic_load(&next_setenv);
    if (!fn) {
        fn = (setenv_fn)dlsym((void *)-1L, "setenv"); /* RTLD_NEXT */
        atomic_store(&next_setenv, fn);
    }
    return fn;
}

static const char *replacement(const char *name, const char *value)
{
    if (!name || !value || strcmp(name, "LC_ALL") ||
        (strcmp(value, "C") && strcmp(value, "POSIX"))) return NULL;
    const char *locale = getenv("FLUORINE_WINE_UTF8");
    return locale && *locale ? locale : NULL;
}

int setenv(const char *name, const char *value, int overwrite)
{
    setenv_fn fn = real_setenv();
    if (!fn) return -1;
    const char *locale = replacement(name, value);
    return fn(name, locale ? locale : value, overwrite);
}

int putenv(char *assignment)
{
    if (!strncmp(assignment, "LC_ALL=", 7)) {
        const char *locale = replacement("LC_ALL", assignment + 7);
        if (locale) {
            setenv_fn fn = real_setenv();
            return fn ? fn("LC_ALL", locale, 1) : -1;
        }
    }
    putenv_fn fn = atomic_load(&next_putenv);
    if (!fn) {
        fn = (putenv_fn)dlsym((void *)-1L, "putenv");
        atomic_store(&next_putenv, fn);
    }
    return fn ? fn(assignment) : -1;
}
