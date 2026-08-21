// RUN: %clang_cc1 -fsyntax-only -fms-compatibility -fms-extensions -verify %s

#define MAJOR 12
#define HEX(value) 0x##value

static_assert(HEX(MAJOR) == 0x12);

#define DOT(member) .##member
#define ARROW(member) ->##member

struct S {
  int member;
};

int access(S object, S *pointer) {
  return object DOT(member) + pointer ARROW(member);
}

#define STRINGS(left, right) left##right
static_assert(sizeof(STRINGS("left", "right")) == sizeof("leftright"));

#define FUNCTION_STRING(string) __FUNCTION__##string

constexpr bool equal(const char *Left, const char *Right) {
  while (*Left && *Left == *Right) {
    ++Left;
    ++Right;
  }
  return *Left == *Right;
}

void function() {
  static_assert(equal(FUNCTION_STRING(": suffix"), "function: suffix")); // expected-warning {{expansion of predefined identifier '__FUNCTION__' to a string literal is a Microsoft extension}}
}
