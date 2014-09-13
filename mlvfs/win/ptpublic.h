//---------------------------------------------------------------------------
// Copyright 1997-2013 Joe Lowe
//
// Permission is granted to any person obtaining a copy of this Software,
// to deal in the Software without restriction, including the rights to use,
// copy, modify, merge, publish, distribute, sublicense, and sell copies of
// the Software.
//
// The above copyright and permission notice must be left intact in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS WITHOUT WARRANTY.
//---------------------------------------------------------------------------
// filename:   ptpublic.h
// created:    2013.01.17
//
// Definitions for creating compiler, language (C/C++), and platform
// portable interfaces and definitions. Implementation code is not
// necessarily expected to use these definitions.
//---------------------------------------------------------------------------
#ifndef PTPUBLIC_H
#define PTPUBLIC_H

#ifndef PT_EXPAND
#define PT_EXPAND_(a) a
#define PT_EXPAND(a) PT_EXPAND_(a)
#endif
#ifndef PT_CAT
#define PT_CAT_(a,b) a##b
#define PT_CAT(a,b) PT_CAT_(a,b)
#endif
#ifndef PT_QUOTE
#define PT_QUOTE_(s) #s
#define PT_QUOTE(s) PT_QUOTE_(s)
#endif
#ifndef PT_LQUOTE
#define PT_LQUOTE(s) PT_CAT(L,PT_QUOTE_(s))
#endif

#if !defined(RC_INVOKED) && !defined(RC_PLIST)

#if (defined(INT_MAX) && LONG_MAX > INT_MAX) || defined(_WIN64) || defined(__x86_64__) || defined(__powerpc64__) || defined(__ppc64__) || defined(__mips64__) || defined(__arm64__) || defined(__ia64__)
 #define PT_SIZEOF_PTR 64
#else
 #define PT_SIZEOF_PTR 32
#endif
#if PT_SIZEOF_PTR == 64 && !defined(_WIN32)
 #define PT_SIZEOF_LONG 64
#else
 #define PT_SIZEOF_LONG 32
#endif
#define PT_CHAR8 char
#define PT_INT32 int
#define PT_UINT32 unsigned
#ifdef INT64_C
 #define PT_BOOL8 uint8_t
 #define PT_INT8 int8_t
 #define PT_UINT8 uint8_t
 #define PT_INT16 int16_t
 #define PT_UINT16 uint16_t
 #define PT_INT64 int64_t
 #define PT_UINT64 uint64_t
#else
 #define PT_BOOL8 unsigned char
 #define PT_INT8 signed char
 #define PT_UINT8 unsigned char
 #define PT_INT16 short
 #define PT_UINT16 unsigned short
 #ifdef _MSC_VER
  #define PT_INT64 __int64
  #define PT_UINT64 unsigned __int64
  #define PT_INT64_MIN  (-9223372036854775807i64 - 1)
  #define PT_INT64_MAX  9223372036854775807i64
  #define PT_UINT64_MAX 0xffffffffffffffffui64
 #elif PT_SIZEOF_LONG == 64
  #define PT_INT64 long
  #define PT_UINT64 unsigned long
  #define PT_INT64_MIN  (-9223372036854775807L - 1)
  #define PT_INT64_MAX  9223372036854775807L
  #define PT_UINT64_MAX 0xffffffffffffffffL
 #else
  #define PT_INT64 long long
  #define PT_UINT64 unsigned long long
  #define PT_INT64_MIN  (-9223372036854775807LL - 1)
  #define PT_INT64_MAX  9223372036854775807LL
  #define PT_UINT64_MAX 0xffffffffffffffffLL
 #endif
#endif
#ifdef NULL
 #define PT_SIZE_T size_t
#elif PT_SIZEOF_PTR == 32
 #define PT_SIZE_T unsigned
#else
 #define PT_SIZE_T PT_UINT64
#endif
#ifdef UUIDA_T_DEFINED
 #define PT_UUID uuida_t
#else
 typedef struct { PT_UINT32 data1; PT_UINT16 data2; PT_UINT16 data3; PT_UINT8 data4[8]; } pt_uuid_t;
 #define PT_UUID pt_uuid_t
#endif
#define PT_INT16LE PT_INT16
#define PT_UINT16LE PT_UINT16
#define PT_INT32LE PT_INT32
#define PT_UINT32LE PT_UINT32
#define PT_INT64LE PT_INT64
#define PT_UINT64LE PT_UINT64
#define PT_UUIDLE pt_uuid_t

#ifdef _WIN32
 #define PT_CCALL __cdecl
 #define PT_STDCALL __stdcall
 #define PT_EXPORT __declspec(dllexport)
 #define PT_IMPORT __declspec(dllimport)
 #ifdef _INC_WINDOWS
  #define PT_FD_T HANDLE
  #define PT_FD_INVALID INVALID_HANDLE_VALUE
 #else
  #define PT_FD_T void*
  #define PT_FD_INVALID ((void*)(ptrdiff_t)(-1))
 #endif
#else
 #define PT_CCALL
 #define PT_STDCALL
 #define PT_EXPORT
 #define PT_IMPORT
 #define PT_FD_T int
 #define PT_FD_INVALID -1
#endif

#define PT_CAST(t,e) ((t)(e))
#ifdef __cplusplus
 #define PT_SCAST(t,e) static_cast<t>(e)
 #define PT_CCAST(t,e) const_cast<t>(e)
 #define PT_RCAST(t,e) reinterpret_cast<t>(e)
 #define PT_EXTERNC extern "C"
 #define PT_EXTERNC_START extern "C" {
 #define PT_EXTERNC_END }
 #define PT_INLINE inline
 #define PT_INLINE2 inline
 #define PT_INLINE_BOOL PT_INLINE bool
 #define PT_STATIC_CONST(t,n,v) static const t n = v
 #define PT_TYPE_DECLARE(t) struct t
 #define PT_TYPE_DEFINE2(t) struct t
 #define PT_TYPE_DEFINE(t) struct t
 #define PT_TYPEUNION_DEFINE(t) union t
 #define PT_TYPEUNION_DECLARE(t) union t
 #define PT_TYPEUNION_DEFINE2(t) union t
 #define PT_INTERFACE_DECLARE(t) struct t
 #define PT_INTERFACE_DEFINE2 PT_TYPE_DEFINE2(INTERFACE_NAME)
 #define PT_INTERFACE_DEFINE PT_TYPE_DEFINE(INTERFACE_NAME)
 #define PT_INTERFACE_FUN0(r,f) virtual r PT_CCALL f(void) = 0
 #define PT_INTERFACE_FUNC(r,f,...) virtual r PT_CCALL f(__VA_ARGS__) = 0
 #define PT_VCAL0(o,f) ((o)->f())
 #define PT_VCALL(o,f,...) ((o)->f(__VA_ARGS__))
#else
 #define PT_SCAST(t,e) ((t)(e))
 #define PT_CCAST(t,e) ((t)(e))
 #define PT_RCAST(t,e) ((t)(e))
 #define PT_EXTERNC
 #define PT_EXTERNC_START
 #define PT_EXTERNC_END
 #define PT_INLINE static __inline
 #define PT_INLINE2 static __inline
 #define PT_INLINE_BOOL PT_INLINE int
 #define PT_STATIC_CONST(t,n,v) enum { n = v }
 #define PT_TYPE_DECLARE_(t) typedef struct t##_ t
 #define PT_TYPE_DECLARE(t) PT_TYPE_DECLARE_(t)
 #define PT_TYPE_DEFINE2_(t) struct t##_
 #define PT_TYPE_DEFINE2(t) PT_TYPE_DEFINE2_(t)
 #define PT_TYPE_DEFINE(t) PT_TYPE_DECLARE_(t); PT_TYPE_DEFINE2_(t)
 #define PT_TYPEUNION_DECLARE_(t) typedef union t##_ t
 #define PT_TYPEUNION_DECLARE(t) PT_TYPEUNION_DECLARE_(t)
 #define PT_TYPEUNION_DEFINE2_(t) union t##_
 #define PT_TYPEUNION_DEFINE2(t) PT_TYPEUNION_DEFINE2_(t)
 #define PT_TYPEUNION_DEFINE_(t) union t##_
 #define PT_TYPEUNION_DEFINE(t) PT_TYPEUNION_DECLARE(t); PT_TYPEUNION_DEFINE_(t)
 #define PT_INTERFACE_DECLARE_(t) typedef struct t##_vtbl_t_ t##_vtbl_t; typedef t##_vtbl_t* t
 #define PT_INTERFACE_DECLARE(t) PT_INTERFACE_DECLARE_(t)
 #define PT_INTERFACE_DEFINE2__(t) struct t##_vtbl_t_
 #define PT_INTERFACE_DEFINE2_(t) PT_INTERFACE_DEFINE2__(t)
 #define PT_INTERFACE_DEFINE2 PT_INTERFACE_DEFINE2_(INTERFACE_NAME)
 #define PT_INTERFACE_DEFINE PT_INTERFACE_DECLARE(INTERFACE_NAME); PT_INTERFACE_DEFINE2_(INTERFACE_NAME)
 #define PT_INTERFACE_FUN0_(t,r,f) r (PT_CCALL*f)(t*)
 #define PT_INTERFACE_FUN0(r,f) PT_INTERFACE_FUN0_(INTERFACE_NAME,r,f)
 #define PT_INTERFACE_FUNC_(t,r,f,...) r (PT_CCALL*f)(t*,__VA_ARGS__)
 #define PT_INTERFACE_FUNC(r,f,...) PT_INTERFACE_FUNC_(INTERFACE_NAME,r,f,__VA_ARGS__)
 #define PT_VCAL0(o,f) ((*(o))->f(o))
 #define PT_VCALL(o,f,...) ((*(o))->f(o,__VA_ARGS__))
#endif
#ifndef PT_ASSERT
 #ifdef ASSERT
  #define PT_ASSERT(e) ASSERT(e)
 #else
  #define PT_ASSERT(e)
 #endif
#endif

#ifdef _WIN32
 #define PT_EXESUFFIXA ".exe"
 #define PT_SHAREDEXTA "dll"
 #define PT_PLUGINEXTA "dll"
#elif defined(__APPLE__)
 #define PT_EXESUFFIXA ""
 #define PT_SHAREDEXTA "dylib"
 #define PT_PLUGINEXTA "bundle"
#else
 #define PT_EXESUFFIXA ""
 #define PT_SHAREDEXTA "so"
 #define PT_PLUGINEXTA "so"
#endif
#define PT_EXESUFFIXW PT_CAT(L,PT_EXESUFFIXA)
#define PT_SHAREDEXTW PT_CAT(L,PT_SHAREDEXTA)
#define PT_PLUGINEXTW PT_CAT(L,PT_PLUGINEXTA)

#endif

#endif
