# A problem of `torb check`, published with its range as an error; a change that replaces the whole text fixes it,
# and the diagnostics published then are none; closing the document clears them as well
{"jsonrpc":"2.0","id":1,"method":"initialize","params":{"processId":null,"rootUri":"{root}","capabilities":{}}}
{"jsonrpc":"2.0","method":"initialized","params":{}}
open main.trb
{"jsonrpc":"2.0","id":2,"method":"textDocument/hover","params":{"textDocument":{"uri":"{root}/main.trb"},"position":{"line":1,"character":8}}}
{"jsonrpc":"2.0","method":"textDocument/didChange","params":{"textDocument":{"uri":"{root}/main.trb","version":2},"contentChanges":[{"text":"const total: Int = 42\nprint total\n"}]}}
{"jsonrpc":"2.0","id":3,"method":"textDocument/hover","params":{"textDocument":{"uri":"{root}/main.trb"},"position":{"line":1,"character":8}}}
{"jsonrpc":"2.0","method":"textDocument/didClose","params":{"textDocument":{"uri":"{root}/main.trb"}}}
{"jsonrpc":"2.0","id":4,"method":"shutdown","params":null}
{"jsonrpc":"2.0","method":"exit","params":null}
