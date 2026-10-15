# Ember Language for VS Code

Support for the Ember programming language (`.em` files):

- the Ember logo as the file icon for `.em` files
- syntax highlighting: keywords, types, functions, strings, numbers, comments
- `Ctrl+/` toggles comments, brackets and quotes close automatically, blocks fold
- snippets: `main`, `fn`, `for`, `while`, `if`, `struct`, `enum`, `match`, `print`, `vec`

## Install

```bash
cd editors/vscode
npx @vscode/vsce package --allow-missing-repository
code --install-extension ember-lang-0.2.0.vsix
```

Then reload VS Code (`Ctrl+Shift+P` → "Developer: Reload Window").
