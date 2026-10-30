#pragma once

#include <windows.h>
#include <stdint.h>

#define LLP_ABI_MAGIC              0x7B314C50u
#define LLP_ABI_VERSION            0x00020000u
#define LLP_MIN_EXCLUSIVE_ABI      0x00020000u
#define LLP_EXCLUSIVE_GENERATION   1u
#define LLP_CAP_EXCLUSIVE_HOST     UINT64_C(0x0000000000000001)
#define LLP_HUB_2_GENERATION       2u
#define LLP_CAP_HUB_2              UINT64_C(0x0000000000000002)
#define LLP_PRODUCT_ID             UINT64_C(0x9d4b1c72e6a30f58)
#define LLP_ENTRY_NAME  "rfe_1d3b7a91"
#define LLP_PROOF_NAME  "llv_6a20c4e7"

typedef uint64_t (__cdecl *LightLoaderProofProc)(uint64_t nonce);

typedef struct LightLoaderProtectedHostV2 {
    uint32_t size;
    uint32_t magic;
    uint32_t abi_version;
    uint32_t process_id;
    HMODULE loader_module;
    uint64_t nonce;
    uint64_t product_id;
    uint64_t capabilities;
    uint32_t compatibility_generation;
    uint32_t reserved;
    uint64_t seal;
    uint64_t proof;
} LightLoaderProtectedHostV2;

static __inline uint64_t LightLoaderMix64(uint64_t value)
{
    value ^= value >> 30;
    value *= UINT64_C(0xbf58476d1ce4e5b9);
    value ^= value >> 27;
    value *= UINT64_C(0x94d049bb133111eb);
    value ^= value >> 31;
    return value;
}

static __inline uint64_t LightLoaderHostSeal(const LightLoaderProtectedHostV2 *host)
{
    return LightLoaderMix64(
        host->nonce ^ UINT64_C(0xd6e8feb86659fd93) ^
        (uint64_t)(uintptr_t)host->loader_module ^
        ((uint64_t)host->process_id << 32) ^ host->magic ^ host->abi_version ^
        host->product_id ^ host->capabilities ^
        ((uint64_t)host->compatibility_generation << 32));
}

static __inline uint64_t LightLoaderProofChallenge(
    const LightLoaderProtectedHostV2 *host)
{
    return LightLoaderMix64(
        host->nonce ^ host->product_id ^ host->capabilities ^
        ((uint64_t)host->compatibility_generation << 32) ^
        host->abi_version);
}
