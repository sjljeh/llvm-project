include "llvm/Option/OptParser.td"

class F<string name, string help> :
    Flag<["-", "/"], name>, HelpText<help>;
class JS<string name, string help> :
    JoinedOrSeparate<["-", "/"], name>, HelpText<help>;

def input_ansi : F<"a", "Read the input file using the active code page">;
def output_ansi : F<"A", "Write message text using the active code page">;
def binary_name : F<"b", "Include the input basename in .bin filenames">;
def customer : F<"c", "Set the customer bit in message IDs">;
def decimal : F<"d", "Print facility and severity values in decimal">;
def header_extension : JS<"e", "Set the generated header extension">;
def header_dir : JS<"h", "Set the generated header directory">;
def max_length : JS<"m", "Warn when message text exceeds this length">;
def null_terminate : F<"n", "NUL-terminate message strings">;
def ole2 : F<"o", "Generate HRESULT definitions">;
def resource_dir : JS<"r", "Set the resource output directory">;
def input_unicode : F<"u", "Read UTF-16LE input without a byte order mark">;
def output_utf8 : F<"U8", "Write UTF-8 message text">;
def output_unicode : F<"U", "Write UTF-16LE message text">;
def verbose : F<"v", "Enable verbose output">;
def debug_dir : JS<"x", "Set the debug header directory">;
def basename : JS<"z", "Set the generated file basename">;
def codepage : JS<"cp", "Set the generated text-file encoding">;
def help : F<"?", "Display available options">;
