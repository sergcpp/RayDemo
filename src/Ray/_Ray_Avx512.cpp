
// MSVC allows setting /arch option only for separate translation units, so put it here.
// (simd_vec is vectorized manually with intrinsics, but compiling whole core functions with /arch:AVX2 allows to avoid SSE/AVX switch overhead)

#if defined(_M_X86) || defined(_M_X64) || defined(__i386__) || defined(__x86_64__)
// TODO: ...
#endif