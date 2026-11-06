include "llvm/Option/OptParser.td"

class ARMASMFlag<string name> : Flag<["-", "/"], name>;
class ARMASMJoined<string name> : Joined<["-", "/"], name>;
class ARMASMJoinedOrSeparate<string name> :
    JoinedOrSeparate<["-", "/"], name>;
class ARMASMSeparate<string name> : Separate<["-", "/"], name>;

def help : ARMASMFlag<"help">, HelpText<"Display available options">;
def help_short : ARMASMFlag<"h">, Alias<help>;
def errors : ARMASMSeparate<"errors">,
             MetaVarName<"<file>">,
             HelpText<"Redirect diagnostics to <file>">;
def errors_short : ARMASMSeparate<"e">, Alias<errors>;
def include_path : ARMASMJoinedOrSeparate<"I">,
                   MetaVarName<"<dir>[;<dir>...]">,
                   HelpText<"Add directories to the include search path">;
def predefine : ARMASMJoinedOrSeparate<"predefine">,
                MetaVarName<"<directive>">,
                HelpText<"Pre-execute a SETA, SETL, or SETS directive">;
def predefine_short : ARMASMJoinedOrSeparate<"pd">, Alias<predefine>;
def no_warn : ARMASMFlag<"nowarn">, HelpText<"Disable all warnings">;
def no_warn_short : ARMASMFlag<"now">, Alias<no_warn>;
def ignore : ARMASMSeparate<"ignore">,
             MetaVarName<"<warning-number>[,...]">,
             HelpText<"Disable the specified warning">;
def via : ARMASMSeparate<"via">, MetaVarName<"<file>">,
          HelpText<"Read command-line arguments from <file>">;
def machine : ARMASMSeparate<"machine">, MetaVarName<"<machine>">,
              HelpText<"Set the COFF machine type: ARM64 or ARM64EC "
                       "(armasm64), ARM, THUMB or ARMCE (armasm)">;
def debug_info : ARMASMFlag<"g">, HelpText<"Generate debugging information">;
def sha1 : ARMASMFlag<"gh:SHA1">,
           HelpText<"Use SHA-1 source checksums in debug information">;
def sha256 : ARMASMFlag<"gh:SHA_256">,
             HelpText<"Use SHA-256 source checksums in debug information">;
def function_override : ARMASMJoined<"funcOverride:">,
                        MetaVarName<"<function>">,
                        HelpText<"Emit function overriding support">;
def error_report : ARMASMJoined<"errorReport:">,
                   MetaVarName<"<mode>">, Flags<[HelpHidden]>;
def guard_ehcont : ARMASMFlag<"guard:ehcont">,
                   HelpText<"Emit EH continuation targets">;
def no_guard_ehcont : ARMASMFlag<"guard:ehcont-">,
                      HelpText<"Do not emit EH continuation targets">;
def no_escape : ARMASMFlag<"noesc">,
                HelpText<"Ignore C-style escape sequences">;
def no_escape_short : ARMASMFlag<"noe">, Alias<no_escape>;
def no_logo : ARMASMFlag<"nologo">, Flags<[HelpHidden]>;
def output : ARMASMJoinedOrSeparate<"o">, MetaVarName<"<file>">,
             HelpText<"Write object output to <file>">;
def output_ms : ARMASMJoinedOrSeparate<"Fo">, Alias<output>;
def source_link : ARMASMJoined<"sourcelink:">, MetaVarName<"<file>">,
                  HelpText<"Embed the specified Source Link file">;

// armasm (32-bit) only.
def thumb_mode : ARMASMFlag<"16">,
                 HelpText<"Assemble source as Thumb-2 instructions (default)">;
def arm_mode : ARMASMFlag<"32">,
               HelpText<"Assemble source as ARM instructions (not supported)">;
def old_it : ARMASMFlag<"oldit">,
             HelpText<"Generate pre-ARMv8 IT blocks">;
def coff_thumb2_only : ARMASMFlag<"coff_thumb2_only">,
                       HelpText<"Allow only Thumb-2 code">;
