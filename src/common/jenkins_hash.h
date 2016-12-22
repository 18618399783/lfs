#ifndef JENKINS_HASH_H
#define    JENKINS_HASH_H

#include <stdint.h>
#include <stdio.h>
#ifdef    __cplusplus
extern "C" {
#endif

uint32_t jenkins_hash(const void *key, size_t length);

#ifdef    __cplusplus
}
#endif

#endif    /* JENKINS_HASH_H */

