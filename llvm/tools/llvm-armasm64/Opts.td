include "llvm/Option/OptParser.td"

class ARMAsm64Flag<string name> : Flag<["-"], name>;

def help : ARMAsm64Flag<"help">, HelpText<"Display available options">;
def help_short : ARMAsm64Flag<"h">, Alias<help>;
