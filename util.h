/* See LICENSE file for copyright and license details. */

#define MAX(A, B)               ((A) > (B) ? (A) : (B))
#define MIN(A, B)               ((A) < (B) ? (A) : (B))
#define BETWEEN(X, A, B)        ((A) <= (X) && (X) <= (B))

void die(const char *fmt, ...);
void *ecalloc(size_t nmemb, size_t size);
char * runCommand(const char *cmd);
void timespecAddMs(struct timespec *ts, int32_t ms);
void timespecSetMs(struct timespec *ts, int32_t ms);
int32_t timespecToMs(struct timespec *ts);
void timespecDiff(struct timespec *res, struct timespec *a, struct timespec *b);
int timespecGte(struct timespec *a, struct timespec *b);
const char* execRead(const char *file, const char *params[]);
