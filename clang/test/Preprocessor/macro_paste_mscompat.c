// RUN: %clang_cc1 -E -P -fms-compatibility -Werror %s | FileCheck %s --check-prefix=MS
// RUN: %clang_cc1 -E -P -DSTANDARD %s | FileCheck %s --check-prefix=STANDARD

#define MAJOR 12
#define HEX(value) 0x##value

#ifdef STANDARD

standard HEX(MAJOR)
// STANDARD: standard 0xMAJOR

#else

numeric HEX(MAJOR)
// MS: numeric 0x12

#define DOT(member) .##member
#define ARROW(member) ->##member
member object DOT(field)
pointer pointer ARROW(field)
// MS: member object .field
// MS: pointer pointer ->field

#define IDENT_STRING(ident, string) ident##string
#define STRINGS(left, right) left##right
identifier_string IDENT_STRING(name, "suffix")
strings STRINGS("left", "right")
// MS: identifier_string name"suffix"
// MS: strings "left""right"

#define FUNCTION_STRING(string) __FUNCTION__##string
void function(void) {
  const char *name = FUNCTION_STRING(": suffix");
}
// MS: const char *name = __FUNCTION__": suffix";

#endif
