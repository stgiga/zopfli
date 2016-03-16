/*
Copyright 2016 Mr_KrzYch00. All Rights Reserved.

Implement OS-based DLL functions visibility.
*/

#ifndef DLLSPEC_H_
#define DLLSPEC_H_

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

#endif
