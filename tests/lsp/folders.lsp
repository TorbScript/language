# A workspace of two folders, each a project of its own: the same name means what each folder declares, and the
# symbols of the workspace come from both
{"jsonrpc":"2.0","id":1,"method":"initialize","params":{"processId":null,"rootUri":null,"workspaceFolders":[{"uri":"{root}/one","name":"one"},{"uri":"{root}/two","name":"two"}],"capabilities":{}}}
{"jsonrpc":"2.0","method":"initialized","params":{}}
open one/src/main.trb
open two/src/main.trb
{"jsonrpc":"2.0","id":2,"method":"textDocument/hover","params":{"textDocument":{"uri":"{root}/one/src/main.trb"},"position":{"line":5,"character":7}}}
{"jsonrpc":"2.0","id":3,"method":"textDocument/hover","params":{"textDocument":{"uri":"{root}/two/src/main.trb"},"position":{"line":5,"character":7}}}
{"jsonrpc":"2.0","id":4,"method":"textDocument/definition","params":{"textDocument":{"uri":"{root}/two/src/main.trb"},"position":{"line":5,"character":7}}}
{"jsonrpc":"2.0","id":5,"method":"workspace/symbol","params":{"query":"greet"}}
{"jsonrpc":"2.0","id":6,"method":"shutdown","params":null}
{"jsonrpc":"2.0","method":"exit","params":null}
