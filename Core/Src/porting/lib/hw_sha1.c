#include <assert.h>
#include <stdio.h>
#include <stdint.h>

#include "hw_sha1.h"
#include "main.h"
#include "stm32h7xx_hal.h"

#define BUFFER_SIZE 4*1024

int8_t calculate_sha1_file(const char *file_path, uint8_t *output_hash) {
    return calculate_sha1_file_limit(file_path, NO_LIMIT, output_hash);
}

int8_t calculate_sha1_file_limit(const char *file_path, ssize_t max_bytes, uint8_t *output_hash) {
    uint8_t buffer[BUFFER_SIZE];
    size_t read_bytes;
    size_t remaining = (max_bytes == NO_LIMIT) ? (size_t)-1 : (size_t)max_bytes;

    FILE *file = fopen(file_path, "rb");
    if (!file) {
        return -1;
    }

    __HAL_RCC_HASH_CLK_ENABLE();
    HASH_HandleTypeDef hhash;
    HAL_HASH_DeInit(&hhash);
    hhash.Init.DataType = HASH_DATATYPE_8B;
    if (HAL_HASH_Init(&hhash) != HAL_OK) {
        __HAL_RCC_HASH_CLK_DISABLE();
        fclose(file);
        return 0;
    }

    while (remaining > 0 && (read_bytes = fread(buffer, 1,
            remaining < BUFFER_SIZE ? remaining : BUFFER_SIZE, file)) > 0) {
        if (HAL_HASH_SHA1_Accumulate(&hhash, buffer, read_bytes) != HAL_OK) {
            HAL_HASH_DeInit(&hhash);
            __HAL_RCC_HASH_CLK_DISABLE();
            fclose(file);
            return 0;
        }
        if (remaining != NO_LIMIT)
            remaining -= read_bytes;
    }

    if (HAL_HASH_SHA1_Start(&hhash, buffer, 0, output_hash, HAL_MAX_DELAY) != HAL_OK) {
        HAL_HASH_DeInit(&hhash);
        __HAL_RCC_HASH_CLK_DISABLE();
        fclose(file);
        return 0;
    }

    HAL_HASH_DeInit(&hhash);
    __HAL_RCC_HASH_CLK_DISABLE();
    fclose(file);
    return 1;
}

int8_t calculate_sha1_hw(const uint8_t *data, size_t len, uint8_t *output) {
    __HAL_RCC_HASH_CLK_ENABLE();

    HASH_HandleTypeDef hhash;

    hhash.Init.DataType = HASH_DATATYPE_8B;
    if (HAL_HASH_Init(&hhash) != HAL_OK) {
        __HAL_RCC_HASH_CLK_DISABLE();
        return 0;
    }

    if (HAL_HASH_SHA1_Start(&hhash, (uint8_t*)data, len, output, HAL_MAX_DELAY) != HAL_OK) {
        __HAL_RCC_HASH_CLK_DISABLE();
        HAL_HASH_DeInit(&hhash);
        return 0;
    }

    HAL_HASH_DeInit(&hhash);
    __HAL_RCC_HASH_CLK_DISABLE();
    return 1;
}
