/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 4 -*-
 * vim: set ts=8 sts=4 et sw=4 tw=99:
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef jsmath_h
#define jsmath_h

#include "jsapi.h"

namespace js {

typedef double (*UnaryFunType)(double);

class MathCache
{
    static const unsigned SizeLog2 = 12;
    static const unsigned Size = 1 << SizeLog2;
    struct Entry { double in; UnaryFunType f; double out; };
    Entry table[Size];

  public:
    MathCache();

    unsigned hash(double x) {
        union { double d; struct { uint32_t one, two; } s; } u = { x };
        uint32_t hash32 = u.s.one ^ u.s.two;
        uint16_t hash16 = uint16_t(hash32 ^ (hash32 >> 16));
        return (hash16 & (Size - 1)) ^ (hash16 >> (16 - SizeLog2));
    }

    /*
     * N.B. lookup uses double-equality. This is only safe if hash() maps +0
     * and -0 to different table entries, which is asserted in MathCache().
     */
    double lookup(UnaryFunType f, double x) {
        unsigned index = hash(x);
        Entry &e = table[index];
        if (e.in == x && e.f == f)
            return e.out;
        e.in = x;
        e.f = f;
        return (e.out = f(x));
    }

    size_t sizeOfIncludingThis(JSMallocSizeOfFun mallocSizeOf);
};

} /* namespace js */

/*
 * JS math functions.
 */

extern JSObject *
js_InitMathClass(JSContext *cx, js::HandleObject obj);

extern double
math_random_no_outparam(JSContext *cx);

extern JSBool
js_math_random(JSContext *cx, unsigned argc, js::Value *vp);

extern JSBool
js_math_abs(JSContext *cx, unsigned argc, js::Value *vp);

extern JSBool
js_math_ceil(JSContext *cx, unsigned argc, js::Value *vp);

extern JSBool
js_math_floor(JSContext *cx, unsigned argc, js::Value *vp);

extern JSBool
js_math_max(JSContext *cx, unsigned argc, js::Value *vp);

extern JSBool
js_math_min(JSContext *cx, unsigned argc, js::Value *vp);

extern JSBool
js_math_round(JSContext *cx, unsigned argc, js::Value *vp);

extern JSBool
js_math_sqrt(JSContext *cx, unsigned argc, js::Value *vp);

extern JSBool
js_math_pow(JSContext *cx, unsigned argc, js::Value *vp);

extern double
js_math_ceil_impl(double x);

extern double
js_math_floor_impl(double x);

namespace js {

extern JSBool
math_clz32(JSContext *cx, unsigned argc, js::Value *vp);

extern JSBool
math_fround(JSContext *cx, unsigned argc, js::Value *vp);

extern JSBool
math_imul(JSContext *cx, unsigned argc, js::Value *vp);

extern JSBool
math_log(JSContext *cx, unsigned argc, js::Value *vp);

extern double
math_log_impl(MathCache *cache, double x);

extern JSBool
math_sin(JSContext *cx, unsigned argc, js::Value *vp);

extern double
math_sin_impl(MathCache *cache, double x);

extern JSBool
math_cos(JSContext *cx, unsigned argc, js::Value *vp);

extern double
math_cos_impl(MathCache *cache, double x);

extern JSBool
math_exp(JSContext *cx, unsigned argc, js::Value *vp);

extern double
math_exp_impl(MathCache *cache, double x);

extern JSBool
math_tan(JSContext *cx, unsigned argc, js::Value *vp);

extern double
math_tan_impl(MathCache *cache, double x);

extern JSBool
math_log10(JSContext *cx, unsigned argc, js::Value *vp);

extern JSBool
math_log2(JSContext *cx, unsigned argc, js::Value *vp);

extern JSBool
math_log1p(JSContext *cx, unsigned argc, js::Value *vp);

extern JSBool
math_expm1(JSContext *cx, unsigned argc, js::Value *vp);

extern JSBool
math_cosh(JSContext *cx, unsigned argc, js::Value *vp);

extern JSBool
math_sinh(JSContext *cx, unsigned argc, js::Value *vp);

extern JSBool
math_tanh(JSContext *cx, unsigned argc, js::Value *vp);

extern JSBool
math_acosh(JSContext *cx, unsigned argc, js::Value *vp);

extern JSBool
math_asinh(JSContext *cx, unsigned argc, js::Value *vp);

extern JSBool
math_atanh(JSContext *cx, unsigned argc, js::Value *vp);

extern JSBool
math_hypot(JSContext *cx, unsigned argc, Value *vp);

extern JSBool
math_trunc(JSContext *cx, unsigned argc, Value *vp);

extern JSBool
math_sign(JSContext *cx, unsigned argc, Value *vp);

extern JSBool
math_cbrt(JSContext *cx, unsigned argc, Value *vp);

extern JSBool
math_asin(JSContext *cx, unsigned argc, Value *vp);

extern JSBool
math_acos(JSContext *cx, unsigned argc, Value *vp);

extern JSBool
math_atan(JSContext *cx, unsigned argc, Value *vp);

extern JSBool
math_atan2(JSContext *cx, unsigned argc, Value *vp);

extern double
ecmaAtan2(double x, double y);

extern double
math_atan_impl(MathCache *cache, double x);

extern JSBool
math_atan(JSContext *cx, unsigned argc, js::Value *vp);

extern double
math_asin_impl(MathCache *cache, double x);

extern JSBool
math_asin(JSContext *cx, unsigned argc, js::Value *vp);

extern double
math_acos_impl(MathCache *cache, double x);

extern JSBool
math_acos(JSContext *cx, unsigned argc, js::Value *vp);

extern double
powi(double x, int y);

extern double
ecmaPow(double x, double y);

extern JSBool
math_imul(JSContext *cx, unsigned argc, Value *vp);

extern double 
math_log10_impl(MathCache *cache, double x);

extern double 
math_log2_impl(MathCache *cache, double x);

extern double 
math_log1p_impl(MathCache *cache, double x);

extern double 
math_expm1_impl(MathCache *cache, double x);

extern double 
math_cosh_impl(MathCache *cache, double x);

extern double 
math_sinh_impl(MathCache *cache, double x);

extern double 
math_tanh_impl(MathCache *cache, double x);

extern double 
math_acosh_impl(MathCache *cache, double x);

extern double 
math_asinh_impl(MathCache *cache, double x);

extern double 
math_atanh_impl(MathCache *cache, double x);

extern double 
math_hypot_impl(double x, double y);

extern double 
math_trunc_impl(MathCache *cache, double x);

extern double 
math_sign_impl(MathCache *cache, double x);

extern double 
math_cbrt_impl(MathCache *cache, double x);

} /* namespace js */

#endif /* jsmath_h */
