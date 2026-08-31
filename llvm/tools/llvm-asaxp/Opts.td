include "llvm/Option/OptParser.td"

class AsAxpFlag<string name> : Flag<["-", "/"], name>;
class AsAxpJoined<string name> : Joined<["-", "/"], name>;
class AsAxpJoinedOrSeparate<string name> :
    JoinedOrSeparate<["-", "/"], name>;

// Digital/Microsoft ASAXP 2.1 command-line interface.
def help : AsAxpFlag<"help">, HelpText<"Display available options">;
def help_question : AsAxpFlag<"?">, Alias<help>;
def nologo : AsAxpFlag<"nologo">, HelpText<"Suppress the sign-on banner">;
def nopp : AsAxpFlag<"nopp">, HelpText<"Do not preprocess the input">;
def define : AsAxpJoinedOrSeparate<"D">, MetaVarName<"<symbol>[=<value>]">,
             HelpText<"Define a preprocessor symbol">;
def undefine : AsAxpJoinedOrSeparate<"U">, MetaVarName<"<symbol>">,
               HelpText<"Undefine a preprocessor symbol">;
def include_path : AsAxpJoinedOrSeparate<"I">, MetaVarName<"<dir>">,
                   HelpText<"Add an include search path">;
def output : AsAxpJoinedOrSeparate<"Fo">, MetaVarName<"<file>">,
             HelpText<"Write object output to <file>">;
def output_short : AsAxpJoinedOrSeparate<"o">, Alias<output>;
def debug_lines : AsAxpFlag<"Zd">,
                  HelpText<"Emit line and public-symbol debug information">;
def debug_info : AsAxpFlag<"Zi">,
                 HelpText<"Emit symbolic debug information">;
def optimize_off : AsAxpFlag<"O0">,
                   HelpText<"Disable assembly scheduling">;
def optimize_on : AsAxpFlag<"O1">,
                  HelpText<"Enable assembly scheduling (default)">;
def resumption_safe : AsAxpFlag<"resumption_safe">;
def symbols_aligned : AsAxpFlag<"symbols_aligned_0mod4">;
def symbols_unaligned : AsAxpFlag<"symbols_not_aligned">;
def stack_aligned : AsAxpFlag<"stack_aligned_0mod8">;
def stack_unaligned : AsAxpFlag<"stack_not_aligned">;
def eflag : AsAxpJoinedOrSeparate<"eflag">, MetaVarName<"<flags>">;
def qapdst : AsAxpFlag<"QApdst">;
def qapdsg : AsAxpFlag<"QApdsg">;
def qapdsa : AsAxpFlag<"QApdsa">;
def qapdie : AsAxpFlag<"QApdie">;
def qapdca : AsAxpFlag<"QApdca">;

// LLVM extension: one binary supports the historical Alpha and Alpha64 tools.
def machine : AsAxpJoined<"machine:">, MetaVarName<"<alpha|alpha64>">,
              HelpText<"Select Alpha TASO or Alpha64 COFF output">;
def taso : AsAxpFlag<"taso">, Alias<machine>, AliasArgs<["alpha"]>;
