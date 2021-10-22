/* See LICENSE file for copyright and license details. */

#define MAX(A, B)               ((A) > (B) ? (A) : (B))
#define MIN(A, B)               ((A) < (B) ? (A) : (B))
#define BETWEEN(X, A, B)        ((A) <= (X) && (X) <= (B))

#define STRINGIZE_NX(A) #A
#define STRINGIZE(A) STRINGIZE_NX(A)


void die(const char *fmt, ...);
void *ecalloc(size_t nmemb, size_t size);
char * runCommand(const char *cmd);
void timespecAddMs(struct timespec *ts, int32_t ms);
void timespec_set_ms(struct timespec *ts, int32_t ms);
int32_t timespec_to_ms(struct timespec *ts);
void timespec_diff(struct timespec *res, struct timespec *a, struct timespec *b);
int timespecGte(struct timespec *a, struct timespec *b);
const char* execRead(const char *file, const char *params[]);
ssize_t strlcpy(char *dst, const char *src, ssize_t size);
