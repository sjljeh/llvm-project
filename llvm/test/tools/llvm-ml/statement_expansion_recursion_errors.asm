; RUN: not llvm-ml64 -filetype=s %s /Fo /dev/null 2>&1 | FileCheck %s

value TEXTEQU <%value>

.code
%value

; CHECK: error: statement expansion exceeds the maximum nesting depth

END
