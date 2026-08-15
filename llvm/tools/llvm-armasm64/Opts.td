include "llvm/Option/OptParser.td"

class ARMAsm64Flag<string name> : Flag<["-", "/"], name>;
class ARMAsm64Joined<string name> : Joined<["-", "/"], name>;
class ARMAsm64JoinedOrSeparate<string name> :
    JoinedOrSeparate<["-", "/"], name>;
class ARMAsm64Separate<string name> : Separate<["-", "/"], name>;

def help : ARMAsm64Flag<"help">, HelpText<"Display available options">;
def help_short : ARMAsm64Flag<"h">, Alias<help>;
def errors : ARMAsm64Separate<"errors">,
             MetaVarName<"<file>">,
             HelpText<"Redirect diagnostics to <file>">;
def errors_short : ARMAsm64Separate<"e">, Alias<errors>;
def include_path : ARMAsm64JoinedOrSeparate<"I">,
                   MetaVarName<"<dir>[;<dir>...]">,
                   HelpText<"Add directories to the include search path">;
def predefine : ARMAsm64JoinedOrSeparate<"predefine">,
                MetaVarName<"<directive>">,
                HelpText<"Pre-execute a SETA, SETL, or SETS directive">;
def predefine_short : ARMAsm64JoinedOrSeparate<"pd">, Alias<predefine>;
def no_warn : ARMAsm64Flag<"nowarn">, HelpText<"Disable all warnings">;
def no_warn_short : ARMAsm64Flag<"now">, Alias<no_warn>;
def ignore : ARMAsm64Separate<"ignore">, MetaVarName<"<warning-number>">,
             HelpText<"Disable the specified warning">;
def via : ARMAsm64Separate<"via">, MetaVarName<"<file>">,
          HelpText<"Read command-line arguments from <file>">;
def machine : ARMAsm64Separate<"machine">, MetaVarName<"<machine>">,
              HelpText<"Set the COFF machine type to ARM64 or ARM64EC">;
def debug_info : ARMAsm64Flag<"g">, HelpText<"Generate debugging information">;
def sha1 : ARMAsm64Flag<"gh:SHA1">,
           HelpText<"Use SHA-1 source checksums in debug information">;
def sha256 : ARMAsm64Flag<"gh:SHA_256">,
             HelpText<"Use SHA-256 source checksums in debug information">;
def function_override : ARMAsm64Joined<"funcOverride:">,
                        MetaVarName<"<function>">,
                        HelpText<"Emit function overriding support">;
def error_report : ARMAsm64Joined<"errorReport:">,
                   MetaVarName<"<mode>">, Flags<[HelpHidden]>;
def guard_ehcont : ARMAsm64Flag<"guard:ehcont">,
                   HelpText<"Emit EH continuation targets">;
def no_guard_ehcont : ARMAsm64Flag<"guard:ehcont-">,
                      HelpText<"Do not emit EH continuation targets">;
def no_escape : ARMAsm64Flag<"noesc">,
                HelpText<"Ignore C-style escape sequences">;
def no_escape_short : ARMAsm64Flag<"noe">, Alias<no_escape>;
def no_logo : ARMAsm64Flag<"nologo">, Flags<[HelpHidden]>;
def output : ARMAsm64JoinedOrSeparate<"o">, MetaVarName<"<file>">,
             HelpText<"Write object output to <file>">;
def output_ms : ARMAsm64JoinedOrSeparate<"Fo">, Alias<output>;
def source_link : ARMAsm64Joined<"sourcelink:">, MetaVarName<"<file>">,
                  HelpText<"Embed the specified Source Link file">;
