; RUN: not llvm-ml64 -filetype=s %s /Fo /dev/null 2>&1 | FileCheck %s

first TEXTEQU <second>
second TEXTEQU <first>

.code
mov eax, first

; CHECK: error: text macro expansion of '{{first|second}}' exceeds the maximum nesting depth

END
