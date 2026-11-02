/**
 * @file tap.h
 * @brief Mini harness de teste para o host, com saída TAP.
 *
 * Cada caso vira uma linha "ok N - nome" / "not ok N - nome". Casos que
 * provam um critério de aceite começam o nome pelo código (ex.: "AC-005 ...");
 * test/spec/lib/host.mjs liga essas linhas ao node:test e ao onp-spec.
 */
#ifndef TAP_H
#define TAP_H

#include <stdio.h>

static int tap_count;
static int tap_failures;
static int tap_case_failed;

#define TAP_CHECK(cond, ...) do { \
    if (!(cond)) { \
        tap_case_failed = 1; \
        printf("#   falhou (%s:%d): ", __FILE__, __LINE__); \
        printf(__VA_ARGS__); \
        printf("\n"); \
    } \
} while (0)

static inline void tap_run(const char *name, void (*fn)(void))
{
    tap_case_failed = 0;
    fn();
    tap_count++;
    if (tap_case_failed) tap_failures++;
    printf("%sok %d - %s\n", tap_case_failed ? "not " : "", tap_count, name);
}

static inline int tap_done(void)
{
    printf("1..%d\n", tap_count);
    return tap_failures ? 1 : 0;
}

#endif /* TAP_H */
