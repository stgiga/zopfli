/*
Copyright 2016 Mr_KrzYch00. All Rights Reserved.

Define some additional information for compiler
required by Zopfli KrzYmod.
*/

#ifndef DEFINES_H_
#define DEFINES_H_

#ifdef NDOUBLE
 typedef float zfloat;
#else
 typedef double zfloat;
#endif

#ifndef _THREAD_SAFE
 #define _THREAD_SAFE
#endif

#if (_XOPEN_SOURCE<500)
#define _XOPEN_SOURCE 500
#endif

#if defined _WIN32 || defined __CYGWIN__
 #ifdef __GNUC__
  #define DLL_PUBLIC __attribute__ ((dllexport))
 #else
  #define DLL_PUBLIC __declspec(dllexport)
 #endif
#else
 #if __GNUC__ >= 4
  #define DLL_PUBLIC __attribute__ ((visibility ("default")))
 #else
  #define DLL_PUBLIC
 #endif
#endif

#define _FILE_OFFSET_BITS 64

#define BITSET(v,p) (((v)>>(p)) & 1)

#endif
