# Every feature over standard input and output: the diagnostics of an opened document (a lint finding, as a hint),
# hover, go to definition, completion behind a `.`, the semantic tokens, a quick fix, and a keystroke that breaks the
# document and the diagnostics it brings
{"jsonrpc":"2.0","id":1,"method":"initialize","params":{"processId":null,"rootUri":"{root}","capabilities":{}}}
{"jsonrpc":"2.0","method":"initialized","params":{}}
open main.trb
{"jsonrpc":"2.0","id":2,"method":"textDocument/hover","params":{"textDocument":{"uri":"{root}/main.trb"},"position":{"line":18,"character":15}}}
{"jsonrpc":"2.0","id":3,"method":"textDocument/hover","params":{"textDocument":{"uri":"{root}/main.trb"},"position":{"line":20,"character":10}}}
{"jsonrpc":"2.0","id":4,"method":"textDocument/definition","params":{"textDocument":{"uri":"{root}/main.trb"},"position":{"line":20,"character":23}}}
{"jsonrpc":"2.0","id":5,"method":"textDocument/completion","params":{"textDocument":{"uri":"{root}/main.trb"},"position":{"line":20,"character":22},"context":{"triggerKind":2,"triggerCharacter":"."}}}
{"jsonrpc":"2.0","id":6,"method":"textDocument/semanticTokens/full","params":{"textDocument":{"uri":"{root}/main.trb"}}}
{"jsonrpc":"2.0","id":7,"method":"textDocument/codeAction","params":{"textDocument":{"uri":"{root}/main.trb"},"range":{"start":{"line":13,"character":0},"end":{"line":13,"character":30}},"context":{"diagnostics":[]}}}
{"jsonrpc":"2.0","method":"textDocument/didChange","params":{"textDocument":{"uri":"{root}/main.trb","version":2},"contentChanges":[{"range":{"start":{"line":18,"character":14},"end":{"line":18,"character":14}},"text":"\""}]}}
{"jsonrpc":"2.0","id":8,"method":"textDocument/hover","params":{"textDocument":{"uri":"{root}/main.trb"},"position":{"line":19,"character":8}}}
{"jsonrpc":"2.0","id":9,"method":"shutdown","params":null}
{"jsonrpc":"2.0","method":"exit","params":null}
