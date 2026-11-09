; Keywords
[
  "if"
  "else"
  "for"
  "while"
  "do"
  "return"
  "match"
  "switch"
  "case"
  "default"
  "break"
  "continue"
  "goto"
  "enum"
  "struct"
  "union"
  "typedef"
] @keyword

[
  "static"
  "const"
  "extern"
  "volatile"
  "register"
  "inline"
  "signed"
  "unsigned"
  "sizeof"
] @keyword.modifier

; Types
[
  "void"
  "int"
  "float"
  "double"
  "char"
  "bool"
  "long"
  "short"
  "string"
  "Result"
  "map"
] @type.builtin

; Constants
[
  "true"
  "false"
  "null"
  "NULL"
] @constant.builtin

; Result constructors
[
  "Ok"
  "Err"
] @constructor

; Builtins
(call_expression
  function: (identifier) @function.builtin
  (#eq? @function.builtin "print"))

; Function definitions
(function_definition
  declarator: (function_declarator
    declarator: (identifier) @function))

; Function calls
(call_expression
  function: (identifier) @function.call)

; Scoped identifiers (Type::Variant)
(scoped_identifier
  scope: (identifier) @type
  name: (identifier) @constructor)

; Enum definitions
(enum_specifier
  name: (type_identifier) @type)

; Type identifiers
(type_identifier) @type

; Field access (.len, .push, .get, .set, .has)
(field_expression
  field: (field_identifier) @property)

; String literals
(string_literal) @string

; Char literals
(char_literal) @string.special

; Number literals
(number_literal) @number

; Comments
(comment) @comment

; Preprocessor
(preproc_include) @keyword.import
(preproc_directive) @keyword.directive
(system_lib_string) @string.special
(preproc_def) @keyword.directive
(preproc_ifdef) @keyword.directive
(preproc_else) @keyword.directive
(preproc_endif) @keyword.directive

; Fat arrow
"=>" @operator

; Scope resolution
"::" @punctuation.delimiter

; Operators
[
  "="
  "+"
  "-"
  "*"
  "/"
  "%"
  "=="
  "!="
  "<"
  ">"
  "<="
  ">="
  "&&"
  "||"
  "!"
  "+="
  "-="
  "*="
  "/="
  "++"
  "--"
  "&"
  "|"
  "^"
  "~"
  "<<"
  ">>"
  "->"
  "."
] @operator

; Punctuation
[
  "("
  ")"
  "{"
  "}"
  "["
  "]"
] @punctuation.bracket

[
  ","
  ";"
  ":"
] @punctuation.delimiter

; Moxy's contextual keywords parse as identifiers in the C grammar
((identifier) @keyword
  (#any-of? @keyword "match" "defer" "await" "in" "namespace" "using" "ref" "weak" "fn" "auto"))

((type_identifier) @keyword
  (#any-of? @keyword "ref" "weak" "namespace"))

((type_identifier) @type.builtin
  (#any-of? @type.builtin "string" "Result" "Future" "map"))

((identifier) @function.builtin
  (#any-of? @function.builtin "print" "assert" "panic"))
