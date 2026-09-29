# First-party code style

These guidelines apply to first-party code throughout this repository, including
tests. Preserve vendored code in `src/core/third-party` and do not edit generated
build output to enforce these guidelines.

Use `src/core/sm_bone.cpp` as the primary C++ style reference. Prefer its readable,
expanded functions and control flow when an existing file contains inconsistent
or compressed code. Read the surrounding code before editing and keep changes
consistent with the project's conventions.

## Formatting and readability

- Prioritize readable, maintainable code over minimizing lines or tokens.
  Instructions to keep responses concise apply to explanations, not source code.
- Match the surrounding indentation. Use tabs in tab-indented code and four
  spaces in space-indented code. Never introduce single-space indentation or mix
  tabs and spaces for the same block's indentation.
- Put each statement on its own line. Do not append assignments, calls, returns,
  or other statements to a preceding statement.
- Use braces and multiline bodies for `if`, `else`, `for`, `while`, and `do`
  statements, including single-statement bodies. Put nested control flow on
  separate, correctly indented lines.
- Put opening braces on the same line as the function or control statement;
  use `} else {` for an alternate branch.
- Use spaces around binary and assignment operators, after commas, and between
  control keywords and their parentheses: `if (condition)` and
  `for (auto& item : items)`.
- Follow the existing type spelling, such as `bone*` and `const node&`.
- Expand nontrivial functions and lambdas across multiple lines. A short lambda
  with a single simple expression may remain inline when that improves clarity.
- Break long calls, conditions, and initializers at logical boundaries. Indent
  continuation lines consistently rather than squeezing an expression onto one
  line or aligning it with excessive whitespace.
- Use blank lines between functions and between distinct logical steps within
  a function. Avoid both dense walls of statements and unnecessary blank lines.
- Use descriptive names consistent with nearby code. Avoid introducing terse
  abbreviations merely to shorten an implementation.

## Scope and review

- Apply these conventions to new code and the sections being changed. Avoid
  unrelated formatting churn unless a broader style cleanup is requested.
- Keep formatting-only changes free of unrelated renaming, refactoring, or
  behavior changes. Preserve comments, string contents, and include order.
- Before finishing, inspect the diff for compressed statements, inline nested
  control flow, inconsistent indentation, and departures from the reference
  style. Passing tests does not replace this readability review.
- If using a formatter, configure it to match these conventions and inspect its
  output. Do not assume its defaults or automatic brace insertion preserve the
  intended layout and control flow, particularly when formatting selected lines.
