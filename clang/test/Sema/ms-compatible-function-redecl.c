// RUN: %clang_cc1 -fsyntax-only -fms-compatibility -DMS -verify=ms %s
// RUN: %clang_cc1 -fsyntax-only -verify=std %s
// RUN: %clang_cc1 -x c++ -fsyntax-only -fms-compatibility -DCXX -verify=cxx %s

#ifndef CXX

// ms-note@+2 {{previous declaration is here}}
// std-note@+1 {{previous declaration is here}}
void one(int *);
// ms-warning@+2 {{parameter 1 of 'one' has a different pointer type in a redeclaration; this is a Microsoft compatibility extension}}
// std-error@+1 {{conflicting types for 'one'}}
void one(float *);

// ms-note@+2 {{previous declaration is here}}
// std-note@+1 {{previous declaration is here}}
void multiple(int *, float *, int);
// ms-warning@+3 {{parameter 1 of 'multiple' has a different pointer type in a redeclaration; this is a Microsoft compatibility extension}}
// ms-warning@+2 {{parameter 2 of 'multiple' has a different pointer type in a redeclaration; this is a Microsoft compatibility extension}}
// std-error@+1 {{conflicting types for 'multiple'}}
void multiple(float *, int *, int);

// ms-note@+2 {{previous declaration is here}}
// std-note@+1 {{previous declaration is here}}
void non_pointer(int *, int);
// ms-error@+2 {{conflicting types for 'non_pointer'}}
// std-error@+1 {{conflicting types for 'non_pointer'}}
void non_pointer(float *, long);

// ms-note@+2 {{previous declaration is here}}
// std-note@+1 {{previous declaration is here}}
int different_return(int *);
// ms-error@+2 {{conflicting types for 'different_return'}}
// std-error@+1 {{conflicting types for 'different_return'}}
void different_return(float *);

#ifdef MS
void calls(float *fp) {
  one(fp);
}
#endif

#else

// cxx-note@+1 {{previous declaration is here}}
extern "C" void c_linkage(int *);
// cxx-error@+1 {{conflicting types for 'c_linkage'}}
extern "C" void c_linkage(float *);

#endif
