#ifndef MULXP_HASH_HPP_INCLUDED
#define MULXP_HASH_HPP_INCLUDED

// Copyright 2020-2022 Peter Dimov.
// Distributed under the Boost Software License, Version 1.0.
// https://www.boost.org/LICENSE_1_0.txt

#include <cstdint>
#include <cstddef>
#include <cstring>

#if defined(_MSC_VER) && defined(_M_X64) && !defined(__clang__)

#include <intrin.h>

__forceinline std::uint64_t mulx( std::uint64_t x, std::uint64_t y )
{
    std::uint64_t r2;
    std::uint64_t r = _umul128( x, y, &r2 );
    return r ^ r2;
}

#elif defined(_MSC_VER) && defined(_M_ARM64) && !defined(__clang__)

#include <intrin.h>

__forceinline std::uint64_t mulx( std::uint64_t x, std::uint64_t y )
{
    std::uint64_t r = x * y;
    std::uint64_t r2 = __umulh( x, y );
    return r ^ r2;
}

#elif defined(__SIZEOF_INT128__)

inline std::uint64_t mulx( std::uint64_t x, std::uint64_t y )
{
    __uint128_t r = (__uint128_t)x * y;
    return (std::uint64_t)r ^ (std::uint64_t)( r >> 64 );
}

#else

inline std::uint64_t mulx( std::uint64_t x, std::uint64_t y )
{
    std::uint64_t x1 = (std::uint32_t)x;
    std::uint64_t x2 = x >> 32;

    std::uint64_t y1 = (std::uint32_t)y;
    std::uint64_t y2 = y >> 32;

    std::uint64_t r3 = x2 * y2;

    std::uint64_t r2a = x1 * y2;

    r3 += r2a >> 32;

    std::uint64_t r2b = x2 * y1;

    r3 += r2b >> 32;

    std::uint64_t r1 = x1 * y1;

    std::uint64_t r2 = (r1 >> 32) + (std::uint32_t)r2a + (std::uint32_t)r2b;

    r1 = (r2 << 32) + (std::uint32_t)r1;
    r3 += r2 >> 32;

    return r1 ^ r3;
}

#endif

inline std::uint64_t read64le( unsigned char const * p )
{
    std::uint64_t r;
    std::memcpy( &r, p, 8 );
    return r;
}

inline std::uint32_t read32le( unsigned char const * p )
{
    std::uint32_t r;
    std::memcpy( &r, p, 4 );
    return r;
}

inline std::uint64_t mulxp0_hash( unsigned char const * p, std::size_t n, std::uint64_t seed )
{
    std::uint64_t const q = 0x9e3779b97f4a7c15ULL;
    std::uint64_t const k = q * q;

    std::uint64_t const n2 = n;

    std::uint64_t h = mulx( seed + q, k );

    while( n >= 8 )
    {
        std::uint64_t v1 = read64le( p );

        h ^= mulx( h + 1 + v1, k );

        p += 8;
        n -= 8;
    }

    {
        std::uint64_t v1 = 0;

        if( n >= 4 )
        {
            v1 = (std::uint64_t)read32le( p + n - 4 ) << ( n - 4 ) * 8 | read32le( p );
        }
        else if( n >= 1 )
        {
            std::size_t const x1 = ( n - 1 ) & 2; // 1: 0, 2: 0, 3: 2
            std::size_t const x2 = n >> 1;        // 1: 0, 2: 1, 3: 1

            v1 = (std::uint64_t)p[ x1 ] << x1 * 8 | (std::uint64_t)p[ x2 ] << x2 * 8 | (std::uint64_t)p[ 0 ];
        }

        h ^= mulx( h + 1 + v1, k );
    }

    return mulx( h + 1 + n2, k );
}

inline std::uint64_t mulxp1_hash( unsigned char const * p, std::size_t n, std::uint64_t seed )
{
    std::uint64_t const q = 0x9e3779b97f4a7c15ULL;
    std::uint64_t const k = q * q;

    std::uint64_t w = mulx( seed + q, k );
    std::uint64_t h = w ^ n;

    while( n >= 8 )
    {
        std::uint64_t v1 = read64le( p );

        w += q;
        h ^= mulx( v1 + w, k );

        p += 8;
        n -= 8;
    }

    {
        std::uint64_t v1 = 0;

        if( n >= 4 )
        {
            v1 = (std::uint64_t)read32le( p + n - 4 ) << ( n - 4 ) * 8 | read32le( p );
        }
        else if( n >= 1 )
        {
            std::size_t const x1 = ( n - 1 ) & 2; // 1: 0, 2: 0, 3: 2
            std::size_t const x2 = n >> 1;        // 1: 0, 2: 1, 3: 1

            v1 = (std::uint64_t)p[ x1 ] << x1 * 8 | (std::uint64_t)p[ x2 ] << x2 * 8 | (std::uint64_t)p[ 0 ];
        }

        w += q;
        h ^= mulx( v1 + w, k );
    }

    return mulx( h + w, k );
}

inline std::uint64_t mulxp2_hash( unsigned char const * p, std::size_t n, std::uint64_t seed )
{
    std::uint64_t const q = 0x9e3779b97f4a7c15ULL;
    std::uint64_t const k = q * q;

    std::uint64_t const n2 = n;

    std::uint64_t w = mulx( seed + q, k );
    std::uint64_t h = w;

    while( n >= 16 )
    {
        std::uint64_t v1 = read64le( p + 0 );
        std::uint64_t v2 = read64le( p + 8 );

        w += q;
        h ^= mulx( v1 + w, k );

        w += q;
        h ^= mulx( v2 + w, k );

        p += 16;
        n -= 16;
    }

    {
        std::uint64_t v1 = 0;
        std::uint64_t v2 = 0;

        if( n > 8 )
        {
            v1 = read64le( p );
            v2 = read64le( p + n - 8 ) >> ( 16 - n ) * 8;
        }
        else if( n >= 4 )
        {
            v1 = (std::uint64_t)read32le( p + n - 4 ) << ( n - 4 ) * 8 | read32le( p );
        }
        else if( n >= 1 )
        {
            std::size_t const x1 = ( n - 1 ) & 2; // 1: 0, 2: 0, 3: 2
            std::size_t const x2 = n >> 1;        // 1: 0, 2: 1, 3: 1

            v1 = (std::uint64_t)p[ x1 ] << x1 * 8 | (std::uint64_t)p[ x2 ] << x2 * 8 | (std::uint64_t)p[ 0 ];
        }

        w += q;
        h ^= mulx( v1 + w, k );

        w += q;
        h ^= mulx( v2 + w, k );
    }

    return mulx( h + w, k + n2 );
}

inline std::uint64_t mulxp3_hash( unsigned char const * p, std::size_t n, std::uint64_t seed )
{
    std::uint64_t const q = 0x9e3779b97f4a7c15ULL;
    std::uint64_t const k = q * q;

    std::uint64_t w = mulx( seed + q, k );
    std::uint64_t h = w ^ n;

    while( n >= 16 )
    {
        std::uint64_t v1 = read64le( p + 0 );
        std::uint64_t v2 = read64le( p + 8 );

        w += q;
        h ^= mulx( v1 + w, v2 + w + k );

        p += 16;
        n -= 16;
    }

    {
        std::uint64_t v1 = 0;
        std::uint64_t v2 = 0;

        if( n > 8 )
        {
            v1 = read64le( p );
            v2 = read64le( p + n - 8 ) >> ( 16 - n ) * 8;
        }
        else if( n >= 4 )
        {
            v1 = (std::uint64_t)read32le( p + n - 4 ) << ( n - 4 ) * 8 | read32le( p );
        }
        else if( n >= 1 )
        {
            std::size_t const x1 = ( n - 1 ) & 2; // 1: 0, 2: 0, 3: 2
            std::size_t const x2 = n >> 1;        // 1: 0, 2: 1, 3: 1

            v1 = (std::uint64_t)p[ x1 ] << x1 * 8 | (std::uint64_t)p[ x2 ] << x2 * 8 | (std::uint64_t)p[ 0 ];
        }

        w += q;
        h ^= mulx( v1 + w, v2 + w + k );
    }

    return mulx( h, k );
}

// 32 bit

inline std::uint64_t mul32( std::uint32_t x, std::uint32_t y )
{
    return (std::uint64_t)x * y;
}

inline std::uint32_t mulxp1_hash32( unsigned char const * p, std::size_t n, std::uint32_t seed )
{
    std::uint32_t const q = 0x9e3779b9U;
    std::uint32_t const k = q * q;

    std::uint64_t h = mul32( seed + q, k );
    std::uint32_t w = (std::uint32_t)h;

    h ^= n;

    while( n >= 4 )
    {
        std::uint32_t v1 = read32le( p );

        w += q;
        h ^= mul32( v1 + w, k );

        p += 4;
        n -= 4;
    }

    {
        std::uint32_t v1 = 0;

        if( n >= 1 )
        {
            std::size_t const x1 = ( n - 1 ) & 2; // 1: 0, 2: 0, 3: 2
            std::size_t const x2 = n >> 1;        // 1: 0, 2: 1, 3: 1

            v1 = (std::uint32_t)p[ x1 ] << x1 * 8 | (std::uint32_t)p[ x2 ] << x2 * 8 | (std::uint32_t)p[ 0 ];
        }

        w += q;
        h ^= mul32( v1 + w, k );
    }

    w += q;
    h ^= mul32( (std::uint32_t)h + w, (std::uint32_t)(h >> 32) + w + k );

    return (std::uint32_t)h ^ (std::uint32_t)(h >> 32);
}

inline std::uint32_t mulxp3_hash32( unsigned char const * p, std::size_t n, std::uint32_t seed )
{
    std::uint32_t const q = 0x9e3779b9U;
    std::uint32_t const k = q * q;

    std::uint64_t h = mul32( seed + q, k );
    std::uint32_t w = (std::uint32_t)h;

    h ^= n;

    while( n >= 8 )
    {
        std::uint32_t v1 = read32le( p + 0 );
        std::uint32_t v2 = read32le( p + 4 );

        w += q;
        h ^= mul32( v1 + w, v2 + w + k );

        p += 8;
        n -= 8;
    }

    {
        std::uint32_t v1 = 0;
        std::uint32_t v2 = 0;

        if( n >= 4 )
        {
            v1 = read32le( p );
            v2 = ((std::uint64_t)read32le( p + n - 4 ) << ( n - 4 ) * 8) >> 32;
        }
        else if( n >= 1 )
        {
            std::size_t const x1 = ( n - 1 ) & 2; // 1: 0, 2: 0, 3: 2
            std::size_t const x2 = n >> 1;        // 1: 0, 2: 1, 3: 1

            v1 = (std::uint32_t)p[ x1 ] << x1 * 8 | (std::uint32_t)p[ x2 ] << x2 * 8 | (std::uint32_t)p[ 0 ];
        }

        w += q;
        h ^= mul32( v1 + w, v2 + w + k );
    }

    w += q;
    h ^= mul32( (std::uint32_t)h + w, (std::uint32_t)(h >> 32) + w + k );

    return (std::uint32_t)h ^ (std::uint32_t)(h >> 32);
}

#endif // #ifndef MULXP_HASH_HPP_INCLUDED
